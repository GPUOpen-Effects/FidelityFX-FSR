#
# enables multithreading compilation
#

add_compile_options(/MP)

#
# includes cauldron's helper cmakes
#
include(${CMAKE_CURRENT_SOURCE_DIR}/../../libs/cauldron/common.cmake)

#
# Add manifest so the app uses the right DPI settings
#
function(addManifest PROJECT_NAME)
    IF (MSVC)
        IF (CMAKE_MAJOR_VERSION LESS 3)
            MESSAGE(WARNING "CMake version 3.0 or newer is required use build variable TARGET_FILE")
        ELSE()
            ADD_CUSTOM_COMMAND(
                TARGET ${PROJECT_NAME}
                POST_BUILD
                COMMAND "mt.exe" -manifest \"${CMAKE_CURRENT_SOURCE_DIR}\\dpiawarescaling.manifest\" -inputresource:\"$<TARGET_FILE:${PROJECT_NAME}>\"\;\#1 -outputresource:\"$<TARGET_FILE:${PROJECT_NAME}>\"\;\#1
                COMMENT "Adding display aware manifest..." 
            )
        ENDIF()
    ENDIF(MSVC)
endfunction()