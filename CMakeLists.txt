cmake_minimum_required(VERSION 3.20)

project(InterfaceCharacter
    VERSION 0.1.0
    DESCRIPTION "Measured interface-character DSP core and VST3 adapter seam"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(interface_character_dsp STATIC
    src/CharacterProcessor.cpp
)

target_include_directories(interface_character_dsp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (MSVC)
    target_compile_options(interface_character_dsp PRIVATE /W4 /permissive-)
else()
    target_compile_options(interface_character_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(interface_character_tests
    tests/test_character.cpp
)

target_link_libraries(interface_character_tests PRIVATE interface_character_dsp)

if (MSVC)
    target_compile_options(interface_character_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(interface_character_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME interface_character_tests COMMAND interface_character_tests)

# The VST3 wrapper is intentionally kept behind the Steinberg SDK boundary.
# Set VST3_SDK_DIR when the official SDK is available, then add the adapter
# target without changing the DSP core or its deterministic tests.
option(BUILD_VST3_ADAPTER "Build the VST3 adapter when an SDK is supplied" OFF)
if (BUILD_VST3_ADAPTER)
    if (NOT DEFINED VST3_SDK_DIR)
        message(FATAL_ERROR "BUILD_VST3_ADAPTER requires -DVST3_SDK_DIR=<path>")
    endif()

    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

    add_subdirectory(${VST3_SDK_DIR} ${CMAKE_BINARY_DIR}/vst3sdk EXCLUDE_FROM_ALL)

    set(interface_character_vst3_sources
        src/vst3/InterfaceCharacterController.cpp
        src/vst3/InterfaceCharacterController.hpp
        src/vst3/InterfaceCharacterProcessor.cpp
        src/vst3/InterfaceCharacterProcessor.hpp
        src/vst3/ParameterIds.hpp
        src/vst3/PluginIds.hpp
        src/vst3/entry.cpp
        src/CharacterProcessor.cpp
        src/CharacterProcessor.hpp
        src/Profile.hpp
    )

    smtg_add_vst3plugin(interface_character ${interface_character_vst3_sources})
    target_compile_features(interface_character PRIVATE cxx_std_17)
    target_include_directories(interface_character PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(interface_character PRIVATE sdk)
    smtg_target_configure_version_file(interface_character)

    if (WIN32)
        set_target_properties(interface_character PROPERTIES
            OUTPUT_NAME "InterfaceCharacter"
        )
    endif()
endif()
