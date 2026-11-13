# Following the song key: the follow-or-own key field and the sequencers' Key field built on it, appended to
# APPUI_SOURCES (included from cmake/SongKeySources.cmake). Also the Poly Sequencer card's poly hint line.
list(APPEND APPUI_SOURCES
    Source/UI/Layout/FollowKeyField.cpp
    Source/UI/Layout/FollowKeyField.h
    Source/UI/ModuleViews/SequencerKey/SequencerKeyField.cpp
    Source/UI/ModuleViews/SequencerKey/SequencerKeyField.h
    Source/UI/ModuleViews/StepGrid/StepGridPolyHint.cpp
    Source/UI/ModuleViews/StepGrid/StepGridPolyHint.h
)
