#pragma once

// StepGridCardFixture.h -- a Sequencer or Poly Sequencer card on a canvas with its step grid, for the card tests.
#include "../../Graph/CardBody/CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/CardViewState.h"
#include "Modules/Sequencer/SequencerModuleBase.h"
#include "UI/ModuleViews/StepGrid/StepGridComponent.h"

using namespace cardbody_test;
using synth::SequencerLane;
using synth::ui::StepGridComponent;

namespace stepgrid_test {

struct SeqCard {
    CardCanvas canvas;
    NodeID id;
    synth::SequencerModuleBase* module = nullptr;
    ModuleComponent* card = nullptr;
    StepGridComponent* grid = nullptr;

    explicit SeqCard(const char* type = "Sequencer", std::function<void(synth::SequencerModuleBase&)> prepare = {}) {
        auto processor = synth::AIStateMapper::createModule(type);
        module = dynamic_cast<synth::SequencerModuleBase*>(processor.get());
        if (prepare)
            prepare(*module);
        id = canvas.add(std::move(processor), 40, 40);
        canvas.editor.updateComponents();
        card = canvas.card(id);
        grid = card != nullptr && card->getCardBody() != nullptr
                   ? dynamic_cast<StepGridComponent*>(card->getCardBody()->findView(synth::CardView::StepGrid))
                   : nullptr;
        canvas.undo.getUndoManager().clearUndoHistory();
    }

    void setChoice(const char* paramId, const juce::String& text) {
        auto* param = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(module, paramId));
        ASSERT_NE(param, nullptr);
        const int index = param->choices.indexOf(text);
        ASSERT_GE(index, 0) << text;
        *param = index;
    }

    /** The module plays its own Key and Scale instead of following the song key. */
    void useOwnKey() {
        auto* follow = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(module, "followSongKey"));
        ASSERT_NE(follow, nullptr);
        *follow = false;
    }

    juce::Point<float> pointIn(SequencerLane lane, int step, float yFraction = 0.5f) const {
        const auto cell = grid->cellBounds(lane, step);
        return {(float)cell.getCentreX(), (float)cell.getY() + yFraction * (float)cell.getHeight()};
    }

    void mouse(const char* what, juce::Point<float> p,
               juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) {
        const auto e = mouseAt(*grid, p, mods);
        if (juce::String(what) == "down")
            grid->mouseDown(e);
        else if (juce::String(what) == "drag")
            grid->mouseDrag(e);
        else
            grid->mouseUp(e);
    }

    void press(int keyCode, juce::ModifierKeys mods = {}) { grid->keyPressed(juce::KeyPress(keyCode, mods, 0)); }
};

} // namespace stepgrid_test
