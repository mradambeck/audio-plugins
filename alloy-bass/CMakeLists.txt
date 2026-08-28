cmake_minimum_required(VERSION 3.22)

project(Alloy VERSION 0.1.3)
set(WILDJAG_RELEASE_CHANNEL "stable") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Alloy
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.alloy"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Aloy
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Alloy"
)

juce_add_binary_data(AlloyBinaryData
    SOURCES
        ../common/Assets/WildJagLogo.svg
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Alloy
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AlloyLookAndFeel.cpp
)

target_compile_definitions(Alloy
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Alloy
    PRIVATE
        AlloyBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner that drives the real AlloyAudioProcessor directly (there's no separate
# testable DSP class the way Gradient has GradientDelayBuffer/GradientPitchShiftEngine - Alloy's
# DSP lives in PluginProcessor.cpp itself). createEditor() lives in PluginEditor.cpp specifically
# so PluginProcessor.cpp has no GUI dependency and this target stays small and fast, excluding
# plugin/JUCE-format machinery and the editor/LookAndFeel/font code entirely.
juce_add_console_app(AlloyTests
    PRODUCT_NAME "AlloyTests"
)

target_sources(AlloyTests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tests/AlloyProcessorTests.cpp
        Source/Tests/TestRunner.cpp
)

target_compile_definitions(AlloyTests
    PRIVATE
        JucePlugin_Name="Alloy"
)

target_link_libraries(AlloyTests
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline render harness (mirrors gradient-pitch's GradientRenderIR / shields-reverb's
# ShieldsRenderIR) - drives the real AlloyAudioProcessor with a scripted MIDI note sequence and
# writes the result to WAV, so a pre/post-change pair can be diffed byte-for-byte or scored via
# ../common/tools/compare_wavs.py without a DAW in the loop. Reuses the same
# TestCreateEditorStub.cpp AlloyTests already uses, so - unlike Gradient/Shields' RenderIR tools,
# which had no such stub available - this stays free of the editor/LookAndFeel/BinaryData too.
juce_add_console_app(AlloyRenderIR
    PRODUCT_NAME "AlloyRenderIR"
)

target_sources(AlloyRenderIR
    PRIVATE
        Source/PluginProcessor.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tools/RenderIR.cpp
)

target_compile_definitions(AlloyRenderIR
    PRIVATE
        JucePlugin_Name="Alloy"
        JucePlugin_VersionString="${PROJECT_VERSION}"
)

target_link_libraries(AlloyRenderIR
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
