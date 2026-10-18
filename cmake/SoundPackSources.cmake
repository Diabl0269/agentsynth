# The sound pack installer: the hosted pack list, the download and the unpack-and-swap steps. Part of Core (no UI);
# their own list so the root file stays under the line cap.
target_sources(Core PRIVATE
    Source/SoundPacks/CoreSounds.cpp
    Source/SoundPacks/CoreSounds.h
    Source/SoundPacks/SoundPackChecksum.cpp
    Source/SoundPacks/SoundPackChecksum.h
    Source/SoundPacks/SoundPackFetch.cpp
    Source/SoundPacks/SoundPackFetch.h
    Source/SoundPacks/SoundPackIndex.cpp
    Source/SoundPacks/SoundPackIndex.h
    Source/SoundPacks/SoundPackInstallWork.cpp
    Source/SoundPacks/SoundPackInstallWork.h
    Source/SoundPacks/SoundPackInstaller.cpp
    Source/SoundPacks/SoundPackInstaller.h
)
