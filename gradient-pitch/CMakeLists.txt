cmake_minimum_required(VERSION 3.22)

project(Gradient VERSION 0.1.3)
set(WILDJAG_RELEASE_CHANNEL "stable") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Gradient
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.gradient"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Gradient needs microphone access to process live audio input in the Standalone app."
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Grad
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Gradient - Pitch Delay"
)

juce_add_binary_data(GradientBinaryData
    SOURCES
        ../common/Assets/WildJagLogo.svg
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Gradient
    PRIVATE
        Source/GradientDelayBuffer.cpp
        Source/GradientPitchShiftEngine.cpp
        Source/PluginProcessor.cpp
        Source/GradientLookAndFeel.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(Gradient
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Gradient
    PRIVATE
        GradientBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner for DSP classes that warrant an objective correctness check beyond this
# catalog's usual by-ear verification (GradientDelayBuffer's interpolation/wrap math, and
# GradientPitchShiftEngine's ramp-rate-to-pitch-ratio accuracy - a by-ear-only check can confirm
# "shifted in the right direction" but not "shifted by exactly the right amount" without a tuner).
# Deliberately excludes plugin/JUCE-format machinery entirely, so it builds and runs fast on its own.
juce_add_console_app(GradientTests
    PRODUCT_NAME "GradientTests"
)

target_sources(GradientTests
    PRIVATE
        Source/GradientDelayBuffer.cpp
        Source/GradientPitchShiftEngine.cpp
        Source/Tests/GradientDelayBufferTests.cpp
        Source/Tests/GradientPitchShiftEngineTests.cpp
        Source/Tests/GradientFeedbackTests.cpp
        Source/Tests/GradientDriftTests.cpp
        Source/Tests/TestRunner.cpp
)

target_link_libraries(GradientTests
    PRIVATE
        juce::juce_core
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline render harness (mirrors shields-reverb's ShieldsRenderIR) - drives the real
# GradientAudioProcessor with a synthesized test signal and writes the result to WAV, so a
# pre/post-change pair can be diffed byte-for-byte or scored via ../common/tools/compare_wavs.py
# without a DAW in the loop. Unlike GradientTests, this needs the full processor (and therefore
# juce_audio_utils, for AudioProcessor/AudioProcessorEditor/WAV I/O) rather than just the DSP core.
juce_add_console_app(GradientRenderIR
    PRODUCT_NAME "GradientRenderIR"
)

target_sources(GradientRenderIR
    PRIVATE
        Source/GradientDelayBuffer.cpp
        Source/GradientPitchShiftEngine.cpp
        Source/PluginProcessor.cpp
        Source/GradientLookAndFeel.cpp
        Source/PluginEditor.cpp
        Source/Tools/RenderIR.cpp
)

target_compile_definitions(GradientRenderIR
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="Gradient"
        JucePlugin_VersionString="${PROJECT_VERSION}"
)

target_link_libraries(GradientRenderIR
    PRIVATE
        GradientBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
