# A JUCE source dir given through FETCHCONTENT_SOURCE_DIR_JUCE skips FetchContent's patch step, so it must
# already carry every cmake/patches/*.patch. Merging a new patch into a tree that cached such a path would
# otherwise build unpatched JUCE without a word. docs/development/juce-patches.md.
#
# Runs from CMakeLists.txt on every configure, or standalone:
#   cmake -DJUCE_SRC=<dir> -DPATCH_DIR=<dir> -P cmake/CheckJuceSourceDir.cmake
function(synth_check_juce_source_dir juce_src patch_dir)
    file(GLOB patches "${patch_dir}/*.patch")
    find_program(SYNTH_GIT_FOR_PATCH_CHECK git)
    if(NOT SYNTH_GIT_FOR_PATCH_CHECK)
        message(WARNING "No git found, so ${juce_src} was not checked for the JUCE patches")
        return()
    endif()
    foreach(patch IN LISTS patches)
        execute_process(
            COMMAND ${SYNTH_GIT_FOR_PATCH_CHECK} apply --check --reverse --ignore-whitespace ${patch}
            WORKING_DIRECTORY ${juce_src}
            RESULT_VARIABLE not_applied
            OUTPUT_QUIET ERROR_QUIET)
        if(not_applied)
            get_filename_component(patch_name ${patch} NAME)
            message(FATAL_ERROR
                "FETCHCONTENT_SOURCE_DIR_JUCE points at ${juce_src}, which lacks ${patch_name}, and a source "
                "dir skips the patch step. Clone that dir and run, in the clone, for each patch in "
                "cmake/patches: cmake -DPATCH_FILE=<patch> -P cmake/ApplyJucePatch.cmake. Then reconfigure "
                "with -DFETCHCONTENT_SOURCE_DIR_JUCE=<clone>, or -UFETCHCONTENT_SOURCE_DIR_JUCE to fetch JUCE.")
        endif()
    endforeach()
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
    synth_check_juce_source_dir("${JUCE_SRC}" "${PATCH_DIR}")
elseif(FETCHCONTENT_SOURCE_DIR_JUCE)
    synth_check_juce_source_dir("${FETCHCONTENT_SOURCE_DIR_JUCE}" "${CMAKE_CURRENT_LIST_DIR}/patches")
endif()
