# Shared definition of the Tools/ measurement harnesses (ENABLE_AI_HARNESS), so each harness's
# CMakeLists.txt only names its own sources. Until FRO290 every harness listed ~60 app UI
# translation units by path and compiled its own copy of every JUCE module unity file, which made
# the tail of the Linux Debug+coverage build (three harnesses at once) run the runner out of memory.
#
# synth_add_harness(<target> <own sources...>)
#   Links the harness against the AppUI and Core static libraries (which already carry the app code
#   and GraphEditor, so a new .cpp only ever needs registering in cmake/AppUISources.cmake), and
#   stops it recompiling the JUCE module unity files: Core links the same modules and already
#   compiled them into libCore.a, which the harness links. Compile flags, includes and definitions
#   still arrive through the usual PUBLIC link to Core, so the headers see the same configuration.
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

    # JUCE attaches each module's unity file(s) to the module's INTERFACE_SOURCES, so every target
    # that links a module compiles its own copy. Mark them header-only in this harness's directory
    # (source file properties are directory-scoped, one harness per directory) so ninja skips them.
    get_target_property(core_libs Core LINK_LIBRARIES)
    foreach(lib IN LISTS core_libs)
        if(lib MATCHES "^juce::juce_")
            get_target_property(module_sources ${lib} INTERFACE_JUCE_MODULE_SOURCES)
            if(module_sources)
                set_source_files_properties(${module_sources} PROPERTIES HEADER_FILE_ONLY TRUE)
            endif()
        endif()
    endforeach()
endfunction()
