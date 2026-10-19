# The headless click audit: idle-to-active edges of the audio modules and sources, kept in its own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/AudioRendering/ModuleClickAuditTests.cpp
)
