# Bundle metadata shared by the app and plugin targets, split out of the root CMakeLists.txt to keep
# that file under the repo's 1,000-line file-size cap (scripts/check-file-sizes.sh).

# App/plugin icon: the full original logo at every size (1024 for the big slot, 256 for the small).
set(SYNTH_ICON_ARGS
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/agentsynth-logo-1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/agentsynth-logo-256.png")

# Sparkle Info.plist keys, merged into JUCE's generated plist. Empty string on non-Apple
# platforms, where JUCE ignores the argument entirely.
set(SYNTH_SPARKLE_PLIST_TO_MERGE "")
if(APPLE)
    set(SYNTH_SPARKLE_PLIST_TO_MERGE
        "<?xml version='1.0' encoding='UTF-8'?><plist version='1.0'><dict><key>SUFeedURL</key><string>${SYNTH_UPDATE_FEED_URL}</string><key>SUEnableAutomaticChecks</key><true/><key>SUScheduledCheckInterval</key><integer>86400</integer><key>SUPublicEDKey</key><string>${SYNTH_SPARKLE_PUBLIC_KEY}</string></dict></plist>")
endif()
