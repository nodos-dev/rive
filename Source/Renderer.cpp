#include <Nodos/PluginAPI.h>
#include <Nodos/PluginHelpers.hpp>

#include <rive/artboard.hpp>
#include <rive/renderer/rive_renderer.hpp>
#include <rive/renderer/render_target.hpp>

namespace nos::rive
{

struct RendererNode : NodeContext
{
	using NodeContext::NodeContext;

	nosResult OnCreate(nosFbNodePtr node) override
	{
		return NOS_RESULT_SUCCESS;
	}

	nosResult ExecuteNode(nosNodeExecuteParams* params) override
	{
		return NOS_RESULT_SUCCESS;
	}

	std::unique_ptr<::rive::Renderer> Renderer;
};

nosResult RegisterRenderer(nosNodeFunctions* node)
{
	NOS_BIND_NODE_CLASS(NOS_NAME("Renderer"), RendererNode, node);
	return NOS_RESULT_SUCCESS;
}

}