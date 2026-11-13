# Popup motion (and the tooltip window every launchDialog dialog hosts, so it is in Core too): the shared soft appear/disappear of menus, call-outs and dialogs, and the
# Reduce motion query it obeys (ReducedMotion, also read by AppUI: AppUI links Core). In Core (not AppUI) because AppLookAndFeel, which hooks every popup
# menu and alert window (and draws the alert box), is compiled into Core. The .mm files are the macOS halves, ARC like
# SparkleUpdateManager.mm.
target_sources(Core PRIVATE
    Source/UI/Layout/PopupMotion.h
    Source/UI/Layout/PopupMotion.cpp
    Source/UI/Layout/AppTooltipWindow.h
    Source/UI/Layout/AppTooltipWindow.cpp
    Source/UI/Layout/HelperTooltip.h
    Source/UI/Layout/DialogMenuKeys.h
    Source/UI/Layout/DialogMenuKeys.cpp
    Source/UI/Layout/DialogTooltip.h
    Source/UI/Layout/DialogTooltip.cpp
    Source/UI/Layout/ReducedMotion.h
    Source/UI/Layout/ReducedMotion.cpp
    Source/UI/Layout/MainMenuTooltips.h
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelWindowMotion.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelAlertBox.cpp
)
if(APPLE)
    target_sources(Core PRIVATE Source/UI/Layout/PopupMotionMac.mm Source/UI/Layout/ReducedMotionMac.mm
                                      Source/UI/Layout/MainMenuTooltipsMac.mm)
    set_source_files_properties(Source/UI/Layout/PopupMotionMac.mm Source/UI/Layout/ReducedMotionMac.mm
                                Source/UI/Layout/MainMenuTooltipsMac.mm
                                PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif()
