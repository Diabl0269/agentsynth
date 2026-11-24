# The AI chat request field's wrap and growing input row tests, and the AI cards' one action-row rule
# (docs/ai/chat-component.md), kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Assistant/AIChatComponent/AIChatComponentInputRowTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Assistant/AIChatComponent/AIChatCardActionRowTests.cpp
)
