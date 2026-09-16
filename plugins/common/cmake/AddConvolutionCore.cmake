# Shared sources for the bundled-IR convolution reverb family (../convolution/).
#
# Deliberately NOT a static library, unlike AddHardwarePanel.cmake. That one is pure juce_gui_basics
# code with no plugin identity in it; this one includes an AudioProcessor subclass, and
# juce_audio_processors' headers read the JucePlugin_* definitions that juce_add_plugin sets per
# target. Compiling these sources once into a shared library and linking it into targets carrying
# different definitions is exactly how that goes wrong, so each target compiles them itself.
#
# Every path here resolves from CMAKE_CURRENT_LIST_DIR, never CMAKE_CURRENT_SOURCE_DIR. That is what
# lets a variant in a separate private repo include this file through its submodule checkout and
# have it find the sources next to itself rather than next to the including CMakeLists.txt. The same
# rule holds for FetchJUCE.cmake and AddHardwarePanel.cmake - don't break it.

set(WILDJAG_CONVOLUTION_DIR "${CMAKE_CURRENT_LIST_DIR}/../convolution")

# Everything a headless target needs: DSP, IR decoding/shaping, and the processor. No GUI.
set(WILDJAG_CONVOLUTION_CORE_SOURCES
    "${WILDJAG_CONVOLUTION_DIR}/IRShaper.cpp"
    "${WILDJAG_CONVOLUTION_DIR}/IRLibrary.cpp"
    "${WILDJAG_CONVOLUTION_DIR}/ConvolutionEngine.cpp"
    "${WILDJAG_CONVOLUTION_DIR}/IRLoadWorker.cpp"
    "${WILDJAG_CONVOLUTION_DIR}/ConvolutionProcessor.cpp"
)

# The editor half, including ConvolutionProcessor::createEditor(). A tests target links the core
# sources plus its own TestCreateEditorStub.cpp instead of these, and so never pulls in the
# LookAndFeel or the variant's BinaryData.
set(WILDJAG_CONVOLUTION_UI_SOURCES
    "${WILDJAG_CONVOLUTION_DIR}/IRWaveformDisplay.cpp"
    "${WILDJAG_CONVOLUTION_DIR}/ConvolutionEditor.cpp"
    "${WILDJAG_CONVOLUTION_DIR}/ConvolutionPluginEntry.cpp"
)

# Adds the headless half to `target`.
function(wildjag_add_convolution_core target)
    target_sources(${target} PRIVATE ${WILDJAG_CONVOLUTION_CORE_SOURCES})
    target_include_directories(${target} PRIVATE "${WILDJAG_CONVOLUTION_DIR}")
    target_link_libraries(${target} PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
    )
endfunction()

# Adds the editor half. Call wildjag_add_convolution_core() on the same target as well - this is the
# extra layer, not a superset.
function(wildjag_add_convolution_ui target)
    target_sources(${target} PRIVATE ${WILDJAG_CONVOLUTION_UI_SOURCES})
    target_link_libraries(${target} PRIVATE
        HardwarePanelLookAndFeel
        juce::juce_gui_basics
    )
endfunction()
