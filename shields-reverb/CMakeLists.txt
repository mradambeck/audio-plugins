cmake_minimum_required(VERSION 3.22)

project(Shields VERSION 0.5.0)
set(WILDJAG_RELEASE_CHANNEL "stable") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Shields
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.shields"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Shields needs microphone access to process live audio input in the Standalone app."
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Shld
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Shields - Reverb"
)

juce_add_binary_data(ShieldsBinaryData
    SOURCES
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Shields
    PRIVATE
        Source/ShieldsFDNEngine.cpp
        Source/PluginProcessor.cpp
        Source/ShieldsLookAndFeel.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(Shields
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Shields
    PRIVATE
        ShieldsBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner for the FDN core (ShieldsFDNEngine's stability-at-high-feedback and
# echo-density-buildup checks - a by-ear check alone can confirm "it swells" but not "it stays
# bounded" or "it swells before it decays, not the other way round"). Deliberately excludes
# plugin/JUCE-format machinery entirely, so it builds and runs fast on its own, matching every
# other plugin's <Name>Tests target in this catalog.
juce_add_console_app(ShieldsTests
    PRODUCT_NAME "ShieldsTests"
)

target_sources(ShieldsTests
    PRIVATE
        Source/ShieldsFDNEngine.cpp
        Source/Tests/ShieldsFDNEngineTests.cpp
        Source/Tests/TestRunner.cpp
)

target_link_libraries(ShieldsTests
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline impulse-response render harness (build order step 2) - drives the real
# ShieldsAudioProcessor with a single-sample impulse and writes the result to WAV, so
# ../common/tools/compare_wavs.py can score it against a captured reference IR in reference-irs/ without a
# DAW in the loop. Unlike ShieldsTests, this needs the full processor (and therefore
# juce_audio_utils, for AudioProcessor/AudioProcessorEditor/WAV I/O) rather than just the DSP core.
juce_add_console_app(ShieldsRenderIR
    PRODUCT_NAME "ShieldsRenderIR"
)

target_sources(ShieldsRenderIR
    PRIVATE
        Source/ShieldsFDNEngine.cpp
        Source/PluginProcessor.cpp
        Source/ShieldsLookAndFeel.cpp
        Source/PluginEditor.cpp
        Source/Tools/RenderIR.cpp
)

target_compile_definitions(ShieldsRenderIR
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="Shields"
        JucePlugin_VersionString="${PROJECT_VERSION}"
)

target_link_libraries(ShieldsRenderIR
    PRIVATE
        ShieldsBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
