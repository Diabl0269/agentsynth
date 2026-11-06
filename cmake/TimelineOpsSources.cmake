# The timelineOps tools (validate, preview, apply): the op batch, the addInstrumentTrack field reader, and the
# setTempo / addMarker readers, and the sections units (model ops, sync, file format, validator). Split out of the root list to keep CMakeLists.txt under the file-size cap.
target_sources(Core PRIVATE
    Source/Timeline/InstrumentTrackOpReader.cpp
    Source/Timeline/InstrumentTrackOpReader.h
    Source/Timeline/TimelineOps.cpp
    Source/Timeline/TimelineOps.h
    Source/Timeline/TimelineOpsLaneRepair.cpp
    Source/Timeline/TimelineOpsLaneRepair.h
    Source/Timeline/TimelineOpsNotes.cpp
    Source/Timeline/TimelineOpsNotes.h
    Source/Timeline/TimelineOpsTempoMarkers.cpp
    Source/Timeline/TimelineOpsTempoMarkers.h
    Source/Timeline/TimelineDoc/TimelineDocSections.cpp
    Source/Timeline/TimelineDoc/TimelineDocSectionSync.cpp
    Source/Timeline/TimelineDoc/TimelineDocSectionSerialization.cpp
    Source/Timeline/TimelineValidatorSections.cpp
    Source/Timeline/TimelineValidatorSections.h
)
