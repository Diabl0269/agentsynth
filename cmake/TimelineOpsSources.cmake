# The timelineOps tools (validate, preview, apply): the op batch, the addInstrumentTrack field reader, and the
# setTempo / addMarker readers. Split out of the root list to keep CMakeLists.txt under the file-size cap.
target_sources(Core PRIVATE
    Source/Timeline/InstrumentTrackOpReader.cpp
    Source/Timeline/InstrumentTrackOpReader.h
    Source/Timeline/TimelineOps.cpp
    Source/Timeline/TimelineOps.h
    Source/Timeline/TimelineOpsTempoMarkers.cpp
    Source/Timeline/TimelineOpsTempoMarkers.h
)
