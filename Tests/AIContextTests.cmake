# The AI macro-port routing, collapsed request-context and edit-plan apply-budget tests, kept in their own list so Tests/CMakeLists.txt
# stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/AI/AIMacroPortRoutingTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AI/AIScopeContextTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AI/ServerUrlPolicyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/AIApplyBudgetTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Settings/SettingsWindowServerAddressTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Settings/SettingsWindowLabelTests.cpp
)
