# The sound index: a background scan of the sound packs and the user's folders, name guessing and the search.
# Part of Core (no UI); their own list so the root file stays under the line cap.
target_sources(Core PRIVATE
    Source/SoundBrowser/SoundEntry.h
    Source/SoundBrowser/SoundIndex.cpp
    Source/SoundBrowser/SoundIndex.h
    Source/SoundBrowser/SoundIndexScan.cpp
    Source/SoundBrowser/SoundIndexScan.h
    Source/SoundBrowser/SoundLists.cpp
    Source/SoundBrowser/SoundLists.h
    Source/SoundBrowser/SoundNameGuess.cpp
    Source/SoundBrowser/SoundNameGuess.h
    Source/SoundBrowser/SoundSearch.cpp
    Source/SoundBrowser/SoundSearch.h
)

# The Sounds view and the sidebar that switches between it and the module library (App UI).
set(SOUND_BROWSER_UI_SOURCES
    Source/MainComponent/MainComponentLibraryViews.cpp
    Source/UI/Library/LibrarySidebarSwitch.cpp
    Source/UI/Library/LibrarySidebarSwitch.h
    Source/UI/Library/LibrarySidebarViews.cpp
    Source/UI/Library/LibrarySidebarViews.h
    Source/UI/Library/SoundBrowser/SoundBrowserAccessibility.cpp
    Source/UI/Library/SoundBrowser/SoundBrowserComponent.cpp
    Source/UI/Library/SoundBrowser/SoundBrowserComponent.h
    Source/UI/Library/SoundBrowser/SoundBrowserInput.cpp
    Source/UI/Library/SoundBrowser/SoundBrowserNavigation.cpp
    Source/UI/Library/SoundBrowser/SoundBrowserPainting.cpp
    Source/UI/Library/SoundBrowser/SoundBrowserText.cpp
    Source/UI/Library/SoundBrowser/SoundBrowserText.h
)
