cmake_minimum_required(VERSION 3.22)

project(Corrosion VERSION 0.1.2)
set(WILDJAG_RELEASE_CHANNEL "stable") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)

juce_add_plugin(Corrosion
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.corrosion"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Corrosion needs microphone access to process live audio input in the Standalone app."
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Corr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Corrosion - Overdrive"
)

juce_add_binary_data(CorrosionBinaryData
    SOURCES
        ../common/Assets/WildJagLogo.svg
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

target_sources(Corrosion
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/CorrosionLookAndFeel.cpp
)

target_compile_definitions(Corrosion
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Corrosion
    PRIVATE
        CorrosionBinaryData
        HardwarePanelLookAndFeel
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner that drives the real CorrosionAudioProcessor directly (there's no
# separate testable DSP class the way Gradient has GradientDelayBuffer/GradientPitchShiftEngine -
# Corrosion's DSP lives in PluginProcessor.cpp itself). createEditor() lives in PluginEditor.cpp
# specifically so PluginProcessor.cpp has no GUI dependency and this target stays small and fast,
# excluding plugin/JUCE-format machinery and the editor/LookAndFeel/font code entirely.
juce_add_console_app(CorrosionTests
    PRODUCT_NAME "CorrosionTests"
)

target_sources(CorrosionTests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tests/CorrosionProcessorTests.cpp
        Source/Tests/TestRunner.cpp
)

target_compile_definitions(CorrosionTests
    PRIVATE
        JucePlugin_Name="Corrosion"
)

target_link_libraries(CorrosionTests
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
