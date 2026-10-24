#pragma once

#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FoldAllButton.h"
#include "UI/Layout/FoldSectionHeader.h"
#include "UI/Settings/AudioSettingsTab/AudioDeviceSection.h"
#include "UI/Settings/AudioSettingsTab/AudioInputsTable.h"
#include "UI/Settings/AudioSettingsTab/AudioInterfacesList.h"
#include "UI/Settings/AudioSettingsTab/AudioMidiSection.h"
#include "UI/Settings/AudioSettingsTab/AudioOutputsSection.h"
#include "UI/Settings/AudioSettingsTab/AudioRecordingSection.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsSource.h"
#include <array>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// The Settings "Audio" tab (docs/layout/settings-preferences.md#the-audio-tab): six folding sections like
// Preferences -- Device, Inputs, Remembered interfaces, Outputs, Recording, MIDI -- each a header with a one-line
// summary over a body. Which are folded is remembered under the user setting "audioFolded"; with none saved, Outputs,
// Recording and MIDI start folded. A fold fades the body and lets the sections below follow its height.
//
// Everything the tab learns about the machine and the app comes through an AudioSettingsSource, so a test feeds it a
// fake interface. With none given it reads the juce::AudioDeviceManager itself.
class AudioSettingsTab
    : public juce::Component
    , private juce::ChangeListener
    , private synth::AudioInputLayoutStore::Listener {
public:
    enum class Section { Device, Inputs, Interfaces, Outputs, Recording, Midi };
    static constexpr int kNumSections = 6;
    static juce::String sectionName(Section section);
    /** The stable name a section is saved under in "audioFolded" (never the display text). */
    static juce::String persistedName(Section section);
    static bool foldedByDefault(Section section);

    /** `midiRemoteDeviceNames`: the input-device names of the profiled controllers on this machine (duplicates and
     *  empties are dropped). Empty: no caption. `source` null: read `deviceManager` directly. `props` null: fold
     *  state is not remembered. */
    AudioSettingsTab(juce::AudioDeviceManager& deviceManager, const std::vector<juce::String>& midiRemoteDeviceNames,
                     AudioSettingsSource* source = nullptr, juce::ApplicationProperties* props = nullptr);
    ~AudioSettingsTab() override;

    void resized() override;
    void lookAndFeelChanged() override;

    /** Offers every name field the Inputs table makes to the window's tab-switch keys. */
    void setEditorHook(std::function<void(juce::TextEditor&)> hook);

    bool isSectionOpen(Section section) const { return open_[index(section)]; }
    void setSectionOpen(Section section, bool open);
    void refreshAll();

    juce::Button& getSectionHeaderForTest(Section section) { return *headers_[index(section)]; }
    synth::ui::FoldSectionHeader& getFoldHeaderForTest(Section section) { return *headers_[index(section)]; }
    AudioSettingsSection& getSectionForTest(Section section) { return *bodies_[index(section)]; }
    AudioInputsTable& getInputsTableForTest() { return *inputs_; }
    AudioInterfacesList& getInterfacesForTest() { return *interfaces_; }
    AudioRecordingSection& getRecordingForTest() { return *recording_; }
    AudioMidiSection& getMidiForTest() { return *midi_; }
    synth::ui::FoldAllButton& getFoldAllButtonForTest() { return foldAll_; }
    juce::Viewport& getViewportForTest() { return viewport_; }
    int getContentHeightForTest() const { return content_.getHeight(); }
    /** Empty when there is no caption. */
    juce::String getCaptionTextForTest() const { return midi_->getCaptionText(); }

private:
    static constexpr int kStripHeight = synth::ui::FoldAllButton::kStripHeight;
    static constexpr int kSectionGap = 8;
    static constexpr int kHeaderGap = 2;
    static int index(Section section) { return static_cast<int>(section); }

    // The body sits in a slot that is squashed to the fade's height, so the body is clipped rather than squeezed.
    struct Slot : juce::Component {};

    void changeListenerCallback(juce::ChangeBroadcaster*) override { refreshAll(); }
    void inputLayoutChanged() override { refreshAll(); }
    void layoutContent();
    void saveFolds() const;
    void updateFoldAll();

    juce::AudioDeviceManager& deviceManager_;
    juce::ApplicationProperties* props_;
    std::unique_ptr<AudioSettingsSource> ownedSource_;
    AudioSettingsSource& source_;

    juce::Viewport viewport_;
    juce::Component content_;
    synth::ui::FoldAllButton foldAll_;
    std::array<std::unique_ptr<synth::ui::FoldSectionHeader>, kNumSections> headers_;
    std::array<std::unique_ptr<Slot>, kNumSections> slots_;
    std::array<std::unique_ptr<synth::ui::FadeVisibility>, kNumSections> fades_;
    std::array<AudioSettingsSection*, kNumSections> bodies_{};
    std::array<bool, kNumSections> open_{};

    AudioDeviceSection* device_ = nullptr;
    AudioInputsTable* inputs_ = nullptr;
    AudioInterfacesList* interfaces_ = nullptr;
    AudioOutputsSection* outputs_ = nullptr;
    AudioRecordingSection* recording_ = nullptr;
    AudioMidiSection* midi_ = nullptr;
    bool refreshing_ = false;
    synth::ui::ScrollIntoViewOnFocus followFocus_{viewport_};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsTab)
};
