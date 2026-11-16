# Protocol control surfaces (Mackie Control, docs/control/midi-remote-mcu-hui.md): the MIDI-path router and
# decode plus the renderer in Core, the mixer-facing controller and MainComponent's wiring in AppUI. Split out
# of the root lists, both at the file-size cap.
target_sources(Core PRIVATE
    Source/MidiRemote/RemoteModelJsonProtocol.cpp
    Source/MidiRemote/Surface/McuProtocol.h
    Source/MidiRemote/Surface/McuSurface.cpp
    Source/MidiRemote/Surface/McuSurface.h
    Source/MidiRemote/Surface/ProtocolSurface.cpp
    Source/MidiRemote/Surface/ProtocolSurface.h
    Source/MidiRemote/Surface/RemoteSinkRouter.cpp
    Source/MidiRemote/Surface/RemoteSinkRouter.h
    Source/MidiRemote/Surface/SurfaceEvent.h
    Source/MidiRemote/Surface/SurfaceRenderer.cpp
    Source/MidiRemote/Surface/SurfaceRenderer.h
)
target_sources(AppUI PRIVATE
    Source/MidiRemote/Surface/SurfaceController.cpp
    Source/MidiRemote/Surface/SurfaceController.h
    Source/MidiRemote/Surface/SurfaceControllerEvents.cpp
    Source/MainComponent/MainComponentControlSurfaces.cpp
)
