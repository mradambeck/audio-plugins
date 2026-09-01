cmake_minimum_required(VERSION 3.22)

project(Flux VERSION 0.1.5)
set(WILDJAG_RELEASE_CHANNEL "stable") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Flux
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.flux"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Flux needs microphone access to process live audio input in the Standalone app."
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Flux
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Flux - Phaser"
)

juce_add_binary_data(FluxBinaryData
    SOURCES
        ../common/Assets/WildJagLogo.svg
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Flux
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/FluxLookAndFeel.cpp
)

target_compile_definitions(Flux
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Flux
    PRIVATE
        FluxBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner that drives the real FluxAudioProcessor directly (there's no separate
# testable DSP class the way Gradient has GradientDelayBuffer/GradientPitchShiftEngine - Flux's
# DSP lives in PluginProcessor.cpp itself). createEditor() lives in PluginEditor.cpp specifically
# so PluginProcessor.cpp has no GUI dependency and this target stays small and fast, excluding
# plugin/JUCE-format machinery and the editor/LookAndFeel/font code entirely.
juce_add_console_app(FluxTests
    PRODUCT_NAME "FluxTests"
)

target_sources(FluxTests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tests/FluxProcessorTests.cpp
        Source/Tests/TestRunner.cpp
)

target_compile_definitions(FluxTests
    PRIVATE
        JucePlugin_Name="Flux"
)

target_link_libraries(FluxTests
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline render harness (mirrors caverns-delay's CavernsRenderIR / gradient-pitch's
# GradientRenderIR) - drives the real FluxAudioProcessor with a synthesized audio-in test signal
# and writes the result to WAV, so a pre/post-change pair can be diffed byte-for-byte or scored via
# ../common/tools/compare_wavs.py without a DAW in the loop. Reuses the same
# TestCreateEditorStub.cpp FluxTests already uses, so this stays free of the editor/LookAndFeel/
# BinaryData too.
juce_add_console_app(FluxRenderIR
    PRODUCT_NAME "FluxRenderIR"
)

target_sources(FluxRenderIR
    PRIVATE
        Source/PluginProcessor.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tools/RenderIR.cpp
)

target_compile_definitions(FluxRenderIR
    PRIVATE
        JucePlugin_Name="Flux"
        JucePlugin_VersionString="${PROJECT_VERSION}"
)

target_link_libraries(FluxRenderIR
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
