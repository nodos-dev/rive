set(USE_NOS_SDK_VERSION "1.4.0")
nos_find_sdk(${USE_NOS_SDK_VERSION} NOS_PLUGIN_SDK_TARGET NOS_SUBSYSTEM_SDK_TARGET NOS_SDK_DIR)
if (NOT DEFINED NOS_SDK_DIR)
    message(FATAL_ERROR "Nodos SDK with version ${USE_NOS_SDK_VERSION} not found, please either install it or choose different version")
endif()

set(MODULE_DEPENDENCIES "nos.sys.vulkan-6.24")
set(dep_idx 0)
foreach(module_name_version ${MODULE_DEPENDENCIES})
    # module_name_version: <MODULE_NAME>-<MODULE_VERSION>
    string(REPLACE "-" ";" module_name_version ${module_name_version})
    list(GET module_name_version 0 module_name)
    list(GET module_name_version 1 module_version)
    nos_get_module("${module_name}" "${module_version}" DEP_${dep_idx})
    list(APPEND MODULE_DEPENDENCIES_TARGETS ${DEP_${dep_idx}})
endforeach()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/External/rive-lib" "${CMAKE_CURRENT_BINARY_DIR}/External/rive-lib")
nos_group_targets("rive-lib" "External")
list(APPEND MODULE_DEPENDENCIES_TARGETS ${NOS_PLUGIN_SDK_TARGET} rive-lib dxgi d3d11 d3dcompiler.lib)

set(INCLUDE_FOLDERS "")
nos_add_plugin("nosRive" "${MODULE_DEPENDENCIES_TARGETS}" "${INCLUDE_FOLDERS}")
nos_group_targets("nosRive" "NOS Plugins")

set_target_properties("nosRive" PROPERTIES CXX_STANDARD 20)
# Always force MD (because rive.lib built in Release)
target_compile_options(nosRive PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/MD>)