// SamplerChrome.cpp -- construction, layout, the mode cross-fade and the gated tick. The Load Sample chooser and the
// file drop are in SamplerChromeLoad.cpp; the picker, its choosers and the undo step in InstrumentPickerRow.cpp.
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerChrome.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"

namespace synth::ui {

namespace {

constexpr int kStripGap = 2;
constexpr int kSectionGap = 8;
constexpr int kEditMapWidth = 72;
constexpr int kEditMapGap = 6;

using synth::cardbody::kRowHeight;
using synth::cardbody::kWaveformHeight;

} // namespace

SamplerChrome::SamplerChrome(Host host, SamplerModule& sampler)
    : host_(host)
    , sampler_(sampler)
    , waveform_(sampler)
    , row_(host, sampler, sampler, {})
    , strip_(sampler) {
    auto& card = *host_.card;
    loadButton_.setTooltip("Load an audio file (WAV, AIFF, FLAC, Ogg) into this Sampler");
    loadButton_.onClick = [this] { launchSampleChooser(); };
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setMinimumHorizontalScale(0.7f);
    row_.defaultCaption = [this] {
        strip_.refresh();
        return strip_.getCaptionText();
    };
    editMapButton_.setTitle("Edit map");
    editMapButton_.setTooltip("Edit map: see every recording on the keys and velocity layers, and change them");
    editMapButton_.onClick = [this] { openMap(); };
    strip_.onDoubleClick = [this] { openMap(); };

    // The order is the tab order and the child order the card goldens pin.
    for (juce::Component* part :
         {(juce::Component*)&waveform_, (juce::Component*)&loadButton_, (juce::Component*)&nameLabel_})
        card.addChildComponent(*part);
    row_.addPickerToCard();
    card.addChildComponent(editMapButton_);
    row_.addCaptionToCard();
    card.addChildComponent(strip_);

    if (auto* mode = findParameterByID(&sampler_, "playMode"))
        modeAttachment_ = std::make_unique<juce::ParameterAttachment>(
            *mode, [this](float index) { applyMode(juce::roundToInt(index) == SamplerModule::kModeInstrument, true); });
    refreshSampleLabel();
    applyMode(isInstrumentMode(), false);
}

SamplerChrome::~SamplerChrome() {
    modeAttachment_.reset();
    closeMap();
    for (auto* part : {(juce::Component*)&waveform_, (juce::Component*)&loadButton_, (juce::Component*)&nameLabel_,
                       (juce::Component*)&editMapButton_, (juce::Component*)&strip_})
        if (auto* parent = part->getParentComponent())
            parent->removeChildComponent(part);
}

bool SamplerChrome::isInstrumentMode() const {
    const auto* mode = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&sampler_, "playMode"));
    return mode != nullptr && mode->getIndex() == SamplerModule::kModeInstrument;
}

// The two sets overlap, so a mode change fades one out while the other fades in; a card that is not on screen lands
// at once.
void SamplerChrome::applyMode(bool instrument, bool animate) {
    if (instrument)
        row_.refresh();
    if (animate) {
        sampleFade_.setShown(!instrument);
        instrumentFade_.setShown(instrument);
    } else {
        sampleFade_.snapTo(!instrument);
        instrumentFade_.snapTo(instrument);
    }
}

// The sample set keeps its old order (waveform, then the load row); the instrument set reads top to bottom: picker,
// caption, key strip. Both fill the same height.
int SamplerChrome::layout(int y, int contentX, int contentW, int narrowX, int narrowW, bool apply) {
    const int total = kWaveformHeight + kSectionGap + kRowHeight + kSectionGap;
    if (!apply)
        return y + total;

    waveform_.setBounds(contentX, y, contentW, kWaveformHeight);
    const int buttonWidth = juce::jmax(96, narrowW / 2);
    const int loadY = y + kWaveformHeight + kSectionGap;
    loadButton_.setBounds(narrowX, loadY, buttonWidth, kRowHeight);
    nameLabel_.setBounds(narrowX + buttonWidth + 6, loadY, narrowW - buttonWidth - 6, kRowHeight);

    editMapButton_.setBounds(contentX + contentW - kEditMapWidth, y, kEditMapWidth, kRowHeight);
    const int captionBottom = row_.layout(y, contentX, contentW - kEditMapWidth - kEditMapGap, contentW, true);
    const int stripY = captionBottom + kStripGap;
    strip_.setBounds(contentX, stripY, contentW, y + total - kSectionGap - stripY);
    return y + total;
}

void SamplerChrome::poll() {
    row_.poll(); // follows an instrument that changed behind our back (undo, a project load, a restore that failed)
    if (sampler_.getSampleGeneration() != shownSampleGeneration_)
        refreshSampleLabel(); // an undo or redo swaps the sample in place, without a rebuild of the card
    if (strip_.isVisible())
        strip_.poll();
    if (mapView_ != nullptr)
        mapView_->poll();
}

void SamplerChrome::refreshLoaded() {
    refreshSampleLabel();
    row_.refresh();
}

void SamplerChrome::refreshSampleLabel(const juce::String& fallbackMessage) {
    const juce::String name = sampler_.getSampleName();
    shownSampleGeneration_ = sampler_.getSampleGeneration();
    nameLabel_.setText(fallbackMessage.isNotEmpty() ? fallbackMessage
                                                    : (name.isEmpty() ? juce::String("(no sample)") : name),
                       juce::dontSendNotification);
    nameLabel_.setTooltip(name);
    host_.card->repaint();
}

} // namespace synth::ui
