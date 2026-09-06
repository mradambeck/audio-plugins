cmake_minimum_required(VERSION 3.22)

project(Concrete VERSION 0.1.0)
set(WILDJAG_RELEASE_CHANNEL "beta") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Concrete
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.concrete"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Cnct
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Concrete"
)

juce_add_binary_data(ConcreteBinaryData
    SOURCES
        ../common/Assets/WildJagLogo.svg
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Concrete
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ConcreteLookAndFeel.cpp
        Source/ConcreteVoice.cpp
        Source/ConcretePitchEngine.cpp
        Source/ConcreteCapturePass.cpp
        Source/ConcreteSampleIO.cpp
)

target_compile_definitions(Concrete
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Concrete
    PRIVATE
        ConcreteBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner for the framework-free-ish DSP/data seams (ConcreteSampleSet,
# ConcreteBusRouter, ConcreteVoiceAllocator, ConcreteVoice, ConcreteSampleIO - see
# concrete-sampler-plugin-plan.md's "Ground rules" for the full eventual seam list) - matches
# Strike's StrikeTests split. ConcreteSampleIO needs juce_audio_formats (real file I/O against
# temp WAVs, FLAC encode/decode) even though it's not framework-free in Strike's stricter sense;
# it stays out of ConcreteProcessorTests/ConcreteRenderIR's "no GUI/BinaryData" scope concern since
# none of that machinery pulls in the editor either way.
juce_add_console_app(ConcreteTests
    PRODUCT_NAME "ConcreteTests"
)

target_sources(ConcreteTests
    PRIVATE
        Source/ConcreteVoice.cpp
        Source/ConcretePitchEngine.cpp
        Source/ConcreteCapturePass.cpp
        Source/ConcreteSampleIO.cpp
        Source/Tests/ConcreteSampleSetTests.cpp
        Source/Tests/ConcreteBusRouterTests.cpp
        Source/Tests/ConcreteVoiceAllocatorTests.cpp
        Source/Tests/ConcreteVoiceTests.cpp
        Source/Tests/ConcretePitchEngineTests.cpp
        Source/Tests/ConcreteQuantizerTests.cpp
        Source/Tests/ConcreteCapturePassTests.cpp
        Source/Tests/ConcreteSampleIOTests.cpp
        Source/Tests/TestRunner.cpp
)

target_link_libraries(ConcreteTests
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_data_structures
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner that drives the real ConcreteAudioProcessor directly (APVTS parameters,
# state, MIDI dispatch, processBlock) - the exact class the plugin ships, not a re-implementation.
# createEditor() lives in PluginEditor.cpp specifically so PluginProcessor.cpp has no GUI
# dependency and this target stays free of the editor/LookAndFeel/BinaryData, matching Strike's
# StrikeProcessorTests split. Needs juce_audio_formats now that PluginProcessor.cpp loads samples.
juce_add_console_app(ConcreteProcessorTests
    PRODUCT_NAME "ConcreteProcessorTests"
)

target_sources(ConcreteProcessorTests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/ConcreteVoice.cpp
        Source/ConcretePitchEngine.cpp
        Source/ConcreteCapturePass.cpp
        Source/ConcreteSampleIO.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tests/ConcreteProcessorTests.cpp
        Source/Tests/TestRunner.cpp
)

target_compile_definitions(ConcreteProcessorTests
    PRIVATE
        JucePlugin_Name="Concrete"
)

target_link_libraries(ConcreteProcessorTests
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline render harness (mirrors alloy-bass's AlloyRenderIR - a MIDI-driven synth, not an
# audio-in-driven effect). --sample/--note/--velocity/--sequence let it drive the real sample
# zone/voice engine (Phase 1); Phase 0's --out/--seconds/--sampleRate silence render still works
# with no --sample given. Reuses Source/Tests/TestCreateEditorStub.cpp so this stays free of the
# editor/LookAndFeel/BinaryData, matching AlloyRenderIR.
juce_add_console_app(ConcreteRenderIR
    PRODUCT_NAME "ConcreteRenderIR"
)

target_sources(ConcreteRenderIR
    PRIVATE
        Source/PluginProcessor.cpp
        Source/ConcreteVoice.cpp
        Source/ConcretePitchEngine.cpp
        Source/ConcreteCapturePass.cpp
        Source/ConcreteSampleIO.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tools/RenderIR.cpp
)

target_compile_definitions(ConcreteRenderIR
    PRIVATE
        JucePlugin_Name="Concrete"
        JucePlugin_VersionString="${PROJECT_VERSION}"
)

target_link_libraries(ConcreteRenderIR
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
