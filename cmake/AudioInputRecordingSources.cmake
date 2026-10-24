# Recording audio from each track's own input: the capture core shared with the Rec Tap, the engine's input
# recorder and input switch-on, and MainComponent's take list, and rendering a selection to an audio track. Split out of the root lists, both at the file-size cap.
target_sources(Core PRIVATE
    Source/Timeline/TakeCaptureWriter.cpp
    Source/Timeline/TakeCaptureWriter.h
    Source/AudioEngine/AudioEngineInputRecording.cpp
    Source/AudioEngine/InputTakeRecorder.cpp
    Source/AudioEngine/InputTakeRecorder.h
    Source/AudioEngine/DeviceInputEnable.cpp
    Source/AudioEngine/DeviceInputEnable.h
    Source/AudioEngine/AudioEngineTrackMonitoring.cpp
    Source/Timeline/TrackAudioInput.h
    Source/Timeline/TrackInputMonitor.h
    Source/Transport/MonitorGuardReports.h
)
target_sources(AppUI PRIVATE
    Source/MainComponent/MainComponentAudioTakes.cpp
    Source/MainComponent/MainComponentRenderToAudio.cpp
    Source/MainComponent/MainComponentRenderToAudio.h
    Source/UI/Chrome/RenderProgressDialog.cpp
    Source/UI/Chrome/RenderProgressDialog.h
    Source/UI/Timeline/RenderSpan.h
    Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneRenderToAudio.cpp
    Source/MainComponent/MainComponentAudioInputSurface.cpp
    Source/MainComponent/MainComponentAudioInputSurface.h
    Source/UI/Timeline/TrackAudioInputSurface.h
    Source/UI/Timeline/TrackMonitorButton.cpp
    Source/UI/Timeline/TrackMonitorButton.h
    Source/UI/Timeline/TimelineRoutingPane/AudioInputMenu.cpp
    Source/UI/Timeline/TimelineRoutingPane/AudioInputMenu.h
    Source/UI/Timeline/TimelineRoutingPane/InputLevelMeter.cpp
    Source/UI/Timeline/TimelineRoutingPane/InputLevelMeter.h
    Source/UI/Timeline/TimelineRoutingPane/TimelineRoutingPaneAudioInput.cpp
)
