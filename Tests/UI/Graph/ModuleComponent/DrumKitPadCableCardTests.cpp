// DrumKitPadCableCardTests.cpp
// A cable dropped on the Drum Kit card's pad knob binds to the picked pad's parameter: the knob rings while it shows
// that pad, picking another pad takes the ring away while the cable keeps moving the first, and picking back
// brings the ring again. Undo of the drop takes the cable and frees the slot on the next rebuild.

#include "../CardBody/CardCanvas.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"

using namespace cardbody_test;
using synth::ui::DrumKitChrome;
using synth::ui::DrumKitPadsSection;
using PadParam = DrumKitModule::PadParam;

namespace {

constexpr int kBase = DrumKitModule::kPadModSlotBase;

struct PadCableScene {
    CardCanvas canvas;
    NodeID lfoId, kitId;

    PadCableScene() {
        lfoId = canvas.add(std::make_unique<LFOModule>(), 0, 0);
        kitId = canvas.add(std::make_unique<DrumKitModule>(), 700, 0);
        canvas.editor.updateComponents();
        canvas.card(lfoId)->setTopLeftPosition(0, 0);
        canvas.card(kitId)->setTopLeftPosition(700, 0);
    }
    DrumKitModule& kit() { return *dynamic_cast<DrumKitModule*>(canvas.processor(kitId)); }
    ModuleComponent& card() { return *canvas.card(kitId); }
    DrumKitPadsSection& pads() { return dynamic_cast<DrumKitChrome*>(card().getInstrumentChrome())->getPads(); }

    void pick(int pad) {
        auto& grid = pads().grid();
        grid.mouseDown(makeModuleClickWithMods(grid, grid.padBounds(pad).getCentre(),
                                               juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier)));
        canvas.editor.updateComponents();
    }
    juce::Slider& levelKnob() { return pads().controls().knob(PadParam::Level); }
    juce::Point<int> levelKnobPoint() {
        return card().getPosition() + card().getLocalPoint(&levelKnob(), levelKnob().getLocalBounds().getCentre());
    }
    bool routedTo(int channel) {
        for (const auto& routing : canvas.engine.getModulationRoutings())
            if (routing.sourceNodeID == lfoId && routing.destNodeID == kitId && routing.destChannelIndex == channel)
                return true;
        return false;
    }
    void dropOn(juce::Point<int> point) {
        canvas.editor.beginConnectionDrag(canvas.card(lfoId), 0, /*isInput*/ false, /*isMidi*/ false, {0, 0});
        canvas.editor.dragConnection(point);
        canvas.editor.endConnectionDrag(point);
    }
    // The ring a pad cable draws: the knob the slot's target resolves to on the card now.
    juce::Slider* ringKnob(const juce::String& paramId) {
        for (const auto& target : kit().getModulationTargets())
            if (target.paramId == paramId)
                return card().padKnobForTarget(target).slider;
        return nullptr;
    }
};

} // namespace

TEST(DrumKitPadCableCard, ACableDroppedOnThePickedPadsLevelKnobBindsThatPadAndTheRingFollowsThePick) {
    PadCableScene scene;
    scene.pick(1); // Snare
    ASSERT_EQ(scene.pads().controls().boundPad(), 1);

    scene.dropOn(scene.levelKnobPoint());

    ASSERT_EQ(scene.kit().modChannelForParam("pad2Level"), kBase) << "the drop claimed a slot for the snare's Level";
    EXPECT_TRUE(scene.routedTo(kBase));
    EXPECT_EQ(scene.ringKnob("pad2Level"), &scene.levelKnob()) << "the snare's knob rings";

    scene.pick(0); // Kick
    EXPECT_EQ(scene.kit().modChannelForParam("pad2Level"), kBase) << "the cable stays on the snare";
    EXPECT_TRUE(scene.routedTo(kBase));
    EXPECT_EQ(scene.ringKnob("pad2Level"), nullptr) << "the kick's knob shows no ring";
    EXPECT_EQ(scene.kit().modChannelForParam("pad1Level"), -1);

    scene.pick(1);
    EXPECT_EQ(scene.ringKnob("pad2Level"), &scene.levelKnob()) << "the ring is back";
}

TEST(DrumKitPadCableCard, UndoingTheDropRemovesTheCableAndTheNextRebuildFreesTheSlot) {
    PadCableScene scene;
    scene.pick(1);
    scene.dropOn(scene.levelKnobPoint());
    ASSERT_GE(scene.kit().modChannelForParam("pad2Level"), 0);

    scene.canvas.undo.undo();
    scene.canvas.editor.updateComponents();

    EXPECT_FALSE(scene.routedTo(kBase));
    EXPECT_EQ(scene.kit().modChannelForParam("pad2Level"), -1);
    EXPECT_TRUE(scene.kit().getExtraState().isVoid());
}

TEST(DrumKitPadCableCard, ADropOnAKitWithEverySlotTakenIsRefusedAndNamesTheLimit) {
    PadCableScene scene;
    scene.pick(1); // a rebuild frees slots no cable reaches, so fill the kit after the pick
    for (int pad = 3; pad <= 10; ++pad)
        for (const char* field : {"Level", "Tune", "Decay", "Start"})
            scene.kit().bindModSlot("pad" + juce::String(pad) + field);
    juce::String message;
    scene.canvas.editor.onStatusMessage = [&message](const juce::String& text) { message = text; };

    scene.dropOn(scene.levelKnobPoint());

    EXPECT_EQ(scene.kit().modChannelForParam("pad2Level"), -1);
    EXPECT_EQ(message, scene.card().cardTitle() + " already has 32 modulated pad knobs");
}
