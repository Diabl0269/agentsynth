# The Custom filter's Pitch cable for the per-note modes (the upstream lookup and the shape window's By hand / By
# pitch choice), kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Mixer/ChannelFlow/UpstreamPitchSourceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/PerNotePitchCableTests.cpp
)
