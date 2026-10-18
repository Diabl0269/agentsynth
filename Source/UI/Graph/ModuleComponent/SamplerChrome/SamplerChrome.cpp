// SamplerChrome.cpp -- construction, layout, the mode cross-fade and the gated tick. The picker, the choosers, the
// undo step and the file drop are in SamplerChromeLoad.cpp.
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerChrome.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"

namespace synth::ui {

namespace {

constexpr int kCaptionGap = 4;
constexpr int kCaptionHeight = 16;
constexpr int kStripGap = 2;
constexpr int kSectionGap = 8;
constexpr float kCaptionFontSize = 11.0f;
constexpr int kEditMapWidth = 72;
constexpr int kEditMapGap = 6;

using synth::cardbody::kRowHeight;
using synth::cardbody::kWaveformHeight;

} // namespace

SamplerChrome::SamplerChrome(Host host, SamplerModule& sampler)
    : host_(host)
    , sampler_(sampler)
    , waveform_(sampler)
    , strip_(sampler) {
    auto& card = *host_.card;
    loadButton_.setTooltip("Load an audio file (WAV, AIFF, FLAC, Ogg) into this Sampler");
    loadButton_.onClick = [this] { launchSampleChooser(); };
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setMinimumHorizontalScale(0.7f);
    caption_.setJustificationType(juce::Justification::centredLeft);
    caption_.setFont(juce::Font(juce::FontOptions(kCaptionFontSize)));
    caption_.setMinimumHorizontalScale(0.7f);
    picker_.onClick = [this] { openPicker(); };
    editMapButton_.setTitle("Edit map");
    editMapButton_.setTooltip("Edit map: see every recording on the keys and velocity layers, and change them");
    editMapButton_.onClick = [this] { openMap(); };
    strip_.onDoubleClick = [this] { openMap(); };

    for (juce::Component* part :
         {(juce::Component*)&waveform_, (juce::Component*)&loadButton_, (juce::Component*)&nameLabel_,
          (juce::Component*)&picker_, (juce::Component*)&editMapButton_, (juce::Component*)&caption_,
          (juce::Component*)&strip_})
        card.addChildComponent(*part);

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
                       (juce::Component*)&picker_, (juce::Component*)&editMapButton_, (juce::Component*)&caption_,
                       (juce::Component*)&strip_})
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
        refreshInstrument();
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
    picker_.setBounds(contentX, y, contentW - kEditMapWidth - kEditMapGap, kRowHeight);
    const int captionY = y + kRowHeight + kCaptionGap;
    caption_.setBounds(contentX, captionY, contentW, kCaptionHeight);
    const int stripY = captionY + kCaptionHeight + kStripGap;
    strip_.setBounds(contentX, stripY, contentW, y + total - kSectionGap - stripY);
    return y + total;
}

void SamplerChrome::poll() {
    if (sampler_.getInstrumentLoadSerial() != shownLoadSerial_ ||
        sampler_.getInstrumentGeneration() != shownGeneration_) {
        refreshInstrument(); // the instrument changed behind our back (undo, a project load, a restore that failed)
    }
    if (strip_.isVisible())
        strip_.poll();
    if (mapView_ != nullptr)
        mapView_->poll();
}

void SamplerChrome::refreshLoaded() {
    refreshSampleLabel();
    refreshInstrument();
}

void SamplerChrome::refreshSampleLabel(const juce::String& fallbackMessage) {
    const juce::String name = sampler_.getSampleName();
    nameLabel_.setText(fallbackMessage.isNotEmpty() ? fallbackMessage
                                                    : (name.isEmpty() ? juce::String("(no sample)") : name),
                       juce::dontSendNotification);
    nameLabel_.setTooltip(name);
    host_.card->repaint();
}

juce::String SamplerChrome::currentPackKey() const {
    if (auto* state = sampler_.getExtraState().getDynamicObject())
        return state->getProperty(synth::module_file_keys::kInstrumentPack).toString();
    return {};
}

void SamplerChrome::refreshInstrument() {
    shownGeneration_ = sampler_.getInstrumentGeneration();
    // A load attempt this card has not shown yet (one that landed before the card was built included) brings its
    // messages; the card's own loads set them in reportLoad first.
    if (const int serial = sampler_.getInstrumentLoadSerial(); serial != shownLoadSerial_) {
        loadMessages_ = sampler_.getInstrumentMessages();
        shownLoadSerial_ = serial;
    }
    const juce::String name = sampler_.getInstrumentName();
    picker_.setInstrument(name, currentPackKey().isNotEmpty() ? "built in" : "your files");
    strip_.refresh();
    if (!loadMessages_.isEmpty()) {
        const juce::String more =
            loadMessages_.size() > 1 ? " (+" + juce::String(loadMessages_.size() - 1) + " more)" : juce::String();
        caption_.setText(loadMessages_[0] + more, juce::dontSendNotification);
        caption_.setTooltip(loadMessages_.joinIntoString("\n"));
    } else {
        caption_.setText(strip_.getCaptionText(), juce::dontSendNotification);
        caption_.setTooltip({});
    }
    host_.card->repaint();
}

} // namespace synth::ui
