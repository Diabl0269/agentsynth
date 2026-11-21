#pragma once

// MacroCountRideRig.h -- the canvas rig the Macros-count motion tests share (MacroCountKnobsRideTheGlideTests.cpp and
// the parts-move-with-their-container guard, PartsMoveWithContainerTests.cpp): a Macros bank on a canvas whose card
// glide, jack fade and knob swap all run as if the card were on screen, stepped by hand to the same elapsed time.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "../../../TestWait.h"
#include "../../Layout/FadeVisibilityTestGuard.h"
#include "AppUndoManager.h"
#include "Modules/MacroControlModule.h"
#include "UI/Graph/CardBody/CardBodySwapTestSeam.h"
#include "UI/Graph/CardBody/CardHeightGlide.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include <cmath>
#include <vector>

namespace macro_ride_test {

constexpr int kFrom = 8;
constexpr int kTo = 3;

/** Full motion, and every card's swap animating as if it were on screen (also a card an undo rebuilds). */
struct RideGuard : FadeAnimateGuard {
    RideGuard() { synth::cardbody_test::setAnimateSwapOffScreen(true); }
    ~RideGuard() { synth::cardbody_test::setAnimateSwapOffScreen(false); }
};

struct Rig {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};

    Rig() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
        editor.getCardGlideForTest().setForceAnimateForTest(true);
    }
    NodeID bank(int count) {
        auto module = std::make_unique<MacroControlModule>();
        setCount(*module, count);
        return addModuleAt(editor, engine, std::move(module), 400, 300);
    }
    ModuleComponent* card(NodeID id) { return findComponent(editor, id); }
    static juce::AudioParameterInt* countParam(juce::AudioProcessor& module) {
        for (auto* p : module.getParameters())
            if (auto* i = dynamic_cast<juce::AudioParameterInt*>(p); i != nullptr && i->paramID == "macroCount")
                return i;
        return nullptr;
    }
    static void setCount(juce::AudioProcessor& module, int count) {
        countParam(module)->setValueNotifyingHost(countParam(module)->convertTo0to1(count));
    }
    /** What the Controllers arrows do: one parameter gesture, then the card's async callbacks run. */
    void arrowTo(NodeID id, int count) {
        auto* param = countParam(*engine.getGraph().getNodeForId(id)->getProcessor());
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(count));
        param->endChangeGesture();
        synth::test::flushMessageQueue();
    }
    static int heightAt(int count) {
        Rig other;
        return other.card(other.bank(count))->getHeight();
    }
    /** The card glide, the knob swap and the jack fade, all `ms` into the change. */
    void stepTo(NodeID id, double ms) {
        card(id)->heightGlideForTest().stepForTest(ms);
        synth::cardbody_test::stepSwap(*card(id), ms);
        if (auto* jacks = card(id)->heightGlideForTest().jacks())
            jacks->stepForTest(ms);
    }
    void settle(NodeID id) {
        stepTo(id, 1000.0);
        editor.finishCardGlideForTest();
    }
};

struct Knob {
    juce::String id;
    juce::Component* widget;
    juce::Component* label;
};

/** The knobs of controllers `lo + 1` .. `hi`: the ones a change between the two counts adds or takes away. */
inline std::vector<Knob> knobsBetween(Rig& rig, NodeID id, int lo, int hi) {
    std::vector<Knob> knobs;
    for (int n = lo + 1; n <= hi; ++n) {
        const auto paramId = "macro" + juce::String(n);
        if (const auto parts = synth::cardbody_test::controlParts(*rig.card(id), paramId))
            knobs.push_back({paramId, parts->widget, parts->label});
    }
    return knobs;
}

inline double scaleOf(const juce::Component& c) {
    const auto t = c.getTransform();
    return std::min((double)std::abs(t.mat00), (double)std::abs(t.mat11));
}

inline double glideMs() { return synth::CardHeightGlide::durationMs(); }

} // namespace macro_ride_test
