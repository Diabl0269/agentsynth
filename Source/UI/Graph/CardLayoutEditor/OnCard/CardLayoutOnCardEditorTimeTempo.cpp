// CardLayoutOnCardEditorTimeTempo.cpp -- the strip's "Controls" switch on an ADSR card (Shared: one set of
// controls for the Time and Tempo looks; Separate: each look its own): showing the mode the card's layout is
// in, and writing the other one at once; and the card's own Sync switch, which stays usable under the overlay
// to pick the look being edited and is put back as it was when the session ends.
// docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/DefaultLayouts/AdsrTimeTempo.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth::ui {

// Only an ADSR card whose layout still has its stages (in either mode) shows the switch, on that mode.
void CardLayoutOnCardEditor::refreshTimeTempo() {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    bool shown = false;
    int index = 0;
    if (source_ != nullptr && body != nullptr && hasAdsrTimeTempo(source_->moduleType()))
        if (const auto mode = adsrTimeTempoOf(body->explicitLayout())) {
            shown = true;
            index = *mode == AdsrTimeTempo::Separate ? 1 : 0;
        }
    if (shown)
        timeTempo_.setSelectedIndex(index, juce::dontSendNotification);
    if (shown != timeTempoFade_.isShown()) {
        timeTempoFade_.setShown(shown);
        resized();
    }
}

// Written like every other edit of the session, so Cancel undoes it and Done keeps it in the one undo step.
void CardLayoutOnCardEditor::chooseTimeTempo(int index) {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    if (closed_ || source_ == nullptr || body == nullptr)
        return;
    flushNudge();
    const auto before = body->explicitLayout();
    const bool separate = index == 1;
    auto after = withAdsrTimeTempo(before, separate ? AdsrTimeTempo::Separate : AdsrTimeTempo::Shared);
    if (after == before)
        return;
    writeLayout(after);
    announce(separate ? "Time and Tempo each have their own controls" : "Time and Tempo share one set of controls");
}

bool CardLayoutOnCardEditor::hasSyncLooks() const {
    return source_ != nullptr && hasAdsrTimeTempo(source_->moduleType());
}

juce::RangedAudioParameter* CardLayoutOnCardEditor::syncParameter() const {
    auto* card = findCard();
    auto* module = card != nullptr ? card->getModule() : nullptr;
    return module != nullptr ? findParameterByID(module, "tempoSync") : nullptr;
}

// The Sync value the card had as the session opened, for Done and Cancel to put back.
void CardLayoutOnCardEditor::rememberSync() {
    openingSync_.reset();
    if (hasSyncLooks())
        if (auto* sync = syncParameter())
            openingSync_ = sync->getValue();
}

// Looking at the other look to edit it is not a change to the card: whichever way the session ends, Sync is
// as it was. Not an undo step (the layout's own writes are).
void CardLayoutOnCardEditor::restoreSync() {
    if (!openingSync_.has_value())
        return;
    if (auto* sync = syncParameter(); sync != nullptr && sync->getValue() != *openingSync_)
        sync->setValueNotifyingHost(*openingSync_);
}

// The keyboard's way to the card's own Sync switch, which the overlay covers.
void CardLayoutOnCardEditor::flipSync() {
    if (auto* sync = syncParameter())
        sync->setValueNotifyingHost(sync->getValue() > 0.5f ? 0.0f : 1.0f);
}

} // namespace synth::ui
