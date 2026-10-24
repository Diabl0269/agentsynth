# Settings > Audio in folding sections (docs/layout/settings-preferences.md#the-audio-tab): the per-interface input
# layout store (Core) and the tab's section components (AppUI).
target_sources(Core PRIVATE
    Source/AudioEngine/AudioInputLayoutStore.cpp
    Source/AudioEngine/AudioInputLayoutStore.h
)
target_sources(AppUI PRIVATE
    Source/UI/Layout/FoldSectionHeader.cpp
    Source/UI/Layout/FoldSectionHeader.h
)
target_sources(AppUI PRIVATE
    Source/MainComponent/MainComponentAudioSettingsSource.cpp
    Source/MainComponent/MainComponentAudioSettingsSource.h
    Source/UI/Settings/AudioSettingsTab/AudioDeviceSection.cpp
    Source/UI/Settings/AudioSettingsTab/AudioDeviceSection.h
    Source/UI/Settings/AudioSettingsTab/AudioInputLevelCell.cpp
    Source/UI/Settings/AudioSettingsTab/AudioInputLevelCell.h
    Source/UI/Settings/AudioSettingsTab/AudioInputsTable.cpp
    Source/UI/Settings/AudioSettingsTab/AudioInputsTable.h
    Source/UI/Settings/AudioSettingsTab/AudioInterfacesList.cpp
    Source/UI/Settings/AudioSettingsTab/AudioInterfacesList.h
    Source/UI/Settings/AudioSettingsTab/AudioMidiSection.cpp
    Source/UI/Settings/AudioSettingsTab/AudioMidiSection.h
    Source/UI/Settings/AudioSettingsTab/AudioOutputsSection.cpp
    Source/UI/Settings/AudioSettingsTab/AudioOutputsSection.h
    Source/UI/Settings/AudioSettingsTab/AudioRecordingSection.cpp
    Source/UI/Settings/AudioSettingsTab/AudioRecordingSection.h
    Source/UI/Settings/AudioSettingsTab/AudioSettingsSection.h
    Source/UI/Settings/AudioSettingsTab/AudioSettingsSource.cpp
    Source/UI/Settings/AudioSettingsTab/AudioSettingsSource.h
    Source/UI/Settings/AudioSettingsTab/LeavingRows.h
)
