# Defines a small static library target, HardwarePanelLookAndFeel, from the shared
# hardware-panel-ui base class in common/LookAndFeel/. Include this from a plugin's
# CMakeLists.txt AFTER JUCE has been made available (see FetchJUCE.cmake), then:
#   target_link_libraries(<Plugin> PRIVATE HardwarePanelLookAndFeel)
#
# Recompiled once per plugin's own CMake configure (each plugin is an independently
# configured project) -- same as every JUCE module. This just keeps the shared
# common/LookAndFeel/*.cpp file list out of all 6 plugin CMakeLists.txt files.

add_library(HardwarePanelLookAndFeel STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../LookAndFeel/HardwarePanelLookAndFeel.cpp
)
target_include_directories(HardwarePanelLookAndFeel PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../LookAndFeel)
target_link_libraries(HardwarePanelLookAndFeel PUBLIC juce::juce_gui_basics)
target_compile_features(HardwarePanelLookAndFeel PUBLIC cxx_std_17)
