# Writes the git commit the app was built from into its Info.plist as BuildCommit, at build time (not configure time, so
# a commit made after configuring is still recorded). Included before cmake/DevSign.cmake so the signature covers it.
if(APPLE)
    add_custom_command(TARGET AgentSynth POST_BUILD
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/write-build-commit.sh" "$<TARGET_BUNDLE_DIR:AgentSynth>"
                "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Writing BuildCommit into Info.plist"
        VERBATIM
    )
endif()
