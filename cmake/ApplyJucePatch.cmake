# Applies one of cmake/patches/*.patch to the JUCE source tree FetchContent just populated.
# Run as FetchContent's PATCH_COMMAND, so the working directory is the JUCE source root:
#   cmake -DPATCH_FILE=<abs path> -P cmake/ApplyJucePatch.cmake
#
# Idempotent, because FetchContent re-runs its patch step on later configures against the same
# source tree: a patch that applies forward is applied; one that already applies in reverse is
# left alone; in a git tree, an older revision of the same patch is replaced (its files reset to the
# pinned tag, then patched); anything else (a new JUCE tag moved the code) is a hard error naming the patch.
# --ignore-whitespace tolerates a checkout that rewrote JUCE's CRLF line endings.
# Mechanism and how to refresh a patch: docs/development/juce-patches.md.

if(NOT PATCH_FILE OR NOT EXISTS "${PATCH_FILE}")
    message(FATAL_ERROR "ApplyJucePatch: PATCH_FILE '${PATCH_FILE}' does not exist")
endif()

find_program(GIT_EXECUTABLE git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check --ignore-whitespace "${PATCH_FILE}"
    RESULT_VARIABLE forward_result
    OUTPUT_QUIET ERROR_QUIET)
if(forward_result EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${PATCH_FILE}"
        RESULT_VARIABLE apply_result)
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR "ApplyJucePatch: applying ${PATCH_FILE} failed")
    endif()
    message(STATUS "Applied JUCE patch ${PATCH_FILE}")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check --reverse --ignore-whitespace "${PATCH_FILE}"
    RESULT_VARIABLE reverse_result
    OUTPUT_QUIET ERROR_QUIET)
if(reverse_result EQUAL 0)
    message(STATUS "JUCE patch ${PATCH_FILE} is already applied")
    return()
endif()

# An edited patch over a tree that still carries its OLDER version (CI restores build/_deps by a key prefix, so a run
# can get a tree patched by the previous revision) neither applies nor reverses. FetchContent cloned JUCE with git, so
# put just this patch's files back to the pinned tag and apply again; the patches touch disjoint files, so no other
# patch is undone. A tree whose pristine files still don't take the patch remains a hard error below.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --is-inside-work-tree
    RESULT_VARIABLE git_tree_result
    OUTPUT_QUIET ERROR_QUIET)
if(git_tree_result EQUAL 0)
    file(STRINGS "${PATCH_FILE}" patched_lines REGEX "^\\+\\+\\+ b/")
    set(patched_files "")
    foreach(line IN LISTS patched_lines)
        string(REGEX REPLACE "^\\+\\+\\+ b/" "" path "${line}")
        list(APPEND patched_files "${path}")
    endforeach()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" checkout HEAD -- ${patched_files}
        RESULT_VARIABLE reset_result
        OUTPUT_QUIET ERROR_QUIET)
    if(reset_result EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${PATCH_FILE}"
            RESULT_VARIABLE reapply_result
            OUTPUT_QUIET ERROR_QUIET)
        if(reapply_result EQUAL 0)
            message(STATUS "Reset this patch's files to the pinned tag and re-applied JUCE patch ${PATCH_FILE}")
            return()
        endif()
    endif()
endif()

message(FATAL_ERROR
    "ApplyJucePatch: ${PATCH_FILE} neither applies to nor is already part of the JUCE tree in "
    "${CMAKE_CURRENT_SOURCE_DIR}. Regenerate it for the pinned JUCE tag "
    "(docs/development/juce-patches.md).")
