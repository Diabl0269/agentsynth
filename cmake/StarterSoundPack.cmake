# The starter sound pack (three real instruments, CC0, ~6 MB: assets/sound-packs/starter) ships inside the app, so the
# Sampler's instrument list is never empty offline. SoundPackLibrary reads it first: <app>/Contents/Resources/SoundPacks
# on macOS, a SoundPacks folder beside the executable on Windows and Linux. Added before the dev-sign step below so the
# signature covers it; CI's `codesign --deep` and the installer's File /r pick it up from the same place. The plugin
# bundles do not carry it (a hosted plugin reads the host's folders, not its own).
set(SYNTH_STARTER_PACK_SRC "${CMAKE_SOURCE_DIR}/assets/sound-packs/starter")
if(APPLE)
    set(SYNTH_STARTER_PACK_DEST "$<TARGET_BUNDLE_CONTENT_DIR:AgentSynth>/Resources/SoundPacks/starter")
else()
    set(SYNTH_STARTER_PACK_DEST "$<TARGET_FILE_DIR:AgentSynth>/SoundPacks/starter")
endif()
add_custom_command(TARGET AgentSynth POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${SYNTH_STARTER_PACK_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SYNTH_STARTER_PACK_SRC}" "${SYNTH_STARTER_PACK_DEST}"
    COMMENT "Bundling the starter sound pack"
)
