// MIDI Learn on the Drum Kit card's picked-pad knobs (Level, Tune, Decay, Start). The panel's knobs re-bind to the
// picked pad's parameters on every pick, so the card's learnable registry re-binds with them: a learn targets the pad
// that was picked when it was learned, and the controller keeps driving that pad after another one is picked. Driven
// through a real right-click on the knob and a fake controller (MidiRemoteWorkflowRig.h), then saved and reloaded.
//
// Suite name contains "MidiRemote" per the ship-task --gtest_filter convention.

#include "MidiRemoteWorkflowRig.h"

#include "Modules/DrumKit/DrumKitModule.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"

#include <gtest/gtest.h>

using namespace midi_remote_rig;
using PadParam = DrumKitModule::PadParam;

namespace {

constexpr const char* kKitUuid = "workflow-drum-kit-uuid";

DrumKitModule* kitOf(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == kKitUuid)
            return dynamic_cast<DrumKitModule*>(node->getProcessor());
    return nullptr;
}

synth::ui::DrumKitPadsSection* padsOf(Rig& rig) {
    for (auto* card : rig.editor.getModuleComponents())
        if (dynamic_cast<DrumKitModule*>(card->getModule()) != nullptr)
            if (auto* chrome = dynamic_cast<synth::ui::DrumKitChrome*>(card->getInstrumentChrome()))
                return &chrome->getPads();
    return nullptr;
}

ModuleComponent* kitCard(Rig& rig) {
    for (auto* card : rig.editor.getModuleComponents())
        if (dynamic_cast<DrumKitModule*>(card->getModule()) != nullptr)
            return card;
    return nullptr;
}

float normalised(juce::AudioProcessorParameter* param) { return param->getValue(); }

int padNamed(const DrumKitModule& kit, const juce::String& name) {
    for (int i = 0; i < kit.padCount(); ++i)
        if (kit.pad(i).name == name)
            return i;
    return -1;
}

// Picks `pad`, right-clicks its Level knob and learns the device's CC onto it. True when the learn settled.
bool learnPadLevel(Rig& rig, int pad) {
    auto* pads = padsOf(rig);
    auto* card = kitCard(rig);
    if (pads == nullptr || card == nullptr)
        return false;
    pads->grid().pick(pad, false);
    auto& knob = pads->controls().knob(PadParam::Level);
    auto* kit = dynamic_cast<DrumKitModule*>(card->getModule());
    const auto item = "MIDI Learn '" + kit->padParameter(pad, PadParam::Level)->getName(100) + "'...";
    if (!invokeMenuItem(rightClick(*card, knob), item) || !rig.controller.isArmed())
        return false;
    rig.hearDevice();
    for (const int v : {10, 40, 70, 100, 127}) {
        rig.sendCc(v);
        rig.advance(20.0);
    }
    rig.advance(kLearnSettleMs + 1.0);
    rig.remote.drain();
    return !rig.controller.isArmed() && rig.doc.assignments.size() == 1;
}

class MidiRemoteDrumPadLearnTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("agentsynth-midiremote-drum-pad-" + juce::Uuid().toString());
        root_.deleteRecursively();
        profileDir_ = root_.getChildFile("controllers");
        bundleDir_ = root_.getChildFile("Pads" + juce::String(ProjectBundle::kBundleExtension));
    }
    void TearDown() override { root_.deleteRecursively(); }

    std::unique_ptr<Rig> makeRigWithKit() {
        auto rig = std::make_unique<Rig>(profileDir_);
        rig->addModule(std::make_unique<DrumKitModule>(), kKitUuid);
        return rig;
    }

    juce::File root_, profileDir_, bundleDir_;
};

} // namespace

TEST_F(MidiRemoteDrumPadLearnTest, EveryPadKnobOffersLearnForThePickedPadAndFollowsThePick) {
    auto rig = makeRigWithKit();
    auto* kit = kitOf(rig->engine.getGraph());
    auto* pads = padsOf(*rig);
    auto* card = kitCard(*rig);
    ASSERT_NE(kit, nullptr);
    ASSERT_NE(pads, nullptr);
    ASSERT_NE(card, nullptr);
    const int kick = padNamed(*kit, "Kick");
    const int snare = padNamed(*kit, "Snare");
    ASSERT_GE(kick, 0);
    ASSERT_GE(snare, 0);

    pads->grid().pick(snare, false);
    for (const auto which : {PadParam::Level, PadParam::Tune, PadParam::Decay, PadParam::Start}) {
        auto& knob = pads->controls().knob(which);
        auto* param = kit->padParameter(snare, which);
        ASSERT_NE(param, nullptr);
        EXPECT_EQ(card->findMidiLearnableParamForTest(&knob), param) << param->paramID;
        EXPECT_TRUE(menuContains(rightClick(*card, knob), "MIDI Learn '" + param->getName(100) + "'..."))
            << param->paramID;
    }

    pads->grid().pick(kick, false);
    auto& level = pads->controls().knob(PadParam::Level);
    EXPECT_EQ(card->findMidiLearnableParamForTest(&level), kit->padParameter(kick, PadParam::Level))
        << "the knob's learn follows the pick";
    EXPECT_TRUE(level.getTooltip().startsWith("Level: Kick")) << level.getTooltip();
}

TEST_F(MidiRemoteDrumPadLearnTest, ALearnOnSnareLevelKeepsDrivingSnareAfterKickIsPicked) {
    auto rig = makeRigWithKit();
    auto* kit = kitOf(rig->engine.getGraph());
    ASSERT_NE(kit, nullptr);
    const int kick = padNamed(*kit, "Kick");
    const int snare = padNamed(*kit, "Snare");
    ASSERT_TRUE(learnPadLevel(*rig, snare));
    EXPECT_EQ(rig->doc.assignments[0].target.parameter.paramId, kit->padParameter(snare, PadParam::Level)->paramID);

    auto* pads = padsOf(*rig);
    auto* card = kitCard(*rig);
    auto& knob = pads->controls().knob(PadParam::Level);
    card->timerCallback();
    EXPECT_TRUE(card->isMidiLearnBadgeMappedForTest(&knob)) << "Snare's knob shows its mapping";

    pads->grid().pick(kick, false);
    card->timerCallback();
    EXPECT_FALSE(card->isMidiLearnBadgeMappedForTest(&knob)) << "Kick's level has no mapping";

    auto* snareLevel = kit->padParameter(snare, PadParam::Level);
    auto* kickLevel = kit->padParameter(kick, PadParam::Level);
    const float kickBefore = normalised(kickLevel);
    rig->letGestureExpire();
    rig->sendCc(20);
    EXPECT_NEAR(normalised(snareLevel), 20.0f / 127.0f, 1.0e-3f) << "the controller moves Snare's level";
    EXPECT_FLOAT_EQ(normalised(kickLevel), kickBefore) << "and leaves Kick's alone";
    EXPECT_FLOAT_EQ((float)knob.getValue(), kickLevel->get()) << "the picked Kick's knob does not move";
    rig->letGestureExpire();
}

TEST_F(MidiRemoteDrumPadLearnTest, APadKnobLearnSurvivesSaveAndReload) {
    juce::String assignmentId;
    int snare = -1;
    {
        auto rig = makeRigWithKit();
        snare = padNamed(*kitOf(rig->engine.getGraph()), "Snare");
        ASSERT_TRUE(learnPadLevel(*rig, snare));
        assignmentId = rig->doc.assignments[0].id;
        const auto saved = ProjectBundle::save(bundleDir_, rig->engine.getGraph(), rig->timeline,
                                               rig->editor.getPatchDocument(), rig->editor.getMacros(), rig->doc);
        ASSERT_TRUE(saved.ok) << saved.message;
    }

    Rig fresh(profileDir_);
    fresh.remote.setProfiles(fresh.controller.getProfiles());
    fresh.editor.detachAllModuleComponents();
    const auto loaded = ProjectBundle::load(bundleDir_, fresh.engine.getGraph(), fresh.timeline,
                                            fresh.editor.getPatchDocument(), fresh.editor.getMacros(), fresh.doc);
    ASSERT_TRUE(loaded.ok) << loaded.message;
    fresh.editor.updateComponents();
    fresh.controller.publishAssignments();
    fresh.hearDevice();

    auto* kit = kitOf(fresh.engine.getGraph());
    ASSERT_NE(kit, nullptr);
    auto* snareLevel = kit->padParameter(snare, PadParam::Level);
    const auto* slot = findSlot(fresh.remote, assignmentId);
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(slot->orphaned);
    EXPECT_EQ(slot->param, snareLevel);
    fresh.sendCc(64);
    EXPECT_NEAR(normalised(snareLevel), 64.0f / 127.0f, 1.0e-3f);
    fresh.letGestureExpire();
}
