#include <Nodos/PluginAPI.h>
#include <Nodos/PluginHelpers.hpp>

#include <rive/artboard.hpp>
#include <rive/renderer/rive_renderer.hpp>
#include <rive/renderer/render_target.hpp>
#include <rive/renderer/render_context.hpp>
#include <rive/renderer/d3d/render_context_d3d_impl.hpp>
#include <rive/file.hpp>
#include <rive/factory.hpp>

#include <nosVulkanSubsystem/nosVulkanSubsystem.h>
#include <nosVulkanSubsystem/Helpers.hpp>

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

namespace nos::rive
{
// TODO: Implement RenderContext for nos.sys.vulkan


#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <windows.h>
#include <stdexcept>

#include <rive/renderer/render_target.hpp>

class SharedD3DRenderTarget : public ::rive::gpu::RenderTarget
{
public:
	static ::rive::rcp<SharedD3DRenderTarget> Create(ID3D11Device* device, uint32_t width, uint32_t height)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // Try this instead of R8G8B8A8_UNORM
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

		ComPtr<ID3D11Texture2D> tex;
		HRESULT hr = device->CreateTexture2D(&desc, nullptr, &tex);
		if (FAILED(hr))
		{
			return nullptr;
		}

		return ::rive::rcp<SharedD3DRenderTarget>(new SharedD3DRenderTarget(width, height, tex));
	}

	size_t GetAllocationSize() const
	{
		D3D11_TEXTURE2D_DESC desc;
		Texture->GetDesc(&desc);

		UINT64 totalSize = 0;
		UINT width = desc.Width;
		UINT height = desc.Height;
		UINT mipLevels = desc.MipLevels ? desc.MipLevels : 1;

		for (UINT mip = 0; mip < mipLevels; ++mip)
		{
			UINT w = std::max(1u, width >> mip);
			UINT h = std::max(1u, height >> mip);
			UINT rowPitch = 0;
			UINT slicePitch = 0;

			// You can use DXGI format helpers here
			DXGI_FORMAT fmt = desc.Format;

			// Estimate pitch (uncompressed formats)
			UINT bpp = 32;
			rowPitch = (w * bpp + 7) / 8;
			slicePitch = rowPitch * h;

			totalSize += slicePitch * desc.ArraySize;
		}
		return totalSize;
	}

	ComPtr<ID3D11Texture2D> GetTexture() const { return Texture; }

	HANDLE CreateSharedHandle()
	{
		ComPtr<IDXGIResource1> dxgiRes;
		HRESULT hr = Texture.As(&dxgiRes);
		if (FAILED(hr))
		{
			return 0;
		}

		HANDLE handle = nullptr;
		hr = dxgiRes->CreateSharedHandle(
			nullptr,
			DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
			nullptr,
			&handle
		);

		if (FAILED(hr))
		{
			return 0;
		}

		return handle;
	}

private:
	SharedD3DRenderTarget(uint32_t width, uint32_t height, ComPtr<ID3D11Texture2D> tex)
		: RenderTarget(width, height), Texture(tex) {}

	ComPtr<ID3D11Texture2D> Texture;
};


struct RendererNode : NodeContext
{
	using NodeContext::NodeContext;

	nosResult OnCreate(nosFbNodePtr node) override
	{
		// Create D3D11 device and context
		UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};

		ComPtr<IDXGIFactory> dxgiFactory;
		HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&dxgiFactory);

		ComPtr<IDXGIAdapter> adapter;
		for (UINT i = 0; dxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC desc;
			adapter->GetDesc(&desc);

			std::wstring name(desc.Description);
			if (name.find(L"NVIDIA") != std::wstring::npos) // or check for "RTX 4090"
			{
				break; // Found our target GPU
			}
			adapter.Reset(); // Try next
		}

		if (!adapter)
		{
			return NOS_RESULT_FAILED; // No discrete GPU found
		}

		hr = D3D11CreateDevice(
			adapter.Get(),
			D3D_DRIVER_TYPE_UNKNOWN, // Required when passing adapter
			nullptr,
			createDeviceFlags,
			featureLevels,
			ARRAYSIZE(featureLevels),
			D3D11_SDK_VERSION,
			&Device,
			nullptr,
			&Context
		);

		if (FAILED(hr))
		{
			return NOS_RESULT_FAILED;
		}

		// Create Rive render context with D3D11 device
		::rive::gpu::RenderContextD3DImpl::ContextOptions options{
			.disableRasterizerOrderedViews = false,
		.disableTypedUAVLoadStore = false};
		RenderContext = ::rive::gpu::RenderContextD3DImpl::MakeContext(Device, Context, options);
		return NOS_RESULT_SUCCESS;
	}

	nosResult ExecuteNode(nosNodeExecuteParams* params) override
	{
		Renderer = std::make_unique<::rive::RiveRenderer>(RenderContext.get());
		::rive::gpu::RenderContext::FrameDescriptor frameDesc{};
		frameDesc.renderTargetHeight = 1080;
		frameDesc.renderTargetWidth = 1920;
		frameDesc.loadAction = ::rive::gpu::LoadAction::preserveRenderTarget;
		frameDesc.clearColor = ::rive::colorARGB(255, 0, 255, 255);

		if (!RenderTarget)
		{
			auto d3dCtx = RenderContext->static_impl_cast<::rive::gpu::RenderContextD3DImpl>();
			auto sharedTarget = SharedD3DRenderTarget::Create(Device.Get(), frameDesc.renderTargetWidth, frameDesc.renderTargetHeight);
			auto renderTarget = d3dCtx->makeRenderTarget(frameDesc.renderTargetWidth, frameDesc.renderTargetHeight);
			auto sharedHandle = sharedTarget ->CreateSharedHandle();
			
			renderTarget->setTargetTexture(sharedTarget->GetTexture());
			auto supportsUAV = renderTarget->targetTextureSupportsUAV();
			auto a = renderTarget->offscreenTexture();
			RenderTarget = renderTarget;
			
			nosExternalMemoryInfo external = {};
			external.HandleType = NOS_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
			external.Handle = reinterpret_cast<uint64_t>(sharedHandle);
			external.Offset = 0;
			external.AllocationSize = sharedTarget->GetAllocationSize();
			external.PID = GetCurrentProcessId();
			nosResourceShareInfo imported = {};
			imported.Info.Type = NOS_RESOURCE_TYPE_TEXTURE;
			imported.Info.Texture.Width = frameDesc.renderTargetWidth;
			imported.Info.Texture.Height = frameDesc.renderTargetHeight;
			imported.Info.Texture.Format = NOS_FORMAT_R8G8B8A8_UNORM;
			imported.Info.Texture.Usage = nosImageUsage(NOS_IMAGE_USAGE_SAMPLED | NOS_IMAGE_USAGE_TRANSFER_SRC);
			imported.Info.Texture.FieldType = NOS_TEXTURE_FIELD_TYPE_PROGRESSIVE;
			imported.Memory = {
				.Handle = 0, // Let Vulkan assign
				.Size = sharedTarget->GetAllocationSize(),   // Let Vulkan assign
				.ExternalMemory = external
			};
			auto res = nosVulkan->ImportResource(&imported, "Rive Imported Render Target");
			if (res != NOS_RESULT_SUCCESS)
			{
				return res;
			}
			if (imported.Memory.Handle == 0)
			{
				return NOS_RESULT_FAILED;
			}
			Imported = imported;
			// Set output texture
			auto buf = vkss::TexturePinData::Pack(Imported);
			nosEngine.SetPinValueByName(NodeId, NOS_NAME("Output"), buf);
		}

		// Load and draw Rive file if not already loaded
		if (!Artboard)
		{
			// TODO: Replace with actual Rive file path
			const char* riveFilePath = "C:/Users/MSA/Downloads/sample.riv";
			
			// Load Rive file
			std::vector<uint8_t> bytes;

			std::filesystem::path path(riveFilePath);
			if (!std::filesystem::exists(path))
			{
				return NOS_RESULT_FAILED;
			}
			// Load file
			{
				std::ifstream file(path, std::ios::ate | std::ios::binary);
				if (!file.is_open())
				{
					return NOS_RESULT_FAILED;
				}
				size_t fileSize = (size_t)file.tellg();
				bytes.resize(fileSize);
				file.seekg(0);
				file.read(reinterpret_cast<char*>(bytes.data()), fileSize);
			}
			
			::rive::ImportResult result;
			RiveFile = ::rive::File::import(bytes, RenderContext.get(), &result);
			
			if (result != ::rive::ImportResult::success)
			{
				return NOS_RESULT_FAILED;
			}

			// Create artboard instance
			Artboard = RiveFile->artboardDefault();
			if (!Artboard)
			{
				return NOS_RESULT_FAILED;
			}

			// Set artboard size to match render target
			Artboard->width(frameDesc.renderTargetWidth);
			Artboard->height(frameDesc.renderTargetHeight);
		}

		RenderContext->beginFrame(frameDesc);

		// Draw artboard
		Artboard->draw(Renderer.get());
		Artboard->advance(0.016f);

		const ::rive::gpu::RenderContext::FlushResources flushRes{
			RenderTarget.get()
		};
		RenderContext->flush(flushRes);
		
		Renderer.reset();
		return NOS_RESULT_SUCCESS;
	}

	std::unique_ptr<::rive::RiveRenderer> Renderer;
	std::unique_ptr<::rive::gpu::RenderContext> RenderContext;
	::rive::rcp<::rive::gpu::RenderTarget> RenderTarget;
	nosResourceShareInfo Imported{};
	std::unique_ptr<::rive::File> RiveFile;
	std::unique_ptr<::rive::ArtboardInstance> Artboard;

	ComPtr<ID3D11Device> Device;
	ComPtr<ID3D11DeviceContext> Context;
};

nosResult RegisterRenderer(nosNodeFunctions* node)
{
	NOS_BIND_NODE_CLASS(NOS_NAME("Renderer"), RendererNode, node);
	return NOS_RESULT_SUCCESS;
}

}
