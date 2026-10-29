// DrumKitChrome.cpp -- the Drum Kit card's picker row: which kits it offers and what its caption says.
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"

namespace synth::ui {

namespace {

const char* const kDrumCategory = "Drums";
constexpr int kSectionGap = 8;
constexpr int kButtonGap = 4;
constexpr int kAvailabilityPollEvery = 8; // card ticks (about 2 Hz): the answer walks every track's graph

InstrumentChoiceOptions drumChoices() {
    InstrumentChoiceOptions options;
    options.category = kDrumCategory;
    options.offerSynthDrums = true;
    return options;
}

} // namespace

int DrumKitChrome::height() {
    return synth::cardbody::kRowHeight + InstrumentPickerRow::kCaptionGap + InstrumentPickerRow::kCaptionHeight +
           kButtonGap + synth::cardbody::kRowHeight + kSectionGap;
}

DrumKitChrome::DrumKitChrome(Host host, DrumKitModule& kit)
    : kit_(kit)
    , host_(host)
    , row_(host, kit, kit, drumChoices())
    , pads_(addDrumMachineButton(host), kit) {
    row_.addPickerToCard();
    row_.addCaptionToCard();
    row_.picker().setNoneLabel("Synth drums", "synth drums");
    row_.defaultCaption = [this] { return captionText(); };
    row_.picker().setVisible(true);
    row_.caption().setVisible(true);
    row_.refresh();

    // Picking a kit is asking to hear it: every pad plays it, in the same undo step as the load.
    row_.afterLoad = [this](bool ok) {
        if (ok && (kit_.hasRecordedKit() || kit_.isInstrumentLoadPending()))
            kit_.fillPadsWithKit();
    };
    drumMachineButton_.setComponentID("drumKitDrumMachineButton");
    drumMachineButton_.setTitle("Drum machine");
    drumMachineButton_.onClick = [this] {
        if (host_.openDrumMachine)
            host_.openDrumMachine();
    };
    refreshDrumMachineButton();
}

// Joins the card before the pads area's components, so the Tab order follows the card top to bottom.
const InstrumentChrome::Host& DrumKitChrome::addDrumMachineButton(const Host& host) {
    host.card->addAndMakeVisible(drumMachineButton_);
    return host;
}

int DrumKitChrome::layout(int y, int contentX, int contentW, int /*narrowX*/, int /*narrowW*/, bool apply) {
    const int below = row_.layout(y, contentX, contentW, contentW, apply) + kButtonGap;
    if (apply)
        drumMachineButton_.setBounds(contentX, below, contentW, synth::cardbody::kRowHeight);
    return below + synth::cardbody::kRowHeight + kSectionGap;
}

void DrumKitChrome::poll() {
    row_.poll();
    pads_.poll();
    if (kit_.padCount() != captionCount_) { // the caption counts the shown pads
        captionCount_ = kit_.padCount();
        row_.refresh();
    }
    if (++pollsSinceRefresh_ >= kAvailabilityPollEvery)
        refreshDrumMachineButton();
}

// Enabled only while a track plays this kit; the tooltip says why it is not, so the control never just goes dead.
void DrumKitChrome::refreshDrumMachineButton() {
    pollsSinceRefresh_ = 0;
    const bool available = host_.drumMachineAvailable && host_.drumMachineAvailable();
    if (drumMachineButton_.isEnabled() != available)
        drumMachineButton_.setEnabled(available);
    drumMachineButton_.setTooltip(available ? "Open the drum machine on the clip under the playhead"
                                            : "No track plays this Drum Kit. Add a track and connect it to this kit to "
                                              "use the drum machine.");
}

// What the kit does: how many of the shown pads play recordings now.
juce::String DrumKitChrome::captionText() const {
    if (!kit_.hasRecordedKit())
        return "Playing the synth drums";
    int playing = 0;
    for (int i = 0; i < kit_.padCount(); ++i)
        playing += kit_.padPlaysRecording(i) ? 1 : 0;
    return "Recordings on " + juce::String(playing) + " of " + juce::String(kit_.padCount()) + " pads";
}

} // namespace synth::ui
