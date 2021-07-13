# Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

cmake_minimum_required(VERSION 3.12.1)

option (GFX_API_DX12 "Build with DX12" ON)
option (GFX_API_VK "Build with Vulkan" ON)

if(NOT DEFINED GFX_API)
    project (FSRSample)
else()
    project (FSRSample_${GFX_API})

    set_property(DIRECTORY ${CMAKE_PROJECT_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

    if(GFX_API STREQUAL DX12)
        set(GFX_API_DX12 ON)
        set(GFX_API_VK OFF)
    elseif(GFX_API STREQUAL VK)
        set(GFX_API_DX12 OFF)
        set(GFX_API_VK ON)
    else()
        message(STATUS "----------------------------------------------------------------------------------------")
        message(STATUS "")
        message(STATUS "** Almost there!!")
        message(STATUS "")
        message(STATUS " This framework supports DX12 and VULKAN, you need to invoke cmake in one of these ways:")
        message(STATUS "")
        message(STATUS " Examples:")
        message(STATUS "  Generate selected one:")
        message(STATUS "    cmake <project_root_dir> -DGFX_API=DX12")
        message(STATUS "    cmake <project_root_dir> -DGFX_API=VK")
        message(STATUS "  Generate with switches (Default is ON):")
        message(STATUS "    cmake <project_root_dir> [-DGFX_API_DX12=ON|OFF] [-DGFX_API_VK=ON|OFF]")
        message(STATUS "")
        message(STATUS "----------------------------------------------------------------------------------------")
        message(FATAL_ERROR "")
    endif()
endif()

# Check MSVC toolset version, Visual Studio 2019 required
if(MSVC_TOOLSET_VERSION VERSION_LESS 142)
    message(FATAL_ERROR "Cannot find MSVC toolset version 142 or greater. Please make sure Visual Studio 2019 or newer installed")
endif()

# ouput exe to bin directory
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin)

foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_HOME_DIRECTORY}/bin )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

add_compile_options(/MP)

# reference libs used by both backends
add_subdirectory(libs/cauldron)
add_subdirectory(src/Common)

# application icon
set(icon_src
	${CMAKE_CURRENT_SOURCE_DIR}/libs/cauldron/src/common/Icon/GPUOpenChip.ico
	${CMAKE_CURRENT_SOURCE_DIR}/libs/cauldron/src/common/Icon/resource.h
	${CMAKE_CURRENT_SOURCE_DIR}/libs/cauldron/src/common/Icon/Cauldron_Common.rc
)

if(GFX_API_VK)
    find_package(Vulkan REQUIRED)
    add_subdirectory(src/VK)
endif()
if(GFX_API_DX12)
    add_subdirectory(src/DX12)
endif()

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/libs/cauldron/src/common/Icon/Cauldron_Common.rc PROPERTIES VS_TOOL_OVERRIDE "Resource compiler")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/libs/cauldron/src/common/Icon/GPUOpenChip.ico  PROPERTIES VS_TOOL_OVERRIDE "Image")
