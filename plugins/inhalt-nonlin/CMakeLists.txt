cmake_minimum_required(VERSION 3.22)

# Inhalt - AMS RMX16 "NonLin" recreation, built by convolving a synthesized IR (see
# Source/InhaltIRSynth.h and README.md) rather than running a live FDN. Reuses
# plugins/common/convolution/ConvolutionEngine.{h,cpp} directly (pre-delay, low cut, dry/wet, a
# ramped bypass), but NOT ConvolutionProcessor/ConvolutionEditor/IRLoadWorker/ConvolutionVariant -
# those are shaped around "pick a bundled IR from a table", which doesn't fit continuously-knobbed
# synthesis. See README.md before reaching for wildjag_add_convolution_core()/_ui() here.
project(Inhalt VERSION 0.1.0)
set(WILDJAG_RELEASE_CHANNEL "beta") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)

set(INHALT_CONVOLUTION_ENGINE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../common/convolution/ConvolutionEngine.cpp)

juce_add_plugin(Inhalt
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.inhalt"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Inhalt needs microphone access to process live audio input in the Standalone app."
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE Inhl
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Inhalt - Reverb"
)

target_sources(Inhalt PRIVATE
    Source/InhaltIRSynth.cpp
    Source/InhaltIRWorker.cpp
    Source/InhaltParameterMap.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${INHALT_CONVOLUTION_ENGINE_SOURCE}
)

target_include_directories(Inhalt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common/convolution)

target_compile_definitions(Inhalt PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Inhalt
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# Headless UnitTest runner - excludes the editor entirely via Source/Tests/TestCreateEditorStub.cpp,
# same convention as every other plugin's Tests target.
juce_add_console_app(InhaltTests
    PRODUCT_NAME "InhaltTests"
)

target_sources(InhaltTests PRIVATE
    Source/InhaltIRSynth.cpp
    Source/InhaltIRWorker.cpp
    Source/InhaltParameterMap.cpp
    Source/PluginProcessor.cpp
    Source/Tests/TestCreateEditorStub.cpp
    Source/Tests/TestRunner.cpp
    Source/Tests/InhaltIRSynthTests.cpp
    Source/Tests/InhaltParameterMapTests.cpp
    Source/Tests/InhaltProcessorTests.cpp
    ${INHALT_CONVOLUTION_ENGINE_SOURCE}
)

target_include_directories(InhaltTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common/convolution)

target_compile_definitions(InhaltTests PRIVATE
    JucePlugin_Name="Inhalt")

target_link_libraries(InhaltTests
    PRIVATE juce::juce_audio_processors juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# Offline render harness (mirrors shields-reverb's ShieldsRenderIR / aura-reverb's AuraRenderIR) -
# drives the real processor and writes 32-bit float stereo WAV for analysis/validate.py to measure.
# Must be stereo (not mono) - decorrelation is a validated property, and a mono render would make
# half of Phase 6's verification impossible.
juce_add_console_app(InhaltRenderIR
    PRODUCT_NAME "InhaltRenderIR"
)

target_sources(InhaltRenderIR PRIVATE
    Source/InhaltIRSynth.cpp
    Source/InhaltIRWorker.cpp
    Source/InhaltParameterMap.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Tools/RenderIR.cpp
    ${INHALT_CONVOLUTION_ENGINE_SOURCE}
)

target_include_directories(InhaltRenderIR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common/convolution)

target_compile_definitions(InhaltRenderIR PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="Inhalt"
    JucePlugin_VersionString="${PROJECT_VERSION}")

target_link_libraries(InhaltRenderIR
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_gui_extra
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
