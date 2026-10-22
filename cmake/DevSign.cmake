# Every local macOS build, not only ci-local's, gets the stable dev signature: an ad-hoc bundle's
# designated requirement is a cdhash that changes per build, so macOS re-asks for the login
# keychain (the saved sign-in token) and the microphone on every launch. Must be included after
# the POST_BUILD step that embeds Sparkle, so the signature covers it. No-ops on CI and on
# machines without a dev identity (see docs/development/local-ci.md#dev-signing).
if(NOT DEFINED ENV{CI})
    # Local builds also keep their own keychain sign-in item (KeychainTokenStore.cpp, in Core),
    # apart from the release app's, which this signature is not trusted to read.
    target_compile_definitions(Core PRIVATE AGENTSYNTH_DEV_KEYCHAIN=1)
    add_custom_command(TARGET AgentSynth POST_BUILD
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/dev-sign-app.sh" "$<TARGET_BUNDLE_DIR:AgentSynth>"
        COMMENT "Dev-signing Agent Synth.app"
    )
endif()
