cmake_minimum_required(VERSION 3.22)

project(Strike VERSION 0.1.0)
set(WILDJAG_RELEASE_CHANNEL "stable") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Strike
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.strike"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Strk
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Strike"
)

juce_add_binary_data(StrikeBinaryData
    SOURCES
        ../common/Assets/WildJagLogo.svg
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Strike
    PRIVATE
        Source/StrikeExcitation.cpp
        Source/StrikeLoopFilter.cpp
        Source/PluginProcessor.cpp
        Source/StrikeLookAndFeel.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(Strike
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Strike
    PRIVATE
        StrikeBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner for the four standalone DSP-seam classes (Excitation, Loop Filter,
# Delay Tuning/StringLine, and the composed Voice) - deliberately excludes plugin/JUCE-format
# machinery entirely, so it builds and runs fast on its own (matches gradient-pitch's GradientTests
# pattern). This only ever exercises the isolated Voice class, not the real PluginProcessor
# (parameter wiring, APVTS, smoothing, MIDI dispatch) - see StrikeProcessorTests below for that.
juce_add_console_app(StrikeTests
    PRODUCT_NAME "StrikeTests"
)

target_sources(StrikeTests
    PRIVATE
        Source/StrikeExcitation.cpp
        Source/StrikeLoopFilter.cpp
        Source/Tests/StrikeExcitationTests.cpp
        Source/Tests/StrikeLoopFilterTests.cpp
        Source/Tests/StrikeWaveshaperTests.cpp
        Source/Tests/StrikeRingModulatorTests.cpp
        Source/Tests/StrikeStringLineTests.cpp
        Source/Tests/StrikeVoiceTests.cpp
        Source/Tests/StrikeVoiceAllocatorTests.cpp
        Source/Tests/StrikeMonoNoteStackTests.cpp
        Source/Tests/TestRunner.cpp
)

target_link_libraries(StrikeTests
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner that drives the real StrikeAudioProcessor directly (APVTS parameters,
# smoothing, MIDI dispatch, processBlock) - the exact class the plugin ships, not the isolated
# Voice DSP class StrikeTests exercises above. Mirrors alloy-bass's AlloyTests split: createEditor()
# lives in PluginEditor.cpp specifically so PluginProcessor.cpp has no GUI dependency and this
# target stays small and fast, excluding plugin/JUCE-format machinery and the editor/LookAndFeel/
# font code entirely.
juce_add_console_app(StrikeProcessorTests
    PRODUCT_NAME "StrikeProcessorTests"
)

target_sources(StrikeProcessorTests
    PRIVATE
        Source/StrikeExcitation.cpp
        Source/StrikeLoopFilter.cpp
        Source/PluginProcessor.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tests/StrikeProcessorTests.cpp
        Source/Tests/TestRunner.cpp
)

target_compile_definitions(StrikeProcessorTests
    PRIVATE
        JucePlugin_Name="Strike"
)

target_link_libraries(StrikeProcessorTests
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
