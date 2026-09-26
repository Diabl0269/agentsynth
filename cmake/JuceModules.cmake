# One compiled copy of the JUCE modules for the targets that can share it (FRO306).
#
# JUCE attaches each module's unity file(s) to the module target's INTERFACE_SOURCES, so every
# target that links a juce::juce_* module, directly or through a PUBLIC link, compiles its own copy
# of all of them: the heaviest translation units in the tree. Before this, Core, AppUI, AgentSynth,
# the plugin and Tests each compiled one.
#
# JuceModules links the modules PRIVATE, so it alone compiles them, and hands consumers only the
# modules' include directories and compile definitions. Core links it PUBLIC, so AppUI, Tests and
# the Tools/ harnesses see the same JUCE configuration without compiling anything, and the linker
# takes the module code from libJuceModules.a.
#
# Two consumers still compile their own copy, on purpose, because JUCE's module code depends on
# per-target definitions:
#   - AgentSynth: JUCE_STANDALONE_APPLICATION=1 (juce_add_gui_app), which changes how the macOS
#     message loop and app delegate start up.
#   - AgentSynthPlugin: juce_add_plugin's JucePlugin_* definitions.
# Both link the modules PRIVATE themselves; their own objects come first on their link lines, so
# the archived copy here is never pulled into them.
#
# JUCE_MODAL_LOOPS_PERMITTED=1 is set PRIVATE: Tests needs MessageManager::runDispatchLoopUntil,
# which the juce_events unity file only compiles when it is set. It adds functions and changes no
# class layout, and it does not reach any consumer's own sources.

set(SYNTH_JUCE_MODULES
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_animation
    juce::juce_dsp
)

add_library(JuceModules STATIC)
target_link_libraries(JuceModules PRIVATE ${SYNTH_JUCE_MODULES})
target_compile_definitions(JuceModules PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)

# JUCE switches the module code itself is compiled with, PUBLIC so every consumer's headers agree.
# No embedded web browser; host third-party VST3 (and AU on macOS) plugins with JUCE's built-in
# hosting (TL7-1/TL7-2, see the comment beside Core in the root CMakeLists.txt).
target_compile_definitions(JuceModules PUBLIC JUCE_WEB_BROWSER=0 JUCE_PLUGINHOST_VST3=1)
if(APPLE)
    target_compile_definitions(JuceModules PUBLIC JUCE_PLUGINHOST_AU=1)
endif()

# juce_gui_basics/juce_gui_extra need GTK's headers on Linux (as Core does for its own sources).
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    target_include_directories(JuceModules PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_libraries(JuceModules PRIVATE ${GTK3_LIBRARIES})
endif()

# Forward each module's usage requirements except its sources. Taken from the module targets, not
# from JuceModules itself, so the PRIVATE definition above stays private.
foreach(module IN LISTS SYNTH_JUCE_MODULES)
    target_include_directories(JuceModules INTERFACE $<TARGET_PROPERTY:${module},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(JuceModules INTERFACE $<TARGET_PROPERTY:${module},INTERFACE_COMPILE_DEFINITIONS>)
    target_compile_options(JuceModules INTERFACE $<TARGET_PROPERTY:${module},INTERFACE_COMPILE_OPTIONS>)
endforeach()

# Linked into the VST3 .so on Linux, like Core/AppUI/Assets (see POSITION_INDEPENDENT_CODE there).
set_target_properties(JuceModules PROPERTIES POSITION_INDEPENDENT_CODE ON)
