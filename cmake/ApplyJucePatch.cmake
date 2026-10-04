# Applies one of cmake/patches/*.patch to the JUCE source tree FetchContent just populated.
# Run as FetchContent's PATCH_COMMAND, so the working directory is the JUCE source root:
#   cmake -DPATCH_FILE=<abs path> -P cmake/ApplyJucePatch.cmake
#
# Idempotent, because FetchContent re-runs its patch step on later configures against the same
# source tree: a patch that applies forward is applied; one that already applies in reverse is
# left alone; anything else (a new JUCE tag moved the code) is a hard error naming the patch.
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

message(FATAL_ERROR
    "ApplyJucePatch: ${PATCH_FILE} neither applies to nor is already part of the JUCE tree in "
    "${CMAKE_CURRENT_SOURCE_DIR}. Regenerate it for the pinned JUCE tag "
    "(docs/development/juce-patches.md).")
