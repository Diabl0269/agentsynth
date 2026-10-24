// Concern: the Audio tab's frame -- the six folding sections, their fold state and the stack layout.
#include "UI/Settings/AudioSettingsTab/AudioSettingsTab.h"

#include "UI/Settings/SettingsFoldState.h"

namespace {
constexpr const char* kFoldedKey = "audioFolded";
// Written first in the saved list so the key is never empty: an empty list would mean "nothing saved", which opens the
// default folds, and a user who unfolded everything must get everything unfolded back.
constexpr const char* kFoldMarker = "saved";
} // namespace

juce::String AudioSettingsTab::sectionName(Section section) {
    switch (section) {
    case Section::Device:
        return "Device";
    case Section::Inputs:
        return "Inputs";
    case Section::Interfaces:
        return "Remembered interfaces";
    case Section::Outputs:
        return "Outputs";
    case Section::Recording:
        return "Recording";
    case Section::Midi:
        return "MIDI";
    }
    return {};
}

juce::String AudioSettingsTab::persistedName(Section section) {
    switch (section) {
    case Section::Device:
        return "device";
    case Section::Inputs:
        return "inputs";
    case Section::Interfaces:
        return "interfaces";
    case Section::Outputs:
        return "outputs";
    case Section::Recording:
        return "recording";
    case Section::Midi:
        return "midi";
    }
    return {};
}

bool AudioSettingsTab::foldedByDefault(Section section) {
    return section == Section::Outputs || section == Section::Recording || section == Section::Midi;
}

AudioSettingsTab::AudioSettingsTab(juce::AudioDeviceManager& deviceManager,
                                   const std::vector<juce::String>& midiRemoteDeviceNames, AudioSettingsSource* source,
                                   juce::ApplicationProperties* props)
    : deviceManager_(deviceManager)
    , props_(props)
    , ownedSource_(source == nullptr ? std::make_unique<DeviceManagerSettingsSource>(deviceManager) : nullptr)
    , source_(source != nullptr ? *source : *ownedSource_) {
    source_.noteOpenInterface();

    juce::StringArray names;
    for (const auto& name : midiRemoteDeviceNames)
        if (name.isNotEmpty())
            names.addIfNotAlreadyThere(name);

    auto device = std::make_unique<AudioDeviceSection>(deviceManager_, source_);
    auto inputs = std::make_unique<AudioInputsTable>(source_);
    auto interfaces = std::make_unique<AudioInterfacesList>(source_);
    auto outputs = std::make_unique<AudioOutputsSection>(deviceManager_);
    auto recording = std::make_unique<AudioRecordingSection>(source_);
    auto midi = std::make_unique<AudioMidiSection>(source_, names);
    device_ = device.get();
    inputs_ = inputs.get();
    interfaces_ = interfaces.get();
    outputs_ = outputs.get();
    recording_ = recording.get();
    midi_ = midi.get();
    std::array<std::unique_ptr<AudioSettingsSection>, kNumSections> bodies{std::move(device),     std::move(inputs),
                                                                           std::move(interfaces), std::move(outputs),
                                                                           std::move(recording),  std::move(midi)};

    const auto saved = synth::ui::settingsfold::load(props_, kFoldedKey);
    for (int i = 0; i < kNumSections; ++i) {
        const auto section = static_cast<Section>(i);
        open_[(size_t)i] = saved.isEmpty() ? !foldedByDefault(section) : !saved.contains(persistedName(section));

        headers_[(size_t)i] = std::make_unique<synth::ui::FoldSectionHeader>(sectionName(section));
        headers_[(size_t)i]->setTooltip("Fold or unfold " + sectionName(section) + " (Left/Right)");
        headers_[(size_t)i]->setOpen(open_[(size_t)i]);
        headers_[(size_t)i]->onOpenRequested = [this, section](bool open) { setSectionOpen(section, open); };
        content_.addAndMakeVisible(*headers_[(size_t)i]);

        bodies_[(size_t)i] = bodies[(size_t)i].release();
        slots_[(size_t)i] = std::make_unique<Slot>();
        slots_[(size_t)i]->addAndMakeVisible(*bodies_[(size_t)i]);
        content_.addAndMakeVisible(*slots_[(size_t)i]);
        // What is there when the tab opens does not fade.
        fades_[(size_t)i] =
            std::make_unique<synth::ui::FadeVisibility>(std::vector<juce::Component*>{slots_[(size_t)i].get()});
        fades_[(size_t)i]->snapTo(open_[(size_t)i]);
        fades_[(size_t)i]->onFrame = [this] { layoutContent(); };
    }

    addAndMakeVisible(foldAll_);
    foldAll_.onClick = [this] {
        const bool openAll = foldAll_.isAllFolded();
        for (int i = 0; i < kNumSections; ++i)
            setSectionOpen(static_cast<Section>(i), openAll);
    };
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setWantsKeyboardFocus(false);
    addAndMakeVisible(viewport_);

    source_.layoutStore().addListener(this);
    deviceManager_.addChangeListener(this);
    refreshAll();
    updateFoldAll();
}

AudioSettingsTab::~AudioSettingsTab() {
    deviceManager_.removeChangeListener(this);
    source_.layoutStore().removeListener(this);
}

void AudioSettingsTab::setEditorHook(std::function<void(juce::TextEditor&)> hook) {
    inputs_->onEditorCreated = hook;
    if (!hook)
        return;
    for (int row = 0; row < inputs_->getNumRowsForTest(); ++row)
        hook(*inputs_->getNameEditorForTest(row));
}

void AudioSettingsTab::lookAndFeelChanged() { refreshAll(); }

void AudioSettingsTab::refreshAll() {
    if (refreshing_)
        return;
    const juce::ScopedValueSetter<bool> guard(refreshing_, true);
    source_.noteOpenInterface();
    for (int i = 0; i < kNumSections; ++i) {
        bodies_[(size_t)i]->refresh();
        headers_[(size_t)i]->setSummary(bodies_[(size_t)i]->summary());
    }
    layoutContent();
}

void AudioSettingsTab::setSectionOpen(Section section, bool open) {
    const auto i = (size_t)index(section);
    if (open_[i] == open)
        return;
    open_[i] = open;
    headers_[i]->setOpen(open);
    fades_[i]->setShown(open);
    saveFolds();
    updateFoldAll();
    layoutContent();
}

void AudioSettingsTab::saveFolds() const {
    juce::StringArray folded;
    folded.add(kFoldMarker);
    for (int i = 0; i < kNumSections; ++i)
        if (!open_[(size_t)i])
            folded.add(persistedName(static_cast<Section>(i)));
    synth::ui::settingsfold::save(props_, kFoldedKey, folded);
}

void AudioSettingsTab::updateFoldAll() {
    bool allFolded = true;
    for (const bool open : open_)
        allFolded = allFolded && !open;
    foldAll_.setAllFolded(allFolded);
}

void AudioSettingsTab::resized() {
    auto bounds = getLocalBounds();
    foldAll_.setBounds(bounds.removeFromTop(kStripHeight).reduced(8, 0));
    viewport_.setBounds(bounds);
    layoutContent();
}

// Headers stack top to bottom; a body's slot is as tall as its fade has got, so a fold slides the sections below.
void AudioSettingsTab::layoutContent() {
    const int width = juce::jmax(0, viewport_.getWidth() - viewport_.getScrollBarThickness());
    int y = 0;
    for (int i = 0; i < kNumSections; ++i) {
        const auto k = (size_t)i;
        headers_[k]->setBounds(0, y, width, synth::ui::kFoldSectionHeaderHeight);
        y += synth::ui::kFoldSectionHeaderHeight + kHeaderGap;
        const int bodyHeight = bodies_[k]->preferredHeight(width);
        bodies_[k]->setBounds(0, 0, width, bodyHeight);
        const int slotHeight = juce::roundToInt((float)bodyHeight * fades_[k]->progress());
        slots_[k]->setBounds(0, y, width, slotHeight);
        y += slotHeight + kSectionGap;
    }
    content_.setSize(width, juce::jmax(y, 1));
}
