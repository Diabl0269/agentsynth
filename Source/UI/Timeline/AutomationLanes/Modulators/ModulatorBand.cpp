// Concern: the modulator band's identity and picture -- what routing it stands for, its names and
// tooltip, and painting the sections. The gestures live in ModulatorBandGestures.cpp, the keys in
// ModulatorBandKeys.cpp.
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorBand.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TimelineBeatsPerBar.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kBlockRadius = 3.0f;
constexpr float kSelectedOutline = 2.0f;
constexpr float kBlockInset = 1.0f;

// The tooltip's tail, naming what each tool does; the head says what the band is showing.
const char* const kToolHelp = "Draw tool: drag to turn the modulator on. Erase tool: drag to turn it off. Select "
                              "tool: drag a block's edge to resize it, its middle to move it, Delete removes it. "
                              "Double-click adds a bar. With no sections it plays everywhere, and erasing the "
                              "last one goes back to that.";
} // namespace

ModulatorBand::ModulatorBand(TimelineViewState& viewState)
    : viewState_(viewState) {
    setComponentID("modulatorBand");
    setInterceptsMouseClicks(false, false);
    setAccessible(false);
}

// An LFO's band is a real control; any other source's is decoration the row in the header column
// already names, and it takes no clicks so the clip lanes underneath still decide.
void ModulatorBand::setModulator(const ModulatorInfo& info, synth::LaneId ownerLane,
                                 const juce::String& parameterName) {
    info_ = info;
    ownerLane_ = ownerLane;
    parameterName_ = parameterName;
    if (isAccessible() != info_.isLfo) {
        setInterceptsMouseClicks(info_.isLfo, false);
        setWantsKeyboardFocus(info_.isLfo);
        setAccessible(info_.isLfo);
    }
    refreshFromDoc();
}

// A selection that an undo (or another edit) took away is dropped here rather than left pointing at
// nothing -- except in the middle of this band's own write, which creates the lane before it fills it.
void ModulatorBand::refreshFromDoc() {
    if (!committing_ && selectedStart_.has_value() && blockIndexStartingAt(currentBlocks(), *selectedStart_) < 0)
        selectedStart_.reset();
    applyNames();
    repaint();
}

SectionBlocks ModulatorBand::currentBlocks() const {
    if (doc_ == nullptr)
        return {};
    const auto* lane = sectionsLaneFor(*doc_, info_.sourceUuid);
    return lane != nullptr ? sectionsFromPoints(lane->points) : SectionBlocks{};
}

bool ModulatorBand::hasSectionsLane() const {
    return doc_ != nullptr && sectionsLaneFor(*doc_, info_.sourceUuid) != nullptr;
}

double ModulatorBand::beatsPerBar() const { return beatsPerBarFor(transport_); }

// The screen reader hears "<Parameter> LFO sections" and the blocks as the description, in bars.
void ModulatorBand::applyNames() {
    if (!info_.isLfo)
        return;
    setTitle(parameterName_ + " LFO sections");
    setDescription(describeSections(currentBlocks(), hasSectionsLane(), beatsPerBar()));
}

juce::String ModulatorBand::getTooltip() {
    if (!info_.isLfo)
        return {};
    const auto head = hasSectionsLane() ? juce::String("Sections: ") + getDescription() + ". "
                                        : juce::String("Draw sections to play this modulator only there. ");
    return head + kToolHelp;
}

synth::TrackId ModulatorBand::ownerTrack() const {
    const auto* track = doc_ != nullptr ? doc_->getTrackForLane(ownerLane_) : nullptr;
    return track != nullptr ? track->id : synth::TrackId();
}

//==============================================================================
void ModulatorBand::paintBlock(juce::Graphics& g, const SectionBlock& block, bool selected) {
    const float x0 = (float)viewState_.beatToX(block.start);
    const float x1 = std::isfinite(block.end) ? (float)viewState_.beatToX(block.end) : (float)getWidth() + 8.0f;
    if (x1 < 0.0f || x0 > (float)getWidth())
        return;
    const auto area = juce::Rectangle<float>(x0, kBlockInset, x1 - x0, (float)getHeight() - 2.0f * kBlockInset);
    g.setColour(info_.colour.withAlpha(kBandAlpha));
    g.fillRoundedRectangle(area, kBlockRadius);
    g.setColour(info_.colour);
    g.drawRoundedRectangle(area.reduced(0.5f), kBlockRadius, 1.0f);
    if (!selected)
        return;
    juce::Colour accent = juce::Colours::orange;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        accent = lf->getTheme().colors.accent;
    g.setColour(accent);
    g.drawRoundedRectangle(area.reduced(kSelectedOutline * 0.5f), kBlockRadius, kSelectedOutline);
}

// No sections lane (or a band that is only a decoration): the whole band is the faint "on everywhere"
// fill, as it always was. With one, only its blocks are drawn; what a drag would write is drawn instead
// of the doc's blocks while the drag runs.
void ModulatorBand::paint(juce::Graphics& g) {
    if (!info_.isLfo || (!hasSectionsLane() && !preview_.has_value())) {
        g.fillAll(info_.colour.withAlpha(kBandAlpha));
    } else {
        const auto blocks = preview_.has_value() ? *preview_ : currentBlocks();
        const int selected = selectedStart_.has_value() ? blockIndexStartingAt(blocks, *selectedStart_) : -1;
        for (int i = 0; i < (int)blocks.size(); ++i)
            paintBlock(g, blocks[(size_t)i], i == selected);
    }
    if (info_.isLfo)
        paintFocusRing(g, getLocalBounds().toFloat(), *this, kBlockRadius);
}

} // namespace synth::ui
