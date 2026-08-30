cmake_minimum_required(VERSION 3.22)

project(Karplunk VERSION 0.3.2)
set(WILDJAG_RELEASE_CHANNEL "stable") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Karplunk
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.karplunk"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Karp
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Karplunk"
)

juce_add_binary_data(KarplunkBinaryData
    SOURCES
        ../common/Assets/WildJagLogo.svg
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Karplunk
    PRIVATE
        Source/KarplunkExcitation.cpp
        Source/KarplunkLoopFilter.cpp
        Source/PluginProcessor.cpp
        Source/KarplunkLookAndFeel.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(Karplunk
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Karplunk
    PRIVATE
        KarplunkBinaryData
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
# (parameter wiring, APVTS, smoothing, MIDI dispatch) - see KarplunkProcessorTests below for that.
juce_add_console_app(KarplunkTests
    PRODUCT_NAME "KarplunkTests"
)

target_sources(KarplunkTests
    PRIVATE
        Source/KarplunkExcitation.cpp
        Source/KarplunkLoopFilter.cpp
        Source/Tests/KarplunkExcitationTests.cpp
        Source/Tests/KarplunkLoopFilterTests.cpp
        Source/Tests/KarplunkWaveshaperTests.cpp
        Source/Tests/KarplunkRingModulatorTests.cpp
        Source/Tests/KarplunkStringLineTests.cpp
        Source/Tests/KarplunkVoiceTests.cpp
        Source/Tests/KarplunkVoiceAllocatorTests.cpp
        Source/Tests/KarplunkMonoNoteStackTests.cpp
        Source/Tests/TestRunner.cpp
)

target_link_libraries(KarplunkTests
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner that drives the real KarplunkAudioProcessor directly (APVTS parameters,
# smoothing, MIDI dispatch, processBlock) - the exact class the plugin ships, not the isolated
# Voice DSP class KarplunkTests exercises above. Mirrors alloy-bass's AlloyTests split: createEditor()
# lives in PluginEditor.cpp specifically so PluginProcessor.cpp has no GUI dependency and this
# target stays small and fast, excluding plugin/JUCE-format machinery and the editor/LookAndFeel/
# font code entirely.
juce_add_console_app(KarplunkProcessorTests
    PRODUCT_NAME "KarplunkProcessorTests"
)

target_sources(KarplunkProcessorTests
    PRIVATE
        Source/KarplunkExcitation.cpp
        Source/KarplunkLoopFilter.cpp
        Source/PluginProcessor.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tests/KarplunkProcessorTests.cpp
        Source/Tests/TestRunner.cpp
)

target_compile_definitions(KarplunkProcessorTests
    PRIVATE
        JucePlugin_Name="Karplunk"
)

target_link_libraries(KarplunkProcessorTests
    PRIVATE
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
