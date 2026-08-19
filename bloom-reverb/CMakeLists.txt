cmake_minimum_required(VERSION 3.22)

project(Bloom VERSION 0.1.0)
set(WILDJAG_RELEASE_CHANNEL "stable") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Bloom
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.bloom"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Bloom needs microphone access to process live audio input in the Standalone app."
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Blom
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Bloom"
)

juce_add_binary_data(BloomBinaryData
    SOURCES
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Bloom
    PRIVATE
        Source/BloomFDNEngine.cpp
        Source/PluginProcessor.cpp
        Source/BloomLookAndFeel.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(Bloom
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Bloom
    PRIVATE
        BloomBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner for the FDN core (BloomFDNEngine's stability-at-high-feedback and
# echo-density-buildup checks - a by-ear check alone can confirm "it swells" but not "it stays
# bounded" or "it swells before it decays, not the other way round"). Deliberately excludes
# plugin/JUCE-format machinery entirely, so it builds and runs fast on its own, matching every
# other plugin's <Name>Tests target in this catalog.
juce_add_console_app(BloomTests
    PRODUCT_NAME "BloomTests"
)

target_sources(BloomTests
    PRIVATE
        Source/BloomFDNEngine.cpp
        Source/Tests/BloomFDNEngineTests.cpp
        Source/Tests/TestRunner.cpp
)

target_link_libraries(BloomTests
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline impulse-response render harness (build order step 2) - drives the real
# BloomAudioProcessor with a single-sample impulse and writes the result to WAV, so
# tools/compare_irs.py can score it against a captured reference IR in reference-irs/ without a
# DAW in the loop. Unlike BloomTests, this needs the full processor (and therefore
# juce_audio_utils, for AudioProcessor/AudioProcessorEditor/WAV I/O) rather than just the DSP core.
juce_add_console_app(BloomRenderIR
    PRODUCT_NAME "BloomRenderIR"
)

target_sources(BloomRenderIR
    PRIVATE
        Source/BloomFDNEngine.cpp
        Source/PluginProcessor.cpp
        Source/BloomLookAndFeel.cpp
        Source/PluginEditor.cpp
        Source/Tools/RenderIR.cpp
)

target_compile_definitions(BloomRenderIR
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="Bloom"
        JucePlugin_VersionString="0.1.0"
)

target_link_libraries(BloomRenderIR
    PRIVATE
        BloomBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
