add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/External/rive-lib" "${CMAKE_CURRENT_BINARY_DIR}/External/rive-lib")
nos_group_targets("rive-lib" "External")
list(APPEND MODULE_DEPENDENCIES_TARGETS rive-lib dxgi d3d11 d3dcompiler.lib)

target_link_libraries(${NOS_PLUGIN_TARGET} PRIVATE "${MODULE_DEPENDENCIES_TARGETS}")

set_target_properties(${NOS_PLUGIN_TARGET} PROPERTIES CXX_STANDARD 20)
# Always force MD (because rive.lib built in Release)
target_compile_options(${NOS_PLUGIN_TARGET} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/MD>)