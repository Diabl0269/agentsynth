# The hosted plugin tests kept out of Tests/CMakeLists.txt so it stays under the file-size cap: the Hosted Plugin node
# (loading, Dual I/O, lanes, latency, modulation slots), its card and the knob picker (docs/control/plugin-card-layout.md).
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin/HostedPluginTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin/HostedPluginDualIOTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin/HostedPluginTitleTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin/HostedPluginEditorWindowTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin/HostedPluginLaneTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin/HostedPluginModSlotTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Plugin/HostedPluginLatencyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModuleComponent/HostedPluginCardTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModuleComponent/HostedPluginCardModTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModuleComponent/HostedPluginCardMidiLearnTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/PluginKnobPicker/PluginKnobPickerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/PluginKnobPicker/PluginKnobPickerTouchFallbackTests.cpp
)
