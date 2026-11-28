# Launch-time guards: sizing the main window with no main display, and logging an exception that escapes a
# message-thread callback. Kept out of the root CMakeLists.txt so it stays under the file-size cap.
target_sources(Core PRIVATE
    Source/MainWindowPlacement.h
    Source/UnhandledExceptionLog.cpp
    Source/UnhandledExceptionLog.h
)
# Without this JUCE lets such an exception reach std::terminate inside MessageQueue::runLoopCallback with no
# message; with it, AppApplication::unhandledException logs what() first. Only the app target: the plugin has no
# JUCEApplication to receive it.
target_compile_definitions(AgentSynth PRIVATE JUCE_CATCH_UNHANDLED_EXCEPTIONS=1)
