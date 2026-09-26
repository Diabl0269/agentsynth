# Shared definition of the Tools/ measurement harnesses (ENABLE_AI_HARNESS), so each harness's
# CMakeLists.txt only names its own sources. Until FRO290 every harness listed ~60 app UI
# translation units by path and compiled its own copy of every JUCE module unity file, which made
# the tail of the Linux Debug+coverage build (three harnesses at once) run the runner out of memory.
#
# synth_add_harness(<target> <own sources...>)
#   Links the harness against the AppUI and Core static libraries (which already carry the app code
#   and GraphEditor, so a new .cpp only ever needs registering in cmake/AppUISources.cmake). JUCE's
#   includes and definitions arrive through Core's PUBLIC link to JuceModules, and the module code
#   from libJuceModules.a (cmake/JuceModules.cmake), so the harness compiles no JUCE unity files.
function(synth_add_harness target)
    add_executable(${target} ${ARGN})

    # Core's AppUndoManager references GraphEditor, which lives in AppUI, and the harness's own
    # Main.cpp never touches AppUI. GNU ld and lld scan archives once, left to right, so with plain
    # `AppUI Core` AppUI would be skipped as unreferenced and GraphEditor's vtable left undefined by
    # the time Core is read. RESCAN makes the linker loop over both until nothing new resolves.
    # macOS's ld64 resolves archives iteratively already and CMake rejects RESCAN there.
    if(UNIX AND NOT APPLE AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
        target_link_libraries(${target} PRIVATE "$<LINK_GROUP:RESCAN,AppUI,Core>")
    else()
        target_link_libraries(${target} PRIVATE AppUI Core)
    endif()

    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/Source)

endfunction()
