cmake_minimum_required(VERSION 3.22)

# ConvBase is a DEVELOPMENT HARNESS, not a shipped product. It exists so the shared convolution
# engine in ../common/convolution/ has a real plugin around it to be tested and auditioned in, and
# so CI has something to build. Its impulse responses are synthetic and generated (tools/
# make_test_irs.py), deliberately not captured audio - the branded, IR-bundled variants live in a
# separate private repo and consume this repo as a submodule.
#
# Consequently it is registered in build-and-test.yml and scripts/, and deliberately NOT in
# release.yml, installers/, sync-site-versions.yml, the site, or the download counter. See the
# README here before adding it to any of those.
project(ConvBase VERSION 0.1.0)
set(WILDJAG_RELEASE_CHANNEL "beta") # stable | beta -- see AGENTS.md > Versioning

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/FetchJUCE.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddHardwarePanel.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/cmake/AddConvolutionCore.cmake)

juce_add_plugin(ConvBase
    COMPANY_NAME "Wild Jag"
    BUNDLE_ID "com.wildjag.convbase"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ConvBase needs microphone access to process live audio input in the Standalone app."
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE WJag
    PLUGIN_CODE CvBs
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "ConvBase - Convolution"
)

# The IR blobs are binary data like any other asset. A real variant's list is longer and its files
# are captures; the mechanism is identical, which is the point of the harness.
juce_add_binary_data(ConvBaseBinaryData
    SOURCES
        irs/dirac-48k.flac
        irs/room-stereo-48k.flac
        irs/hall-stereo-44k.flac
        irs/plate-mono-96k.flac
        ../common/Assets/Oxanium-Bold.ttf
        ../common/Assets/Oswald-SemiBold.ttf
)

# Everything a variant owns: an IR table and a set of accent colours. There is no per-variant
# processor, editor, LookAndFeel or plugin entry point - all of those are shared and take the
# variant as input (see ../common/convolution/ConvolutionPluginEntry.cpp).
target_sources(ConvBase
    PRIVATE
        Source/VariantConfig.cpp
        Source/VariantTheme.cpp
)

wildjag_add_convolution_core(ConvBase)
wildjag_add_convolution_ui(ConvBase)

target_compile_definitions(ConvBase
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(ConvBase
    PRIVATE
        ConvBaseBinaryData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless UnitTest runner for the shared engine: IR shaping, sample-rate conversion, reported
# latency, and the two discontinuity checks (bypass and IR swap) that no compile can prove. Unlike
# every other plugin's Tests target this one does link BinaryData - the embedded IRs are the thing
# under test, not an asset the tests happen to drag in. It still excludes the editor entirely via
# Source/Tests/TestCreateEditorStub.cpp.
juce_add_console_app(ConvBaseTests
    PRODUCT_NAME "ConvBaseTests"
)

target_sources(ConvBaseTests
    PRIVATE
        Source/VariantConfig.cpp
        Source/Tests/IRShaperTests.cpp
        Source/Tests/IRLibraryTests.cpp
        Source/Tests/ConvolutionEngineTests.cpp
        Source/Tests/ConvolutionProcessorTests.cpp
        Source/Tests/TestCreateEditorStub.cpp
        Source/Tests/TestRunner.cpp
)

wildjag_add_convolution_core(ConvBaseTests)

target_compile_definitions(ConvBaseTests
    PRIVATE
        JucePlugin_Name="ConvBase"
)

target_link_libraries(ConvBaseTests
    PRIVATE
        ConvBaseBinaryData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline render harness (mirrors shields-reverb's ShieldsRenderIR / aura-reverb's AuraRenderIR) -
# drives the real ConvolutionProcessor and writes 32-bit float WAV for analysis/validate.py to
# measure. Unlike ConvBaseTests it needs the editor, because it links the full plugin-side sources;
# that is also what makes it a useful smoke test that the editor still links.
juce_add_console_app(ConvBaseRenderIR
    PRODUCT_NAME "ConvBaseRenderIR"
)

target_sources(ConvBaseRenderIR
    PRIVATE
        Source/VariantConfig.cpp
        Source/VariantTheme.cpp
        Source/Tools/RenderIR.cpp
)

wildjag_add_convolution_core(ConvBaseRenderIR)
wildjag_add_convolution_ui(ConvBaseRenderIR)

target_compile_definitions(ConvBaseRenderIR
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="ConvBase"
        JucePlugin_VersionString="${PROJECT_VERSION}"
)

target_link_libraries(ConvBaseRenderIR
    PRIVATE
        ConvBaseBinaryData
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless editor renderer - paints the real editor into a PNG with no window, no audio device and
# no host. The juce-hardware-panel-ui skill's verification step normally screenshots the Standalone,
# but macOS re-prompts for microphone access every time the bundle's signature changes (i.e. every
# rebuild), which makes the fix-and-look-again loop manual. This makes it deterministic, and drops
# the window chrome and the Standalone's own banner so the output diffs against the mockup directly.
juce_add_console_app(ConvBaseRenderUI
    PRODUCT_NAME "ConvBaseRenderUI"
)

target_sources(ConvBaseRenderUI
    PRIVATE
        Source/VariantConfig.cpp
        Source/VariantTheme.cpp
        Source/Tools/RenderUI.cpp
)

wildjag_add_convolution_core(ConvBaseRenderUI)
wildjag_add_convolution_ui(ConvBaseRenderUI)

target_compile_definitions(ConvBaseRenderUI
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="ConvBase"
        JucePlugin_VersionString="${PROJECT_VERSION}"
)

target_link_libraries(ConvBaseRenderUI
    PRIVATE
        ConvBaseBinaryData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
