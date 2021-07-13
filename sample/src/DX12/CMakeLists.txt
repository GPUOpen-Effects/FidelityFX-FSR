project (FSRSample_DX12)

set(sources
	FSR_Filter.cpp
	FSR_Filter.h
    FSRSample.cpp
    FSRSample.h
    FSRTonemapping.h
    SampleRenderer.cpp
    SampleRenderer.h
    stdafx.cpp
    stdafx.h
    dpiawarescaling.manifest)

set(Shaders_src
    ${CMAKE_CURRENT_SOURCE_DIR}/FSR_Pass.hlsl
    ${CMAKE_CURRENT_SOURCE_DIR}/FSR_Tonemapping.hlsl
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-fsr/ffx_a.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-fsr/ffx_fsr1.h)

source_group("Sources" FILES ${sources})
source_group("shaders" FILES ${Shaders_src})
source_group("Icon"    FILES ${icon_src}) # defined in top-level CMakeLists.txt

copyCommand("${Shaders_src}" ${CMAKE_HOME_DIRECTORY}/bin/ShaderLibDX)

add_executable(${PROJECT_NAME} WIN32 ${sources} ${Shaders_src} ${common} ${icon_src})
target_link_libraries(${PROJECT_NAME} LINK_PUBLIC FSRSample_Common Cauldron_DX12 ImGUI amd_ags d3dcompiler D3D12)
target_include_directories (${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../ffx-fsr)

set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_HOME_DIRECTORY}/bin" DEBUG_POSTFIX "d")
set_source_files_properties(${Shaders_src} PROPERTIES VS_TOOL_OVERRIDE "Text")
