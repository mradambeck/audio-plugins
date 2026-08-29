cmake_minimum_required(VERSION 3.22)

project(Intruder VERSION 0.2.0)
set(WILDJAG_RELEASE_CHANNEL "stable") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Intruder
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.intruder"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Intruder needs microphone access to process live audio input in the Standalone app."
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Intr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Intruder - Reverb"
)

juce_add_binary_data(IntruderBinaryData
    SOURCES
        ../common/Assets/WildJagLogo.svg
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Intruder
    PRIVATE
        Source/IntruderFDNEngine.cpp
        Source/IntruderParameterMap.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/IntruderLookAndFeel.cpp
)

target_compile_definitions(Intruder
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Intruder
    PRIVATE
        IntruderBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner for the DSP core (IntruderFDNEngine) - an objective correctness check
# beyond this catalog's usual by-ear verification, matching every other plugin's Tests target.
# Deliberately excludes plugin/JUCE-format machinery entirely, so it builds and runs fast on its
# own without ever touching COPY_PLUGIN_AFTER_BUILD.
juce_add_console_app(IntruderTests
    PRODUCT_NAME "IntruderTests"
)

target_sources(IntruderTests
    PRIVATE
        Source/IntruderFDNEngine.cpp
        Source/IntruderParameterMap.cpp
        Source/Tests/IntruderFDNEngineTests.cpp
        Source/Tests/IntruderParameterMapTests.cpp
        Source/Tests/TestRunner.cpp
)

target_link_libraries(IntruderTests
    PRIVATE
        juce::juce_core
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline render harness (mirrors shields-reverb's ShieldsRenderIR / gradient-pitch's
# GradientRenderIR) - drives the real IntruderAudioProcessor and writes the result to WAV, for
# Phase 6 validation against the reference IRs in ir-captures/ via analysis/validate.py.
juce_add_console_app(IntruderRenderIR
    PRODUCT_NAME "IntruderRenderIR"
)

target_sources(IntruderRenderIR
    PRIVATE
        Source/IntruderFDNEngine.cpp
        Source/IntruderParameterMap.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/IntruderLookAndFeel.cpp
        Source/Tools/RenderIR.cpp
)

target_compile_definitions(IntruderRenderIR
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="Intruder"
        JucePlugin_VersionString="${PROJECT_VERSION}"
)

target_link_libraries(IntruderRenderIR
    PRIVATE
        IntruderBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
