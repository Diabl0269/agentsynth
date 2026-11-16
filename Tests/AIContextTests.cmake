# The AI macro-port routing and collapsed request-context tests, kept in their own list so Tests/CMakeLists.txt
# stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/AI/AIMacroPortRoutingTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AI/AIScopeContextTests.cpp
)
