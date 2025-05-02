#include <Nodos/PluginAPI.h>
#include <Nodos/PluginHelpers.hpp>
#include <Nodos/Helpers.hpp>

#include <nosVulkanSubsystem/nosVulkanSubsystem.h>

NOS_INIT()
NOS_VULKAN_INIT()

NOS_BEGIN_IMPORT_DEPS()
	NOS_VULKAN_IMPORT()
NOS_END_IMPORT_DEPS()

namespace nos::rive
{

enum class Nodes : uint8_t
{
	Renderer = 0,
	Count,
};

nosResult RegisterRenderer(nosNodeFunctions* node);

nosResult NOSAPI_CALL ExportNodeFunctions(size_t* outCount, nosNodeFunctions** outFunctions)
{
	*outCount = (size_t)(Nodes::Count);
	if (!outFunctions)
		return NOS_RESULT_SUCCESS;
	
#define GEN_CASE_NODE(name)					\
	case Nodes::name: {						\
		auto ret = Register##name(node);	\
		if (NOS_RESULT_SUCCESS != ret)		\
			return ret;						\
		break;								\
	}

	for (size_t i = 0; i < (size_t)Nodes::Count; ++i)
	{
		auto node = outFunctions[i];
		switch ((Nodes)i)
		{
			GEN_CASE_NODE(Renderer)
		}
	}

#undef GEN_CASE_NODE
	return NOS_RESULT_SUCCESS;
}

extern "C"
{
NOSAPI_ATTR nosResult NOSAPI_CALL nosExportPlugin(nosPluginFunctions* out)
{
	out->ExportNodeFunctions = ExportNodeFunctions;
	return NOS_RESULT_SUCCESS;
}
}
}
