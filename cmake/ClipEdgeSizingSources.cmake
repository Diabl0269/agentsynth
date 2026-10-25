# Clip edge sizing (docs/timeline/clips.md): the sizing rule a clip edge drag previews and commits with, the clip
# lanes' drag-time half, and the edit-tool strip's Sizing field. Split out of the root and AppUI lists, both at the
# file-size cap.
target_sources(Core PRIVATE
    Source/Timeline/TimelineDoc/ClipEdgeSizing.cpp
    Source/Timeline/TimelineDoc/ClipEdgeSizing.h
)
target_sources(AppUI PRIVATE
    Source/UI/Timeline/ClipSizingButton.cpp
    Source/UI/Timeline/ClipSizingButton.h
    Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneSizing.cpp
)
