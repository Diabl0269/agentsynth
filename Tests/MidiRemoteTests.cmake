# The MIDI Remote model, engine and MIDI Learn page tests (docs/control/midi-remote.md), kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteModelTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteModelPagesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteModelFocusBankTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteModelHandshakeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/ControllerHandshakeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEnginePagesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEngineFocusBankTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiLearnControllerPagesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiLearnControllerFocusBankTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEngineDecodeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEngineTwoHalfDecodeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEngineApplyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEngineFeedbackTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEngineReconcileTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEngineHostedTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEngineNodeCommandE2ETests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiRemoteWorkflowE2ETests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEngineThreadingTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/RemoteEngineLearnTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/ControllerProfileStoreTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiRemoteFeedbackOutputsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiRemoteLearnBinderTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiRemoteMappingTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiLearnControllerAssignTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiLearnControllerOrphanTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/ControllerTemplatesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/ControllerTemplatesVendorTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/ControllerDetectTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/EncoderAutoDetectTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiLearnControllerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiLearnControllerHandshakeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiLearnControllerRetargetTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiLearnControllerHostedParameterTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/ProfileEditHistoryTests.cpp
)
