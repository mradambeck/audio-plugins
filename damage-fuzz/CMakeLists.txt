cmake_minimum_required(VERSION 3.22)

project(Damage VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Damage
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.damage"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Damage needs microphone access to process live audio input in the Standalone app."
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Dmge
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Damage"
)

juce_add_binary_data(DamageBinaryData
    SOURCES
        Source/Assets/Rajdhani-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Damage
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DamageLookAndFeel.cpp
)

target_compile_definitions(Damage
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Damage
    PRIVATE
        DamageBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner that drives the real DamageAudioProcessor directly (there's no
# separate testable DSP class the way Gradient has GradientDelayBuffer/GradientPitchShiftEngine -
# Damage's DSP lives in PluginProcessor.cpp itself). createEditor() lives in PluginEditor.cpp
# specifically so PluginProcessor.cpp has no GUI dependency and this target stays small and fast,
# excluding plugin/JUCE-format machinery and the editor/LookAndFeel/font code entirely.
juce_add_console_app(DamageTests
    PRODUCT_NAME "DamageTests"
)

target_sources(DamageTests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tests/DamageProcessorTests.cpp
        Source/Tests/TestRunner.cpp
)

target_compile_definitions(DamageTests
    PRIVATE
        JucePlugin_Name="Damage"
)

target_link_libraries(DamageTests
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
