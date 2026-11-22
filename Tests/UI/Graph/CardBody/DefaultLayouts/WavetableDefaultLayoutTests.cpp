// WavetableDefaultLayoutTests.cpp
//
// The Wavetable's designed default card layout drawn by a real card on a canvas with no override: the lane
// grid (a mode row with Warp, Sync, the Sub switches and the Poly and Show Scope pills; Position as the hero; Pitch,
// Shape, Voice and the Out lane with Pan and Level), every control placed where its lane says, the jacks of the lane
// knobs bound to them, the size estimate against the real card, and no tab strip left.
// Source/UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsSources.cpp, docs/layout/module-card-layout.md.

#include "../../../Accessibility/AccessibilityAudit.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include <set>

using namespace cardbody_test;

namespace {

struct Wavetable {
    NodeID id;
    ModuleComponent* card = nullptr;
    synth::CardBody* body = nullptr;
};

Wavetable build(CardCanvas& canvas, int x = 0) {
    Wavetable w;
    w.id = canvas.add(synth::AIStateMapper::createModule("Wavetable"), x, 0);
    canvas.editor.updateComponents();
    w.card = canvas.card(w.id);
    w.body = w.card->getCardBody();
    return w;
}

juce::Component* widget(const Wavetable& w, const juce::String& paramId) { return w.body->findWidget(paramId); }

// The id of the section a control is in ("<more>" for one in the More row, "<none>" for an unknown parameter).
juce::String sectionOf(const Wavetable& w, const juce::String& paramId) {
    const auto& plan = w.body->getPlan();
    const int item = plan.findParam(paramId);
    if (item < 0)
        return "<none>";
    const int section = plan.items[(size_t)item].section;
    return section < 0 ? "<more>" : plan.sections[(size_t)section].id;
}

} // namespace

TEST(WavetableDefaultLayout, TheLanesRunModeHeroPitchShapeVoiceOut) {
    CardCanvas canvas;
    const auto w = build(canvas);
    const auto& plan = w.body->getPlan();
    juce::StringArray ids;
    for (const auto& section : plan.sections)
        ids.add(section.id);
    EXPECT_EQ(ids.joinIntoString(","), "mode,hero,pitch,shape,voice,out");
    EXPECT_TRUE(plan.tabGroups.empty()) << "no tabs any more";
    EXPECT_EQ(w.body->getTabStrip(0), nullptr);
}

TEST(WavetableDefaultLayout, EachControlSitsInItsLane) {
    CardCanvas canvas;
    const auto w = build(canvas);
    for (const auto* id : {"warp", "syncMode", "subOctave", "subShape", "poly"})
        EXPECT_EQ(sectionOf(w, id), "mode") << id;
    EXPECT_EQ(sectionOf(w, "position"), "hero");
    for (const auto* id : {"octave", "coarse", "fine"})
        EXPECT_EQ(sectionOf(w, id), "pitch") << id;
    for (const auto* id : {"warpAmount", "phase", "randomPhase"})
        EXPECT_EQ(sectionOf(w, id), "shape") << id;
    for (const auto* id : {"unison", "detune", "width", "blend", "spread", "subLevel"})
        EXPECT_EQ(sectionOf(w, id), "voice") << id;
    EXPECT_EQ(sectionOf(w, "pan"), "out");
    EXPECT_EQ(sectionOf(w, "level"), "out");
    for (const auto* id : {"stack", "importMode", "interpolation"})
        EXPECT_EQ(sectionOf(w, id), "<more>") << id << " stays reachable in the More row";
    EXPECT_TRUE(w.body->hasMoreRow());
    EXPECT_EQ(w.body->findWidget("table"), nullptr) << "Table is the chrome's, beside the display";
}

TEST(WavetableDefaultLayout, EveryControlIsInsideTheCardAndTheOutLaneIsLastAndLowest) {
    CardCanvas canvas;
    const auto w = build(canvas);
    for (const auto& item : w.body->getPlan().items)
        if (item.widget != nullptr && item.widget->isVisible())
            EXPECT_TRUE(w.card->getLocalBounds().contains(item.widget->getBounds())) << item.param->paramID;
    const int outBottom = widget(w, "level")->getBottom();
    for (const auto* id : {"position", "octave", "warpAmount", "unison", "detune"})
        EXPECT_LE(widget(w, id)->getBottom(), outBottom) << id;
    EXPECT_GT(widget(w, "level")->getX(), widget(w, "unison")->getX()) << "the Out lane is right of the Voice lane";
    EXPECT_GT(widget(w, "poly")->getX(), widget(w, "warp")->getX()) << "Poly sits right of the mode selector";
    EXPECT_LT(widget(w, "poly")->getY(), widget(w, "position")->getY()) << "the mode row is above the lanes";
}

// A knob on the lanes binds its CV jack, so only the few jacks of controls the card does not show as knobs stay in the
// gutter, in at most two left-hand columns; Position's jack lands on its own knob.
TEST(WavetableDefaultLayout, TheLaneKnobsTakeTheirJacks) {
    CardCanvas canvas;
    const auto w = build(canvas);
    const auto drawn = w.card->drawnInputJackIndices();
    EXPECT_LT((int)drawn.size(), WavetableOscillatorModule::kNumJacks - 2);
    EXPECT_TRUE(w.card->isInputJackKnobBound(WavetableOscillatorModule::kJackPosition));
    EXPECT_EQ(w.card->getPortCenter(WavetableOscillatorModule::kJackPosition, true).y,
              juce::roundToInt(w.card->getModTargetKnobAnchor(WavetableOscillatorModule::kJackPosition)->y));
    std::set<int> columns;
    for (int jack : drawn)
        columns.insert(w.card->getPortCenter(jack, true).x);
    EXPECT_LE(columns.size(), 2u);
}

TEST(WavetableDefaultLayout, TheEstimateEqualsTheRealCard) {
    CardCanvas canvas;
    const auto w = build(canvas);
    const juce::Point<int> real(w.card->getWidth(), w.card->getHeight());
    const auto measured = synth::measureDataDrivenCardSize("Wavetable");
    ASSERT_TRUE(measured.has_value()) << "the Wavetable is measured from its plan, not a table";
    EXPECT_EQ(*measured, real);
    EXPECT_EQ(GraphEditor::estimateModuleSize("Wavetable"), real);
    const auto g = synth::cardbody::BodyGeometry::forCardWidth(w.card->getWidth());
    EXPECT_EQ(w.body->layout(300, g, false), w.body->layout(300, g, true));
}

TEST(WavetableDefaultLayout, NothingAboutTheCardReachesThePatch) {
    CardCanvas canvas;
    const auto w = build(canvas);
    EXPECT_TRUE(synth::getCardLayoutOverride(canvas.engine.getGraph(), w.id).isVoid());
    auto* module = dynamic_cast<ModuleBase*>(canvas.processor(w.id));
    EXPECT_TRUE(module->getCardViewState().isDefault());
    EXPECT_TRUE(synth::test::auditAccessibility(*w.card).empty()) << "every control on the card is named";
}
