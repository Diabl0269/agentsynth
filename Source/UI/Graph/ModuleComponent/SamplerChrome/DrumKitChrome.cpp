// DrumKitChrome.cpp -- the Drum Kit card's picker row: which kits it offers and what its caption says.
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"

namespace synth::ui {

namespace {

const char* const kDrumCategory = "Drums";
constexpr int kSectionGap = 8;
constexpr int kFirstDrumNote = 35;
constexpr int kLastDrumNote = 59;

InstrumentChoiceOptions drumChoices() {
    InstrumentChoiceOptions options;
    options.category = kDrumCategory;
    options.offerSynthDrums = true;
    return options;
}

} // namespace

int DrumKitChrome::height() {
    return synth::cardbody::kRowHeight + InstrumentPickerRow::kCaptionGap + InstrumentPickerRow::kCaptionHeight +
           kSectionGap;
}

DrumKitChrome::DrumKitChrome(Host host, DrumKitModule& kit)
    : kit_(kit)
    , row_(host, kit, kit, drumChoices()) {
    row_.addPickerToCard();
    row_.addCaptionToCard();
    row_.picker().setNoneLabel("Synth drums", "synth drums");
    row_.defaultCaption = [this] { return captionText(); };
    row_.picker().setVisible(true);
    row_.caption().setVisible(true);
    row_.refresh();
}

int DrumKitChrome::layout(int y, int contentX, int contentW, int /*narrowX*/, int /*narrowW*/, bool apply) {
    return row_.layout(y, contentX, contentW, contentW, apply) + kSectionGap;
}

// What the kit does: how many of the drum groups play recordings now.
juce::String DrumKitChrome::captionText() const {
    if (!kit_.hasRecordedKit())
        return "Playing the synth drums";
    std::bitset<DrumKitModule::kNumGroups> playing;
    for (int note = kFirstDrumNote; note <= kLastDrumNote; ++note)
        if (const auto group = DrumKitModule::groupForNote(note); group != DrumKitModule::Group::None)
            playing.set((size_t)group, playing.test((size_t)group) || kit_.playsRecording(note));
    return "Recordings on " + juce::String(playing.count()) + " of " + juce::String(DrumKitModule::kNumGroups) +
           " drum groups";
}

} // namespace synth::ui
