#include <Nodos/PluginAPI.h>
#include <Nodos/PluginHelpers.hpp>

#include <rive/artboard.hpp>
#include <rive/renderer/rive_renderer.hpp>
#include <rive/renderer/render_target.hpp>
#include <rive/renderer/render_context.hpp>
#include <rive/renderer/d3d/render_context_d3d_impl.hpp>
#include <rive/viewmodel/runtime/viewmodel_runtime.hpp>
#include <rive/file.hpp>
#include <rive/factory.hpp>
#include <rive/animation/state_machine_instance.hpp>

#include <nosVulkanSubsystem/nosVulkanSubsystem.h>
#include <nosVulkanSubsystem/Helpers.hpp>

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include "rive/scene.hpp"

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
			return NOS_RESULT_FAILED;

		// Create the event query
		D3D11_QUERY_DESC queryDesc = {};
		queryDesc.Query = D3D11_QUERY_EVENT;
		queryDesc.MiscFlags = 0;

		Device->CreateQuery(&queryDesc, &Query);

		// Create Rive render context with D3D11 device
		::rive::gpu::RenderContextD3DImpl::ContextOptions options{
			.disableRasterizerOrderedViews = false,
			.disableTypedUAVLoadStore = false
		};
		RenderContext = ::rive::gpu::RenderContextD3DImpl::MakeContext(Device, Context, options);
		Renderer = std::make_unique<::rive::RiveRenderer>(RenderContext.get());

		FrameDesc.renderTargetWidth = 1920;
		FrameDesc.renderTargetHeight = 1080;

		AddPinValueWatcher(NOS_NAME("Resolution"), [this](nos::Buffer const& newVal, std::optional<nos::Buffer> oldValue) {
			auto& res = *newVal.As<nos::fb::vec2u>();
			FrameDesc.renderTargetWidth = res.x();
			FrameDesc.renderTargetHeight = res.y();
			Recreate();
		});

		AddPinValueWatcher(NOS_NAME("AssetPath"), [this](nos::Buffer const& newVal, std::optional<nos::Buffer> oldValue) {
			auto path = newVal.As<const char>();
			AssetPath = path;
			Recreate();
		});
		
		return NOS_RESULT_SUCCESS;
	}

	nosResult Recreate() {
		FrameDesc.loadAction = ::rive::gpu::LoadAction::dontCare;
		FrameDesc.clearColor = ::rive::colorARGB(255, 0, 255, 255);

		// Create render target
		{
			auto d3dCtx = RenderContext->static_impl_cast<::rive::gpu::RenderContextD3DImpl>();
			auto sharedTarget = SharedD3DRenderTarget::Create(Device.Get(), FrameDesc.renderTargetWidth, FrameDesc.renderTargetHeight);
			auto renderTarget = d3dCtx->makeRenderTarget(FrameDesc.renderTargetWidth, FrameDesc.renderTargetHeight);
			auto sharedHandle = sharedTarget ->CreateSharedHandle();
			
			renderTarget->setTargetTexture(sharedTarget->GetTexture());
			RenderTarget = renderTarget;
			
			nosExternalMemoryInfo external = {};
			external.HandleType = NOS_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
			external.Handle = reinterpret_cast<uint64_t>(sharedHandle);
			external.Offset = 0;
			external.AllocationSize = sharedTarget->GetAllocationSize();
			external.PID = GetCurrentProcessId();
			nosResourceShareInfo imported = {};
			imported.Info.Type = NOS_RESOURCE_TYPE_TEXTURE;
			imported.Info.Texture.Width = FrameDesc.renderTargetWidth;
			imported.Info.Texture.Height = FrameDesc.renderTargetHeight;
			imported.Info.Texture.Format = NOS_FORMAT_R8G8B8A8_UNORM;
			imported.Info.Texture.Usage = nosImageUsage(NOS_IMAGE_USAGE_SAMPLED | NOS_IMAGE_USAGE_TRANSFER_SRC);
			imported.Info.Texture.FieldType = NOS_TEXTURE_FIELD_TYPE_PROGRESSIVE;
			imported.Memory = {
				.Handle = 0, // Let Vulkan assign
				.Size = sharedTarget->GetAllocationSize(),   // Let Vulkan assign
				.ExternalMemory = external
			};
			auto res = nosVulkan->ImportResource(&imported, "Rive Imported Render Target");
			if (res != NOS_RESULT_SUCCESS || imported.Memory.Handle == 0)
			{
				nosEngine.LogE("Failed to import Rive render target.");
				if (res != NOS_RESULT_FAILED)
					res = NOS_RESULT_FAILED;
				return res;
			}
			if (Imported.Memory.Handle != 0)
				nosVulkan->DestroyResource(&Imported);
			Imported = imported;
			// Set output texture
			auto buf = vkss::TexturePinData::Pack(Imported);
			nosEngine.SetPinValueByName(NodeId, NOS_NAME("Output"), buf);
		}

		// Load Rive Asset
		{
			// Load Rive file
			std::vector<uint8_t> bytes;

			if (!std::filesystem::exists(AssetPath))
				return NOS_RESULT_FAILED;
			// Load file
			{
				std::ifstream file(AssetPath, std::ios::ate | std::ios::binary);
				if (!file.is_open())
				{
					nosEngine.LogE("Failed to open Rive file: %s", AssetPath.string().c_str());
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
				nosEngine.LogE("Failed to import Rive file: %s", AssetPath.string().c_str());
				return NOS_RESULT_FAILED;
			}

			// Create artboard instance
			Artboard = RiveFile->artboardDefault();
			if (!Artboard)
			{
				nosEngine.LogE("Failed to create artboard instance.");
				return NOS_RESULT_FAILED;
			}

			// Set artboard size to match render target
			Artboard->width(FrameDesc.renderTargetWidth);
			Artboard->height(FrameDesc.renderTargetHeight);

			auto viewModelRt = RiveFile->defaultArtboardViewModel(Artboard.get());
			if (viewModelRt)
				Properties = viewModelRt->properties();
			else
				Properties.clear();
			for (auto prop : Properties)
			{
				auto& propName = prop.name;
				nosEngine.LogI("Property: %s", propName.c_str());
			}

			// Fall back to default if named one not found
			StateMachine = Artboard->defaultStateMachine();
			
			// Finally try first available
			if (!StateMachine && Artboard->stateMachineCount() > 0) {
				StateMachine = Artboard->stateMachineAt(0);
			}
		}

		return NOS_RESULT_SUCCESS;
	}

	nosResult ExecuteNode(nosNodeExecuteParams* params) override
	{
		nos::NodeExecuteParams execParams = params;

		if (!RenderTarget)
			return NOS_RESULT_FAILED;

		if (!Artboard)
			return NOS_RESULT_FAILED;

		auto deltaSecs = execParams.GetDeltaTime();

		RenderContext->beginFrame(FrameDesc);
		
		// Advance state machine if we have one
		if (StateMachine) {
			StateMachine->advance(deltaSecs);
		}

		// Draw artboard
		Artboard->draw(Renderer.get());
		Artboard->advance(deltaSecs);

		const ::rive::gpu::RenderContext::FlushResources flushRes {
			RenderTarget.get()
		};
		RenderContext->flush(flushRes);
		
		// Wait on the CPU for GPU to finish
		Context->End(Query.Get());
		while (S_OK != Context->GetData(Query.Get(), nullptr, 0, 0))
			;

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
	ComPtr<ID3D11Query> Query = nullptr;

	::rive::gpu::RenderContext::FrameDescriptor FrameDesc;
	std::filesystem::path AssetPath;

	// Data binds:
	std::vector<::rive::PropertyData> Properties;

	std::unique_ptr<::rive::StateMachineInstance> StateMachine;
};

nosResult RegisterRenderer(nosNodeFunctions* node)
{
	NOS_BIND_NODE_CLASS(NOS_NAME("Renderer"), RendererNode, node);
	return NOS_RESULT_SUCCESS;
}

}
