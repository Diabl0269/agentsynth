# The AI chat request field's wrap and growing input row tests (docs/ai/chat-component.md), kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Assistant/AIChatComponent/AIChatComponentInputRowTests.cpp
)
