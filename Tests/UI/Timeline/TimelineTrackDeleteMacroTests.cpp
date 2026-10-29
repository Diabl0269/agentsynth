// TimelineTrackDeleteMacroTests.cpp: deleting a track also removes the macro it lives in and the modules only it used,
// in the same undo step, and Cmd+Z brings all of it back in place with nothing extra on the canvas. Drives a real
// off-screen MainComponent through the row menu's Delete Track item, then the app's undo manager, so the real command
// path runs; the unit rule is in Timeline/TrackRemovalSetTests.cpp. The second half is the question asked when the
// macro a track plays is deleted (menu, Delete / Backspace on the selected card, Cut): Remove track, Keep track,
// Cancel and Remember my choice, run against the shared saved project (fixtures/saved-project) and a two-track macro.
#include "../../UniqueTempDir.h"
#include "../../fixtures/saved-project/SavedProjectFixture.h"
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Mixer/ChannelMacroLookup.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/LFOModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "ProjectBundle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Timeline/DeleteTrackConfirm.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>
#include <map>
#include <set>

namespace {

class MockProviderTDM : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockTDM"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// Everything an undo has to give back, as comparable values.
struct State {
    std::map<juce::String, std::pair<int, int>> positions; // node uuid -> x, y
    std::set<juce::String> nodes;
    std::set<juce::String> cables; // "srcUuid:ch>dstUuid:ch"
    juce::var macros;
    juce::var timeline;
};

State captureState(MainComponent& mc, bool nameAll = false) {
    auto& graph = mc.getAudioEngine().getGraph();
    // A node of an opened project has no uuid until a snapshot first names it (ensureNodeUuid); name them all now so a
    // before and an after compare the same nodes.
    const auto uuidOf = [nameAll](juce::AudioProcessorGraph::Node* node) {
        if (node == nullptr)
            return juce::String();
        return nameAll ? synth::AIStateMapper::ensureNodeUuid(node) : node->properties["uuid"].toString();
    };
    State s;
    for (auto* node : graph.getNodes()) {
        const auto uuid = uuidOf(node);
        s.nodes.insert(uuid);
        s.positions[uuid] = {(int)node->properties.getWithDefault("x", 0),
                             (int)node->properties.getWithDefault("y", 0)};
    }
    for (const auto& c : graph.getConnections())
        s.cables.insert(uuidOf(graph.getNodeForId(c.source.nodeID)) + ":" + juce::String(c.source.channelIndex) + ">" +
                        uuidOf(graph.getNodeForId(c.destination.nodeID)) + ":" +
                        juce::String(c.destination.channelIndex));
    s.macros = mc.getGraphEditor().getMacros().toVar();
    s.timeline = mc.getTimelineDoc().toVar();
    return s;
}

struct Rig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderTDM>()};

    explicit Rig(int tracks = 2) {
        mc.setSize(1400, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        for (int i = 0; i < tracks; ++i)
            mc.simulateAddAudioTrackClick();
        mc.getTimelinePanel().setSize(1400, 360);
    }

    juce::AudioProcessorGraph& graph() { return mc.getAudioEngine().getGraph(); }
    GraphEditor& editor() { return mc.getGraphEditor(); }
    synth::MacroSet& macros() { return editor().getMacros(); }
    synth::TimelineDoc& doc() { return mc.getTimelineDoc(); }
    AppUndoManager& undo() { return mc.getUndoManager(); }

    static juce::String uuidOf(const juce::AudioProcessorGraph::Node* node) {
        return node != nullptr ? node->properties["uuid"].toString() : juce::String();
    }
    juce::AudioProcessorGraph::Node* nodeByUuid(const juce::String& uuid) {
        for (auto* node : graph().getNodes())
            if (uuidOf(node) == uuid)
                return node;
        return nullptr;
    }
    juce::String boundUuid(int trackIndex) { return doc().getTracks()[(size_t)trackIndex].bindingUuid; }
    const synth::Macro* macroOfTrack(int trackIndex) { return macros().findByMember(boundUuid(trackIndex)); }
    // The node of type `name` inside the track's macro.
    juce::AudioProcessorGraph::Node* memberNamed(int trackIndex, const juce::String& name) {
        const auto* macro = macroOfTrack(trackIndex);
        if (macro == nullptr)
            return nullptr;
        for (const auto& uuid : macro->members)
            if (auto* node = nodeByUuid(uuid); node != nullptr && node->getProcessor()->getName() == name)
                return node;
        return nullptr;
    }

    // An LFO with a position of its own, modulating `paramId` of `target`.
    juce::AudioProcessorGraph::Node::Ptr addLfoInto(juce::AudioProcessorGraph::Node& target,
                                                    const juce::String& paramId, int x, int y) {
        auto lfo = graph().addNode(synth::AIStateMapper::createModule("LFO"));
        const auto uuid = juce::Uuid().toDashedString();
        lfo->properties.set("uuid", uuid);
        lfo->properties.set("x", x);
        lfo->properties.set("y", y);
        if (auto* module = dynamic_cast<ModuleBase*>(lfo->getProcessor()))
            module->setNodeUuid(uuid);
        editor().updateComponents();
        const int raw = editor().modulationChannelFor(target.nodeID, paramId);
        EXPECT_GE(raw, 0);
        EXPECT_NE(editor().connectModulationSource(lfo->nodeID, 0, target.nodeID, raw, 0.5f).uid, 0u);
        return lfo;
    }

    int countOf(const juce::String& name) {
        int n = 0;
        for (auto* node : graph().getNodes())
            n += node->getProcessor()->getName() == name ? 1 : 0;
        return n;
    }

    State state() { return captureState(mc); }

    void deleteTrackRow(int index) {
        auto* header = mc.getTimelinePanel().getTrackHeaderAt(index);
        ASSERT_NE(header, nullptr);
        header->applyContextMenuChoice(synth::ui::TimelineTrackHeaderComponent::kDeleteTrackMenuId);
    }

    // Cards that show on the canvas by themselves: not inside a macro and not part of the output dock.
    std::set<juce::String> freeStandingCards() {
        std::set<juce::String> out;
        for (auto* comp : editor().getModuleComponents()) {
            auto* node = graph().getNodeForId(comp->getNodeId());
            if (comp == nullptr || node == nullptr || !comp->isVisible() ||
                editor().isOutputDockNode(comp->getNodeId()))
                continue;
            out.insert(uuidOf(node));
        }
        return out;
    }
};

void expectSameState(const State& a, const State& b) {
    EXPECT_EQ(a.nodes, b.nodes);
    EXPECT_EQ(a.positions, b.positions) << "every node back at its old position";
    EXPECT_EQ(a.cables, b.cables) << "every cable back";
    EXPECT_TRUE(synth::sameJson(a.macros, b.macros)) << "the macro back with the same members, ports, place and fold";
    EXPECT_TRUE(synth::sameJson(a.timeline, b.timeline)) << "the track back with its lanes and automation";
}

} // namespace

TEST(TimelineTrackDeleteMacro, TheTracksMacroAndItsCardsLeaveWithIt) {
    Rig rig;
    ASSERT_NE(rig.macroOfTrack(0), nullptr);
    const auto victimMacro = rig.macroOfTrack(0)->id;
    const auto keeperMacro = rig.macroOfTrack(1)->id;
    const auto victimUuid = rig.boundUuid(0);
    const int compressorsBefore = rig.countOf("Compressor");

    rig.deleteTrackRow(0);

    EXPECT_EQ(rig.doc().getTracks().size(), 1u);
    EXPECT_EQ(rig.macros().find(victimMacro), nullptr) << "no macro box is left behind";
    EXPECT_NE(rig.macros().find(keeperMacro), nullptr);
    EXPECT_EQ(rig.nodeByUuid(victimUuid), nullptr);
    EXPECT_EQ(rig.countOf("Compressor"), compressorsBefore - 1) << "its Gate, EQ and Compressor went with the macro";
    EXPECT_EQ(rig.macros().size(), 1);
}

TEST(TimelineTrackDeleteMacro, AnLfoOnlyThatTrackUsedGoesAndOneAnotherTrackAlsoUsesStays) {
    Rig rig;
    auto* ownCompressor = rig.memberNamed(0, "Compressor");
    auto* otherCompressor = rig.memberNamed(1, "Compressor");
    ASSERT_NE(ownCompressor, nullptr);
    ASSERT_NE(otherCompressor, nullptr);
    auto onlyThis = rig.addLfoInto(*ownCompressor, "threshold", 40, 40);
    auto shared = rig.addLfoInto(*rig.memberNamed(0, "Compressor"), "ratio", 40, 200);
    ASSERT_NE(rig.editor()
                  .connectModulationSource(
                      shared->nodeID, 0, rig.memberNamed(1, "Compressor")->nodeID,
                      rig.editor().modulationChannelFor(rig.memberNamed(1, "Compressor")->nodeID, "ratio"), 0.5f)
                  .uid,
              0u);
    const auto onlyThisUuid = Rig::uuidOf(onlyThis.get());
    const auto sharedUuid = Rig::uuidOf(shared.get());
    const int attenuvertersBefore = rig.countOf("Attenuverter");
    ASSERT_EQ(attenuvertersBefore, 3);

    rig.deleteTrackRow(0);

    EXPECT_EQ(rig.nodeByUuid(onlyThisUuid), nullptr) << "wired only to the deleted track";
    ASSERT_NE(rig.nodeByUuid(sharedUuid), nullptr) << "another track uses it too";
    EXPECT_EQ(rig.countOf("Attenuverter"), 1)
        << "its link to the other track is intact, the links into the deleted one are gone";

    ASSERT_TRUE(rig.undo().undo());
    EXPECT_NE(rig.nodeByUuid(onlyThisUuid), nullptr);
    EXPECT_EQ(rig.countOf("Attenuverter"), attenuvertersBefore) << "the link to the first track is back";
}

TEST(TimelineTrackDeleteMacro, ALonelyModuleWithNoCableIsLeftAlone) {
    Rig rig;
    auto loner = rig.graph().addNode(synth::AIStateMapper::createModule("LFO"));
    loner->properties.set("uuid", juce::Uuid().toDashedString());
    rig.editor().updateComponents();
    rig.deleteTrackRow(0);
    EXPECT_NE(rig.nodeByUuid(Rig::uuidOf(loner.get())), nullptr);
}

TEST(TimelineTrackDeleteMacro, OneUndoBringsEverythingBackInPlaceAndOneRedoRemovesItAgain) {
    Rig rig;
    auto* compressor = rig.memberNamed(0, "Compressor");
    auto lfo = rig.addLfoInto(*compressor, "threshold", 40, 40);
    ASSERT_NE(lfo, nullptr);
    const auto before = rig.state();
    const auto cardsBefore = rig.freeStandingCards();
    rig.undo().beginNewTransaction();

    rig.deleteTrackRow(0);
    const auto deleted = rig.state();
    EXPECT_LT(deleted.nodes.size(), before.nodes.size());

    ASSERT_TRUE(rig.undo().undo()) << "one step";
    expectSameState(before, rig.state());
    EXPECT_EQ(rig.freeStandingCards(), cardsBefore) << "no extra node sits on the canvas";
    const auto* macro = rig.macroOfTrack(0);
    ASSERT_NE(macro, nullptr) << "the track's node is inside its macro again";
    EXPECT_TRUE(macro->collapsed);
    EXPECT_EQ(rig.freeStandingCards().count(rig.boundUuid(0)), 0u) << "not a loose node over its macro";

    ASSERT_TRUE(rig.undo().redo());
    expectSameState(deleted, rig.state());
    ASSERT_TRUE(rig.undo().undo());
    expectSameState(before, rig.state());
}

TEST(TimelineTrackDeleteMacro, UndoLeavesNoLooseNodeEvenWithoutAnyModulator) {
    Rig rig;
    const auto before = rig.state();
    const auto cardsBefore = rig.freeStandingCards();
    rig.deleteTrackRow(1);
    ASSERT_TRUE(rig.undo().undo());
    expectSameState(before, rig.state());
    EXPECT_EQ(rig.freeStandingCards(), cardsBefore);
    ASSERT_NE(rig.macroOfTrack(1), nullptr);
    EXPECT_TRUE(rig.macroOfTrack(1)->hasMember(rig.boundUuid(1)));
}

// The project as a fresh app opens it after a save, so a restore that left something the file format drops, or a node
// outside its macro, shows up as a difference from the same save taken before the delete.
static State savedAndReopened(Rig& rig, bool* freeStandingMatches = nullptr) {
    auto dir = synth::test::uniqueTempPath("fro656-project", ".agsproj");
    EXPECT_TRUE(rig.mc.saveProjectForTest(dir));
    Rig loaded(0);
    EXPECT_TRUE(loaded.mc.openProjectForTest(dir));
    if (freeStandingMatches != nullptr)
        *freeStandingMatches = loaded.freeStandingCards() == rig.freeStandingCards();
    dir.deleteRecursively();
    return loaded.state();
}

TEST(TimelineTrackDeleteMacro, ASavedProjectAfterDeleteAndUndoOpensAsItDidWithoutTheDelete) {
    Rig rig;
    rig.addLfoInto(*rig.memberNamed(0, "Compressor"), "threshold", 40, 40);
    const auto before = rig.state();
    const auto reopenedBefore = savedAndReopened(rig);
    rig.deleteTrackRow(0);
    ASSERT_TRUE(rig.undo().undo());
    expectSameState(before, rig.state());

    bool sameCards = false;
    const auto reopenedAfter = savedAndReopened(rig, &sameCards);
    expectSameState(reopenedBefore, reopenedAfter);
    EXPECT_TRUE(sameCards);
}

TEST(TimelineTrackDeleteMacro, TheMacroCardAndTheRowAnimateTogetherAndUndoReversesThem) {
    ReducedMotionGuard guard(false);
    Rig rig;
    rig.mc.getTimelinePanel().forceTrackGlideForTest(true);
    auto& glide = rig.editor().getCardGlideForTest();
    glide.setForceAnimateForTest(true);
    ASSERT_NE(rig.macroOfTrack(0), nullptr);

    rig.deleteTrackRow(0);
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning()) << "the row shrinks";
    EXPECT_GE(glide.exitGhostCount(), 1) << "and the macro card shrinks with it";
    glide.finish();
    rig.mc.getTimelinePanel().getTrackListMotionForTest().finishNow();

    ASSERT_TRUE(rig.undo().undo());
    EXPECT_TRUE(rig.mc.getTimelinePanel().getTrackListMotionForTest().isRunning()) << "the row grows back";
    EXPECT_GE(glide.enterGhostCount(), 1) << "with the macro card";
}

// ---- Deleting the macro a track plays: the question ----

namespace {

constexpr const char* kAskKey = "timelineAskBeforeDeletingTrack";
constexpr const char* kChoiceKey = "timelineDeleteMacroChoice";

using synth::ui::DeleteMacroChoice;

// Answers the macro question by hand: holds it open like a window would, counts it and keeps its words. Clears the two
// shared user settings it reads before and after, so no test sees another's remembered answer.
class MacroAsk {
public:
    explicit MacroAsk(MainComponent& mc)
        : settings_(*mc.getAppPropertiesForTest().getUserSettings()) {
        clearSettings();
        synth::ui::test_hooks::deleteMacroConfirmHookForTest() = [this](const auto& t, auto done) {
            ++asked;
            text = t;
            pending = std::move(done);
        };
    }
    ~MacroAsk() {
        synth::ui::test_hooks::deleteMacroConfirmHookForTest() = nullptr;
        clearSettings();
    }
    void answer(DeleteMacroChoice choice, bool remember = false) {
        ASSERT_TRUE(pending) << "no question is open";
        auto done = std::move(pending);
        pending = nullptr;
        done(choice, remember);
    }
    juce::PropertiesFile& settings() { return settings_; }

    int asked = 0;
    synth::ui::DeleteTrackConfirmText text;
    std::function<void(DeleteMacroChoice, bool)> pending;

private:
    void clearSettings() {
        settings_.removeValue(kAskKey);
        settings_.removeValue(kChoiceKey);
        settings_.saveIfNeeded();
    }
    juce::PropertiesFile& settings_;
};

juce::MouseEvent cardMouseEvent(juce::Component& comp, juce::Point<int> position) {
    const auto pos = position.toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

// The saved multi-track project, reopened the way the app opens a file, with the macro question wired to MacroAsk.
class SavedProjectMacroDelete : public ::testing::Test {
protected:
    void SetUp() override {
        mc = project.open();
        ASSERT_TRUE(project.wasSaved());
        ASSERT_NE(mc, nullptr);
        ask = std::make_unique<MacroAsk>(*mc);
    }
    void TearDown() override {
        ask.reset();
        mc.reset();
    }

    GraphEditor& editor() { return mc->getGraphEditor(); }
    synth::MacroSet& macros() { return editor().getMacros(); }
    size_t trackCount() { return mc->getTimelineDoc().getTracks().size(); }
    juce::String trackMacro(int index) { return synth::test::SavedProjectFixture::trackMacroId(*mc, index); }
    State state() { return captureState(*mc, true); }
    int countOf(const juce::String& name) {
        int n = 0;
        for (auto* node : mc->getAudioEngine().getGraph().getNodes())
            n += node->getProcessor()->getName() == name ? 1 : 0;
        return n;
    }
    int channelStripCount() {
        int n = 0;
        for (auto* node : mc->getAudioEngine().getGraph().getNodes())
            n += dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr ? 1 : 0;
        return n;
    }
    int hostedPluginCount() {
        int n = 0;
        for (auto* node : mc->getAudioEngine().getGraph().getNodes())
            n += dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()) != nullptr ? 1 : 0;
        return n;
    }
    // A click on the folded card, then the real Delete key on the canvas.
    bool clickCardThenPress(const juce::String& macroId, const juce::KeyPress& key) {
        auto* card = editor().getMacroController().getMacroCardForTest(macroId);
        if (card == nullptr || !card->isVisible())
            return false;
        card->mouseDown(cardMouseEvent(*card, {140, 60}));
        card->mouseUp(cardMouseEvent(*card, {140, 60}));
        return editor().keyPressed(key);
    }
    bool selectMacroThenPress(const juce::String& macroId, const juce::KeyPress& key) {
        editor().getMacroController().selectMacro(macroId, false);
        return editor().keyPressed(key);
    }
    void clickDeleteMacroMenuItem(const juce::String& macroId) {
        const auto menu = editor().buildMacroMenu(macroId);
        juce::PopupMenu::MenuItemIterator it(menu, true);
        while (it.next())
            if (it.getItem().text == "Delete Macro && Modules") {
                ASSERT_TRUE(static_cast<bool>(it.getItem().action));
                it.getItem().action();
                return;
            }
        FAIL() << "the macro menu has no Delete Macro item";
    }

    BottomDockActiveTabResetGuardMDT resetGuard;
    synth::test::SavedProjectFixture project;
    std::unique_ptr<MainComponent> mc;
    std::unique_ptr<MacroAsk> ask;
};

const juce::KeyPress kDelete(juce::KeyPress::deleteKey);
const juce::KeyPress kBackspace(juce::KeyPress::backspaceKey);

} // namespace

TEST_F(SavedProjectMacroDelete, TheSavedProjectOpensWithTheMacrosAndModulesItWasSavedWith) {
    ASSERT_EQ(trackCount(), 3u);
    for (int i = 0; i < 3; ++i)
        EXPECT_FALSE(trackMacro(i).isEmpty()) << "track " << i;
    EXPECT_TRUE(macros().find(trackMacro(0))->collapsed);
    EXPECT_TRUE(macros().find(trackMacro(1))->collapsed);
    EXPECT_FALSE(macros().find(trackMacro(2))->collapsed);
    const auto parent = synth::test::SavedProjectFixture::macroIdNamed(*mc, "Nested parent");
    ASSERT_FALSE(parent.isEmpty());
    EXPECT_EQ(macros().parentOf(trackMacro(1)), parent) << "Audio 2's macro is nested";
    EXPECT_FALSE(synth::test::SavedProjectFixture::macroIdNamed(*mc, "Plain group").isEmpty());
    EXPECT_EQ(countOf("LFO"), 1);
    EXPECT_EQ(countOf("ADSR"), 1);
    EXPECT_EQ(hostedPluginCount(), 1);
}

TEST_F(SavedProjectMacroDelete, DeleteOnAFoldedTracksMacroCardAsksAndChangesNothingUntilAnswered) {
    const auto before = state();
    ASSERT_TRUE(clickCardThenPress(trackMacro(0), kDelete));
    EXPECT_EQ(ask->asked, 1);
    EXPECT_EQ(ask->text.title, "Delete the macro of Audio 1?");
    expectSameState(before, state());
    EXPECT_EQ(trackCount(), 3u);
}

TEST_F(SavedProjectMacroDelete, RemoveTrackTakesTheTrackItsMacroItsModulatorsAndItsMixerChannelInOneUndoStep) {
    const auto victim = trackMacro(0);
    const auto before = state();
    const int stripsBefore = channelStripCount();
    ASSERT_GE(stripsBefore, 3);
    mc->getUndoManager().beginNewTransaction();
    ASSERT_TRUE(clickCardThenPress(victim, kBackspace));
    ask->answer(DeleteMacroChoice::removeTrack);

    EXPECT_EQ(trackCount(), 2u);
    EXPECT_EQ(macros().find(victim), nullptr);
    EXPECT_EQ(countOf("LFO"), 0) << "the LFO inside the macro went with it";
    EXPECT_EQ(countOf("ADSR"), 0);
    EXPECT_EQ(channelStripCount(), stripsBefore - 1) << "its mixer channel went";
    EXPECT_NE(macros().find(trackMacro(0)), nullptr) << "Audio 2's macro, now the first track's, is untouched";

    ASSERT_TRUE(mc->getUndoManager().undo()) << "one step";
    expectSameState(before, state());
}

TEST_F(SavedProjectMacroDelete, KeepTrackDeletesTheMacroAndItsModulesAndLeavesTheTrack) {
    const auto victim = trackMacro(0);
    const auto before = state();
    mc->getUndoManager().beginNewTransaction();
    ASSERT_TRUE(clickCardThenPress(victim, kDelete));
    ask->answer(DeleteMacroChoice::keepTrack);

    EXPECT_EQ(trackCount(), 3u) << "the track row stays";
    EXPECT_EQ(macros().find(victim), nullptr);
    EXPECT_EQ(countOf("LFO"), 0);
    EXPECT_EQ(countOf("ADSR"), 0);

    ASSERT_TRUE(mc->getUndoManager().undo()) << "one step";
    expectSameState(before, state());
}

TEST_F(SavedProjectMacroDelete, CancelChangesNothingAndStoresNothingEvenWithRememberTicked) {
    const auto before = state();
    ASSERT_TRUE(clickCardThenPress(trackMacro(0), kDelete));
    ask->answer(DeleteMacroChoice::cancel, true);
    expectSameState(before, state());
    EXPECT_FALSE(ask->settings().containsKey(kAskKey));
    EXPECT_FALSE(ask->settings().containsKey(kChoiceKey));
}

TEST_F(SavedProjectMacroDelete, TheMenuItemAsksTheSameQuestionAndEveryAnswerWorks) {
    clickDeleteMacroMenuItem(trackMacro(0));
    EXPECT_EQ(ask->asked, 1);
    EXPECT_EQ(ask->text.title, "Delete the macro of Audio 1?");
    ask->answer(DeleteMacroChoice::cancel);
    EXPECT_EQ(trackCount(), 3u);

    clickDeleteMacroMenuItem(trackMacro(2));
    ask->answer(DeleteMacroChoice::removeTrack);
    EXPECT_EQ(trackCount(), 2u);

    clickDeleteMacroMenuItem(trackMacro(0));
    ask->answer(DeleteMacroChoice::keepTrack);
    EXPECT_EQ(trackCount(), 2u);
    EXPECT_EQ(ask->asked, 3);
}

TEST_F(SavedProjectMacroDelete, AnOpenTracksMacroAsksOnDeleteToo) {
    const auto open = trackMacro(2);
    ASSERT_FALSE(macros().find(open)->collapsed);
    ASSERT_TRUE(selectMacroThenPress(open, kDelete));
    EXPECT_EQ(ask->asked, 1);
    EXPECT_EQ(ask->text.title, "Delete the macro of Audio 3?");
    ask->answer(DeleteMacroChoice::removeTrack);
    EXPECT_EQ(trackCount(), 2u);
    EXPECT_EQ(macros().find(open), nullptr);
}

TEST_F(SavedProjectMacroDelete, ANestedTracksMacroAsksFromItsOwnCardAndFromItsParent) {
    const auto child = trackMacro(1);
    const auto parent = synth::test::SavedProjectFixture::macroIdNamed(*mc, "Nested parent");
    ASSERT_EQ(macros().parentOf(child), parent);
    const auto before = state();
    // The old lookup compared the track's nearest channel macro with the macro being deleted: for the parent that is
    // the child, so the parent delete fell through to a plain delete and the track row stayed.
    const auto audio2 = mc->getTimelineDoc().getTracks()[1].bindingUuid;
    const auto* channel = synth::nearestChannelMacro(mc->getAudioEngine().getGraph(), macros(), audio2);
    ASSERT_NE(channel, nullptr);
    EXPECT_EQ(channel->id, child);
    EXPECT_NE(channel->id, parent);

    clickDeleteMacroMenuItem(child);
    EXPECT_EQ(ask->asked, 1) << "the nested macro's own menu asks";
    ask->answer(DeleteMacroChoice::cancel);

    ASSERT_TRUE(selectMacroThenPress(parent, kDelete));
    EXPECT_EQ(ask->asked, 2) << "so does the parent that holds it";
    EXPECT_EQ(ask->text.title, "Delete the macro of Audio 2?");
    expectSameState(before, state());

    ask->answer(DeleteMacroChoice::removeTrack);
    EXPECT_EQ(trackCount(), 2u);
    EXPECT_EQ(macros().find(parent), nullptr);
    EXPECT_EQ(macros().find(child), nullptr);
    ASSERT_TRUE(mc->getUndoManager().undo());
    expectSameState(before, state());
}

TEST_F(SavedProjectMacroDelete, KeepTrackOnTheParentOfATracksMacroLeavesTheTrack) {
    const auto parent = synth::test::SavedProjectFixture::macroIdNamed(*mc, "Nested parent");
    ASSERT_TRUE(selectMacroThenPress(parent, kBackspace));
    ask->answer(DeleteMacroChoice::keepTrack);
    EXPECT_EQ(trackCount(), 3u);
    EXPECT_EQ(macros().find(parent), nullptr);
}

TEST_F(SavedProjectMacroDelete, TwoTracksMacrosAndALooseModuleSelectedTogetherAskOnceAndRemoveBothTracks) {
    const auto first = trackMacro(0);
    const auto third = trackMacro(2);
    const auto plain = synth::test::SavedProjectFixture::macroIdNamed(*mc, "Plain group");
    editor().getMacroController().selectMacro(first, false);
    editor().getMacroController().selectMacro(third, true);
    editor().getMacroController().selectMacro(plain, true);
    const auto before = state();
    mc->getUndoManager().beginNewTransaction();

    ASSERT_TRUE(editor().keyPressed(kDelete));
    EXPECT_EQ(ask->asked, 1);
    EXPECT_EQ(ask->text.title, "Delete the macro of Audio 1, Audio 3?");
    ask->answer(DeleteMacroChoice::removeTrack);

    EXPECT_EQ(trackCount(), 1u);
    EXPECT_EQ(macros().find(first), nullptr);
    EXPECT_EQ(macros().find(third), nullptr);
    EXPECT_EQ(macros().find(plain), nullptr) << "the rest of the selection goes too";
    ASSERT_TRUE(mc->getUndoManager().undo()) << "one step";
    expectSameState(before, state());
}

TEST_F(SavedProjectMacroDelete, KeepTrackOnSeveralSelectedMacrosKeepsEveryTrack) {
    editor().getMacroController().selectMacro(trackMacro(0), false);
    editor().getMacroController().selectMacro(trackMacro(2), true);
    ASSERT_TRUE(editor().keyPressed(kBackspace));
    ask->answer(DeleteMacroChoice::keepTrack);
    EXPECT_EQ(trackCount(), 3u);
    EXPECT_EQ(macros().size(), 3) << "Audio 2's parent and child and the plain group are left";
}

TEST_F(SavedProjectMacroDelete, CutAsksLikeDeleteAndCancelKeepsTheMacro) {
    const auto victim = trackMacro(0);
    editor().getMacroController().selectMacro(victim, false);
    const auto before = state();
    ASSERT_TRUE(mc->getCommandManager().invokeDirectly(AppCommands::cutSelection, false));
    EXPECT_EQ(ask->asked, 1);
    ask->answer(DeleteMacroChoice::cancel);
    expectSameState(before, state());

    ASSERT_TRUE(mc->getCommandManager().invokeDirectly(AppCommands::cutSelection, false));
    ask->answer(DeleteMacroChoice::keepTrack);
    EXPECT_EQ(macros().find(victim), nullptr);
    EXPECT_EQ(trackCount(), 3u);
}

TEST_F(SavedProjectMacroDelete, AMacroNoTrackPlaysDeletesAtOnceWithHostedPluginAndAllAndNoQuestion) {
    const auto plain = synth::test::SavedProjectFixture::macroIdNamed(*mc, "Plain group");
    ASSERT_EQ(hostedPluginCount(), 1);
    ASSERT_TRUE(clickCardThenPress(plain, kDelete));
    EXPECT_EQ(ask->asked, 0);
    EXPECT_EQ(trackCount(), 3u);
    EXPECT_EQ(macros().find(plain), nullptr);
    EXPECT_EQ(hostedPluginCount(), 0);
    ASSERT_TRUE(mc->getUndoManager().undo());
    EXPECT_EQ(hostedPluginCount(), 1);
}

TEST_F(SavedProjectMacroDelete, ASingleModuleInsideATracksMacroDeletesWithoutTheQuestion) {
    const auto macroId = trackMacro(2);
    const auto module = synth::test::SavedProjectFixture::memberUuidOfType(*mc, macroId, "Compressor");
    ASSERT_FALSE(module.isEmpty());
    for (auto* node : mc->getAudioEngine().getGraph().getNodes())
        if (node->properties["uuid"].toString() == module)
            editor().setSelectedNodes({node->nodeID});
    ASSERT_TRUE(editor().keyPressed(kDelete));
    EXPECT_EQ(ask->asked, 0);
    EXPECT_EQ(trackCount(), 3u);
}

TEST_F(SavedProjectMacroDelete, RememberKeepTrackStopsTheQuestionAndKeepsTheTrackEveryTime) {
    ASSERT_TRUE(clickCardThenPress(trackMacro(0), kDelete));
    ask->answer(DeleteMacroChoice::keepTrack, true);
    EXPECT_EQ(ask->settings().getValue(kAskKey), "0");
    EXPECT_EQ(ask->settings().getValue(kChoiceKey), "keep");

    ASSERT_TRUE(selectMacroThenPress(trackMacro(1), kDelete));
    EXPECT_EQ(ask->asked, 1) << "no second question";
    EXPECT_EQ(trackCount(), 3u);
}

TEST_F(SavedProjectMacroDelete, RememberRemoveTrackRemovesTheTrackWithoutAskingNext) {
    ASSERT_TRUE(clickCardThenPress(trackMacro(0), kDelete));
    ask->answer(DeleteMacroChoice::removeTrack, true);
    EXPECT_EQ(ask->settings().getValue(kChoiceKey), "remove");
    EXPECT_EQ(trackCount(), 2u);

    ASSERT_TRUE(selectMacroThenPress(trackMacro(1), kDelete));
    EXPECT_EQ(ask->asked, 1);
    EXPECT_EQ(trackCount(), 1u);
}

TEST_F(SavedProjectMacroDelete, TheTrackDeleteSettingAloneMeansRemoveTrackAsItAlwaysDid) {
    ask->settings().setValue(kAskKey, "0"); // the track row's "Don't ask again"
    ASSERT_TRUE(clickCardThenPress(trackMacro(0), kDelete));
    EXPECT_EQ(ask->asked, 0);
    EXPECT_EQ(trackCount(), 2u);
}

// A parent holding two tracks' macros: one question names both, and Remove track takes both in one undo step.
TEST(TimelineTrackDeleteMacroAsk, AParentHoldingTwoTracksAsksOnceAndRemovesBothInOneUndoStep) {
    Rig rig(3);
    MacroAsk ask(rig.mc);
    const auto first = rig.macroOfTrack(0)->id;
    const auto second = rig.macroOfTrack(1)->id;
    rig.editor().getMacroController().selectMacro(first, false);
    rig.editor().getMacroController().selectMacro(second, true);
    const auto parent = rig.editor().getMacroController().groupSelectionIntoMacro();
    ASSERT_FALSE(parent.isEmpty());
    ASSERT_EQ(rig.macros().parentOf(first), parent);
    const auto before = rig.state();

    rig.editor().getMacroController().selectMacro(parent, false);
    ASSERT_TRUE(rig.editor().keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(ask.asked, 1);
    EXPECT_EQ(ask.text.title, "Delete the macro of Audio 1, Audio 2?");
    rig.undo().beginNewTransaction();
    ask.answer(DeleteMacroChoice::removeTrack);
    EXPECT_EQ(rig.doc().getTracks().size(), 1u);
    EXPECT_EQ(rig.macros().find(parent), nullptr);
    ASSERT_TRUE(rig.undo().undo()) << "one step";
    expectSameState(before, rig.state());
}

// A delete with no app around the editor has nothing to ask: the macro goes as it always did.
TEST(TimelineTrackDeleteMacroAsk, WithoutAnAppHookTheKeyDeletesTheMacroDirectly) {
    Rig rig;
    const auto macroId = rig.macroOfTrack(0)->id;
    rig.editor().getMacroController().onDeleteMacrosRequested = nullptr;
    rig.editor().getMacroController().selectMacro(macroId, false);
    ASSERT_TRUE(rig.editor().keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    EXPECT_EQ(rig.macros().find(macroId), nullptr);
    EXPECT_EQ(rig.doc().getTracks().size(), 2u);
}
