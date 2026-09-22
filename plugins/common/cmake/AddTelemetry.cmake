# Defines a small static library target, WildJagTelemetry, from the shared opt-out launch/preset
# telemetry client in common/Telemetry/. Include this from a plugin's CMakeLists.txt AFTER JUCE
# has been made available (see FetchJUCE.cmake), then:
#   target_link_libraries(<Plugin> PRIVATE WildJagTelemetry)
#
# Requires JUCE_USE_CURL=1 (or the platform's native HTTP backend) on whatever target links this,
# since TelemetryClient.cpp sends its events via juce::URL. Recompiled once per plugin's own CMake
# configure, same as HardwarePanelLookAndFeel (see AddHardwarePanel.cmake).

add_library(WildJagTelemetry STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../Telemetry/TelemetrySettings.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../Telemetry/TelemetryClient.cpp
)
target_include_directories(WildJagTelemetry PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../Telemetry)
target_link_libraries(WildJagTelemetry PUBLIC juce::juce_core juce::juce_data_structures juce::juce_audio_processors)
target_compile_features(WildJagTelemetry PUBLIC cxx_std_17)
