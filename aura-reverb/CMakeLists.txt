cmake_minimum_required(VERSION 3.22)

project(Aura VERSION 0.1.0)
set(WILDJAG_RELEASE_CHANNEL "beta") # stable | beta -- see AGENTS.md > Versioning. Beta: the real
    # hardware-panel UI (juce-hardware-panel-ui skill, mockup-approved and screenshot-verified
    # against aura-reverb/mockups/aura-mockup-v1.html) is in place and Low Cut is wired, but no
    # installer exists yet (wildjag-plugin-installer skill) - not a manual channel bump on its own.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Aura
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.aura"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Aura needs microphone access to process live audio input in the Standalone app."
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Aura
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Aura - Reverb"
)

juce_add_binary_data(AuraBinaryData
    SOURCES
        ../common/Assets/WildJagLogo.svg
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Aura
    PRIVATE
        Source/AuraFDNEngine.cpp
        Source/AuraParameterMap.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AuraLookAndFeel.cpp
)

target_compile_definitions(Aura
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Aura
    PRIVATE
        AuraBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner for the DSP core (AuraFDNEngine) and the parameter mapping layer
# (AuraParameterMap) - an objective correctness check beyond by-ear verification, matching every
# other plugin's Tests target. Deliberately excludes plugin/JUCE-format machinery entirely, so it
# builds and runs fast on its own without ever touching COPY_PLUGIN_AFTER_BUILD.
juce_add_console_app(AuraTests
    PRODUCT_NAME "AuraTests"
)

target_sources(AuraTests
    PRIVATE
        Source/AuraFDNEngine.cpp
        Source/AuraParameterMap.cpp
        Source/Tests/AuraFDNEngineTests.cpp
        Source/Tests/AuraParameterMapTests.cpp
        Source/Tests/TestRunner.cpp
)

target_link_libraries(AuraTests
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline render harness (mirrors shields-reverb's ShieldsRenderIR / intruder-gated-reverb's
# IntruderRenderIR) - drives the real AuraAudioProcessor and writes the result to WAV, for Phase D
# validation against the reference captures in ml-toolkit/effects/ambience/captures/ via a
# validate.py script (not yet written - see the plan).
juce_add_console_app(AuraRenderIR
    PRODUCT_NAME "AuraRenderIR"
)

target_sources(AuraRenderIR
    PRIVATE
        Source/AuraFDNEngine.cpp
        Source/AuraParameterMap.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AuraLookAndFeel.cpp
        Source/Tools/RenderIR.cpp
)

target_compile_definitions(AuraRenderIR
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="Aura"
        JucePlugin_VersionString="${PROJECT_VERSION}"
)

target_link_libraries(AuraRenderIR
    PRIVATE
        AuraBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Calibration-only probe (not part of the shipped plugin) - drives AuraFDNEngine directly at raw
# decayGain/dampingWeight, bypassing AuraParameterMap's Time/High curves entirely, so
# AuraDecayGainData.h's binary-search calibration has something to sweep. See
# Source/Tools/CalibrateProbe.cpp's own header comment. No BinaryData/LookAndFeel dependency -
# engine-only, unlike AuraRenderIR.
juce_add_console_app(AuraCalibrateProbe
    PRODUCT_NAME "AuraCalibrateProbe"
)

target_sources(AuraCalibrateProbe
    PRIVATE
        Source/AuraFDNEngine.cpp
        Source/Tools/CalibrateProbe.cpp
)

target_link_libraries(AuraCalibrateProbe
    PRIVATE
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
