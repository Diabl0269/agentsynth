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
