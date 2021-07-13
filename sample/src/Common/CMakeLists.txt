include(${CMAKE_CURRENT_SOURCE_DIR}/../../common.cmake)

add_library(FSRSample_Common INTERFACE)

set(config
    ${CMAKE_CURRENT_SOURCE_DIR}/../Common/FSRSample.json
)

copyTargetCommand("${config}" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} copied_common_config)
add_dependencies(FSRSample_Common copied_common_config)
