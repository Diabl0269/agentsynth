# Popup motion: the shared soft appear/disappear of menus, call-outs and dialogs, and the
# Reduce motion query it obeys (ReducedMotion, also read by AppUI's CalloutReveal: AppUI links Core). In Core (not AppUI) because AppLookAndFeel, which hooks every popup
# menu and alert window, is compiled into Core. The .mm files are the macOS halves, ARC like
# SparkleUpdateManager.mm.
target_sources(Core PRIVATE
    Source/UI/Layout/PopupMotion.h
    Source/UI/Layout/PopupMotion.cpp
    Source/UI/Layout/ReducedMotion.h
    Source/UI/Layout/ReducedMotion.cpp
    Source/UI/Theme/AppLookAndFeel/AppLookAndFeelWindowMotion.cpp
)
if(APPLE)
    target_sources(Core PRIVATE Source/UI/Layout/PopupMotionMac.mm Source/UI/Layout/ReducedMotionMac.mm)
    set_source_files_properties(Source/UI/Layout/PopupMotionMac.mm Source/UI/Layout/ReducedMotionMac.mm
                                PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif()
