# Audio clip streaming: the disk streamer that feeds clip playback, and the live time-stretch that lets a clip follow
# the song's tempo. Split out of the root list to keep CMakeLists.txt under the file-size cap.
target_sources(Core PRIVATE
    Source/Timeline/AudioClipStreamer.cpp
    Source/Timeline/AudioClipStreamer.h
    Source/Timeline/AudioClipStreamerWarp.cpp
    Source/Timeline/AudioClipWarp.cpp
    Source/Timeline/AudioClipWarp.h
)
