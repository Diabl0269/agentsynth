#include "UI/ModuleViews/SequencerKey/SequencerKeyField.h"
#include "Transport/TransportService.h"

namespace synth::ui {

namespace {

void setParam(juce::AudioProcessor& module, const char* id, float plainValue) {
    if (auto* param = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id)))
        param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

} // namespace

SequencerKeyField::SequencerKeyField(SequencerModuleBase& module)
    : module_(module) {
    setComponentID("sequencerKey");
    onPickOwn = [this](std::optional<MusicalScale> key) { pickOwn(key); };
    onFollow = [this] { follow(); };
}

// The song key comes from the app's transport, never the module's playhead: a node gets its playhead only once the
// graph renders, so a card built on load or with the audio device off would read No key and then glow.
void SequencerKeyField::refresh() {
    const auto songKey = transport_ != nullptr ? transport_->getDocumentState().songKey : std::nullopt;
    setState(module_.followsSongKey(), songKey, module_.getOwnKey());
}

void SequencerKeyField::runEdit(const std::function<void()>& edit) {
    if (recordEdit_)
        recordEdit_(edit);
    else
        edit();
}

// The values are written without a change gesture, so the card's per-parameter undo stays out of it and the caller's
// one snapshot covers all of them. A scale the Scale list does not hold goes in as Custom with its mask.
void SequencerKeyField::pickOwn(const std::optional<MusicalScale>& key) {
    const auto values = SequencerModuleBase::ownKeyValuesFor(key);
    runEdit([this, values] {
        namespace ids = sequencer_ids;
        if (values.customMask.has_value() && module_.getPattern().customMask != *values.customMask) {
            auto pattern = module_.getPattern();
            pattern.customMask = *values.customMask;
            module_.setPattern(pattern);
        }
        setParam(module_, ids::kScaleRoot, (float)values.root);
        setParam(module_, ids::kScale, (float)values.scaleIndex);
        setParam(module_, ids::kFollowSongKey, 0.0f);
    });
    refresh();
}

void SequencerKeyField::follow() {
    runEdit([this] { setParam(module_, sequencer_ids::kFollowSongKey, 1.0f); });
    refresh();
}

} // namespace synth::ui
