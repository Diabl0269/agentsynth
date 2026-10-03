// CardLayoutOnCardEditorTimeTempo.cpp -- the edit bar's "Time and tempo" switch on an ADSR card: showing the
// mode the card's layout is in, and writing the other one at once. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/DefaultLayouts/AdsrTimeTempo.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth::ui {

// Only an ADSR card whose layout still has its stages (in either mode) shows the switch, on that mode.
void CardLayoutOnCardEditor::refreshTimeTempo() {
    const auto* body = card_ != nullptr ? card_->getCardBody() : nullptr;
    std::optional<int> index;
    if (source_ != nullptr && body != nullptr && hasAdsrTimeTempo(source_->moduleType()))
        if (const auto mode = adsrTimeTempoOf(body->explicitLayout()))
            index = *mode == AdsrTimeTempo::Separate ? 1 : 0;
    const bool shown = editBar_.hasTimeTempo();
    editBar_.setTimeTempo(index);
    if (shown != editBar_.hasTimeTempo())
        resized();
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
    announce(separate ? "Stages shown as separate Time and Tempo groups" : "Stages shown once");
}

} // namespace synth::ui
