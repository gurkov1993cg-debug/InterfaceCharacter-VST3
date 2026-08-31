cmake_minimum_required(VERSION 3.22)
project(RT60RoomDecayCorrector VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rt60_core STATIC
    src/core/DecayModel.cpp
    src/core/MeasurementEngine.cpp
    src/core/CorrectionEngine.cpp
    src/core/DecayUniformity.cpp
)
target_include_directories(rt60_core PUBLIC src)

add_executable(rt60_core_tests tests/test_decay_model.cpp)
target_link_libraries(rt60_core_tests PRIVATE rt60_core)
add_executable(rt60_measurement_tests tests/test_measurement_engine.cpp)
target_link_libraries(rt60_measurement_tests PRIVATE rt60_core)
add_executable(rt60_correction_tests tests/test_correction_engine.cpp)
target_link_libraries(rt60_correction_tests PRIVATE rt60_core)
add_executable(rt60_uniformity_tests tests/test_uniformity.cpp)
target_link_libraries(rt60_uniformity_tests PRIVATE rt60_core)
enable_testing()
add_test(NAME rt60_core_tests COMMAND rt60_core_tests)
add_test(NAME rt60_measurement_tests COMMAND rt60_measurement_tests)
add_test(NAME rt60_correction_tests COMMAND rt60_correction_tests)
add_test(NAME rt60_uniformity_tests COMMAND rt60_uniformity_tests)

option(RT60_BUILD_JUCE "Build Standalone and VST3 with JUCE" OFF)
if(RT60_BUILD_JUCE)
    add_compile_definitions(JUCE_VST3_CAN_REPLACE_VST2=0)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)

    juce_add_gui_app(RT60RoomDecayStandalone
        PRODUCT_NAME "RT60 Room Decay Corrector"
        COMPANY_NAME "Room Decay Lab"
        BUNDLE_ID "com.roomdecaylab.rt60corrector.standalone"
    )
    target_sources(RT60RoomDecayStandalone PRIVATE
        src/standalone/Main.cpp
        src/standalone/MainComponent.cpp
        src/standalone/MainComponent.hpp
        src/ui/RT60Graph.cpp
        src/ui/RT60Graph.hpp
        src/core/DecayModel.cpp
        src/core/MeasurementEngine.cpp
        src/core/CorrectionEngine.cpp
        src/core/CorrectionEngine.hpp
        src/core/DecayUniformity.cpp
        src/core/DecayUniformity.hpp
    )
    target_include_directories(RT60RoomDecayStandalone PRIVATE src)
    target_link_libraries(RT60RoomDecayStandalone PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
    )
    target_compile_definitions(RT60RoomDecayStandalone PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )

    juce_add_plugin(RT60RoomDecayPlugin
        COMPANY_NAME "Room Decay Lab"
        BUNDLE_ID "com.roomdecaylab.rt60corrector"
        PLUGIN_MANUFACTURER_CODE RdLb
        PLUGIN_CODE Rt60
        FORMATS VST3
        PRODUCT_NAME "RT60 Room Decay Corrector"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
    )
    target_sources(RT60RoomDecayPlugin PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginProcessor.hpp
        src/plugin/PluginEditor.cpp
        src/plugin/PluginEditor.hpp
        src/ui/RT60Graph.cpp
        src/ui/RT60Graph.hpp
        src/core/DecayModel.cpp
        src/core/MeasurementEngine.cpp
        src/core/CorrectionEngine.cpp
        src/core/CorrectionEngine.hpp
        src/core/DecayUniformity.cpp
        src/core/DecayUniformity.hpp
    )
    target_include_directories(RT60RoomDecayPlugin PRIVATE src)
    target_link_libraries(RT60RoomDecayPlugin PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    )
    target_compile_definitions(RT60RoomDecayPlugin PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
endif()
