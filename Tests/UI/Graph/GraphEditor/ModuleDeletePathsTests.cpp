// ModuleDeletePathsTests.cpp: every module type the factory can build, deleted through every route that removes a node
// from the graph. A card (or a view inside it) that still listens to a processor after the graph freed it crashes
// under ASAN as a use-after-free, so one parameterised sweep over (module type x removal path) guards the whole kind
// instead of one hand-picked module per fix (docs/architecture/graph-queries.md, "Deleting nodes").
//
// The types come from synth::detail::moduleFactory(), never a list kept here, so a module added later is swept the day
// it is registered. Each case runs on a real off-screen MainComponent: a new project with one audio track, the module
// added to that track's channel macro through the canvas's own add, then removed by the path under test, with the
// message loop pumped and the window repainted afterwards so a deferred delete or a stale view is touched.
//
// Types the sweep cannot delete through a path say why in the test (kRefusedByDelete, kSingletonTypes), never skip
// silently.

// Regression test for FRO1099: a macro deleted with Keep track left a Compressor card listening to a freed processor
#include "../../../UniqueTempDir.h"
#include "../../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/AIStateMapper/AIStateMapperInternal.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ModuleBase.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/DeleteTrackConfirm.h"

#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <memory>
#include <set>
#include <tuple>
#include <vector>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using synth::ui::DeleteMacroChoice;

class QuietProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "DeletePathsProvider"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"Model"}, true);
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
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return "Model"; }
    void setRequestTimeoutMs(int) override {}
    int getRequestTimeoutMs() const override { return 1000; }
};

enum class Path {
    removeNodesNowNarrow,
    removeNodesNowBroad,
    deleteSelection,
    macroKeepTrack,
    macroRemoveTrack,
    cut,
    undoOfAdd,
    redoOfDelete,
};

const std::vector<std::pair<Path, const char*>> kPaths = {
    {Path::removeNodesNowNarrow, "RemoveNodesNowNarrow"},
    {Path::removeNodesNowBroad, "RemoveNodesNowBroad"},
    {Path::deleteSelection, "DeleteSelection"},
    {Path::macroKeepTrack, "MacroDeleteKeepTrack"},
    {Path::macroRemoveTrack, "MacroDeleteRemoveTrack"},
    {Path::cut, "Cut"},
    {Path::undoOfAdd, "UndoOfAdd"},
    {Path::redoOfDelete, "RedoOfDelete"},
};

// The canvas's add refuses a second Audio Input / Audio Output (GraphEditor::isSingletonIOModule), and a new project
// already has them, so for these the sweep takes the module that is already there instead of an added one. Undoing an
// add cannot be reached for them (nothing is added), which testUndoOfAdd reports with this reason.
const std::set<juce::String> kSingletonTypes = {"Audio Input", "Audio Output"};

// Types drawn as a chip on a cable, not as a card on the canvas (a modulation attenuverter): there is no card to check.
const std::set<juce::String> kCardlessTypes = {"Attenuverter", "Mod Slot"};

// Cut copies the selection before it deletes, and graph IO nodes and a modulation attenuverter are not
// snippet-copyable, so Cut leaves them. (Delete
// leaves the output dock's nodes too: GraphEditor::outputDockDeleteRefusal, asked per case below.)
const std::set<juce::String> kCutKeeps = {"Audio Input", "Midi Input",   "Audio Output",
                                          "Master",      "Attenuverter", "Mod Slot"};

std::vector<juce::String> factoryTypes() {
    std::vector<juce::String> types;
    for (const auto& entry : synth::detail::moduleFactory())
        types.push_back(entry.first);
    std::sort(types.begin(), types.end());
    return types;
}

juce::String gtestSafe(const juce::String& text) {
    juce::String out;
    for (auto c : text)
        out += juce::CharacterFunctions::isLetterOrDigit(c) ? c : (juce::juce_wchar)'_';
    return out;
}

constexpr const char* kAskKey = "timelineAskBeforeDeletingTrack";
constexpr const char* kChoiceKey = "timelineDeleteMacroChoice";

// Answers the macro question at once with a fixed choice; clears the two shared settings before and after so no case
// sees an earlier remembered answer.
class MacroAnswer {
public:
    MacroAnswer(MainComponent& mc, DeleteMacroChoice choice)
        : settings_(*mc.getAppPropertiesForTest().getUserSettings()) {
        clear();
        synth::ui::test_hooks::deleteMacroConfirmHookForTest() = [this, choice](const auto&, auto done) {
            ++asked;
            done(choice, false);
        };
    }
    ~MacroAnswer() {
        synth::ui::test_hooks::deleteMacroConfirmHookForTest() = nullptr;
        clear();
    }
    int asked = 0;

private:
    void clear() {
        settings_.removeValue(kAskKey);
        settings_.removeValue(kChoiceKey);
        settings_.saveIfNeeded();
    }
    juce::PropertiesFile& settings_;
};

using Param = std::tuple<juce::String, Path>;

class ModuleDeletePaths : public ::testing::TestWithParam<Param> {
protected:
    // One window for the whole suite: building a MainComponent per case would dominate the run. Every case starts from
    // a new project, so none sees another's graph.
    static void SetUpTestSuite() {
        mc_ = std::make_unique<MainComponent>(std::make_unique<QuietProvider>());
        mc_->setSize(1400, 900);
        mc_->getAudioEngine().suspendDeviceCallback();
    }
    static void TearDownTestSuite() { mc_.reset(); }

    void SetUp() override {
        mc().newPatchForTest();
        mc().simulateAddAudioTrackClick();
        pump();
        ASSERT_EQ(mc().getTimelineDoc().getTracks().size(), 1u);
        const auto binding = mc().getTimelineDoc().getTracks().front().bindingUuid;
        const auto* macro = editor().getMacros().findByMember(binding);
        ASSERT_NE(macro, nullptr) << "the track's channel macro";
        trackMacro_ = macro->id;
    }

    static MainComponent& mc() { return *mc_; }
    static GraphEditor& editor() { return mc().getGraphEditor(); }
    juce::AudioProcessorGraph& graph() { return mc().getAudioEngine().getGraph(); }

    static void pump(int ms = 8) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

    std::set<NodeID> nodeIds() {
        std::set<NodeID> ids;
        for (auto* node : graph().getNodes())
            ids.insert(node->nodeID);
        return ids;
    }

    // The module's node id: the one the canvas's add created, or for a singleton the one already in the project.
    NodeID addModule(const juce::String& type) {
        const auto before = nodeIds();
        editor().addModuleAtCanvasPosition(type, {900, 400}, {}, trackMacro_);
        pump();
        for (auto* node : graph().getNodes())
            if (before.count(node->nodeID) == 0)
                return node->nodeID;
        for (auto* node : graph().getNodes())
            if (node->getProcessor()->getName() == type)
                return node->nodeID;
        return {};
    }

    // Everything a live view does with its processor: nudge each parameter (the card's controls listen to these),
    // repaint the card and resize the window, then settle the message loop. After a removal the same repaint and resize
    // walk whatever views are left, so a card still bound to a freed processor is touched here, not only at teardown.
    void exerciseViews(NodeID id) {
        if (auto* node = graph().getNodeForId(id))
            for (auto* p : node->getProcessor()->getParameters())
                p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->getValue() + 0.01f));
        touchViews();
    }
    void touchViews() {
        for (auto* card : editor().getModuleComponents())
            card->repaint();
        editor().repaint();
        mc().resized();
        pump();
        const juce::Image image = mc().createComponentSnapshot(mc().getLocalBounds().withHeight(300), true, 0.25f);
        EXPECT_TRUE(image.isValid());
    }

    bool present(NodeID id) { return graph().getNodeForId(id) != nullptr; }
    bool hasCard(NodeID id) {
        for (auto* card : editor().getModuleComponents())
            if (card != nullptr && card->getNodeId() == id)
                return true;
        return false;
    }

    void expectRemoved(NodeID id, const juce::String& type) {
        pump();
        EXPECT_FALSE(present(id)) << type << " is still in the graph";
        EXPECT_FALSE(hasCard(id)) << type << " still has a card";
        touchViews();
    }

    // A user-facing delete that refuses this type: the node and its card stay, and the views still paint.
    void expectKept(NodeID id, const juce::String& type) {
        pump();
        EXPECT_TRUE(present(id)) << type << " was deleted though the project keeps it";
        touchViews();
    }

    void runPath(const juce::String& type, Path path) {
        const bool addRefused = kSingletonTypes.count(type) != 0;

        mc().getUndoManager().beginNewTransaction();
        const auto id = addModule(type);
        ASSERT_NE(id.uid, 0u) << type << " could not be added";
        ASSERT_TRUE(present(id));
        if (kCardlessTypes.count(type) == 0)
            ASSERT_TRUE(hasCard(id)) << type << " has no card on the canvas";
        exerciseViews(id);

        // Whether the user-facing delete leaves this node on purpose: the output dock's nodes (Master, Audio Output)
        // are refused, and a node that is not a member of the track's macro is not taken by the macro's delete (the
        // dock owns the nodes it lays out, so they are evicted from macros).
        const bool refused = editor().outputDockDeleteRefusal(id).isNotEmpty();
        const auto uuid = synth::AIStateMapper::ensureNodeUuid(graph().getNodeForId(id));
        const bool leftByMacro = refused || editor().getMacros().findByMember(uuid) == nullptr;

        switch (path) {
        case Path::removeNodesNowNarrow:
        case Path::removeNodesNowBroad: {
            editor().removeNodesNow({id}, /*healChain=*/true, /*narrowDetach=*/path == Path::removeNodesNowNarrow);
            expectRemoved(id, type);
            break;
        }
        case Path::deleteSelection: {
            editor().setSelectedNodes({id});
            editor().deleteSelection();
            refused ? expectKept(id, type) : expectRemoved(id, type);
            break;
        }
        case Path::cut: {
            editor().setSelectedNodes({id});
            mc().setEditSurfaceOverrideForTest(MainComponent::EditSurface::Graph);
            const bool handled = mc().getCommandManager().invokeDirectly(AppCommands::cutSelection, false);
            mc().setEditSurfaceOverrideForTest(std::nullopt);
            EXPECT_TRUE(handled);
            refused || kCutKeeps.count(type) != 0 ? expectKept(id, type) : expectRemoved(id, type);
            break;
        }
        case Path::macroKeepTrack:
        case Path::macroRemoveTrack: {
            // The track plays the macro the module sits in, so this asks the question and applies the answer.
            MacroAnswer answer(mc(), path == Path::macroKeepTrack ? DeleteMacroChoice::keepTrack
                                                                  : DeleteMacroChoice::removeTrack);
            editor().getMacroController().requestDeleteMacro(trackMacro_);
            EXPECT_EQ(answer.asked, 1);
            leftByMacro ? expectKept(id, type) : expectRemoved(id, type);
            EXPECT_EQ(editor().getMacros().find(trackMacro_), nullptr);
            EXPECT_EQ(mc().getTimelineDoc().getTracks().size(), path == Path::macroKeepTrack ? 1u : 0u);
            break;
        }
        case Path::undoOfAdd: {
            if (addRefused) {
                // Nothing was added (the project already holds this singleton), so there is no add to undo.
                GTEST_SKIP() << type << " is a singleton: the canvas add is a no-op, so no add exists to undo";
            }
            ASSERT_TRUE(mc().getUndoManager().undo());
            expectRemoved(id, type);
            break;
        }
        case Path::redoOfDelete: {
            editor().setSelectedNodes({id});
            mc().getUndoManager().beginNewTransaction();
            editor().deleteSelection();
            if (refused) {
                expectKept(id, type);
                break; // refused: no delete was recorded, so there is nothing to undo and redo
            }
            expectRemoved(id, type);
            ASSERT_TRUE(mc().getUndoManager().undo());
            pump();
            touchViews();
            ASSERT_TRUE(mc().getUndoManager().redo());
            // The restore built a new node for the module; the id the delete removed stays gone.
            expectRemoved(id, type);
            break;
        }
        }
    }

    BottomDockActiveTabResetGuardMDT resetGuard_;
    juce::String trackMacro_;
    static std::unique_ptr<MainComponent> mc_;
};

std::unique_ptr<MainComponent> ModuleDeletePaths::mc_;

} // namespace

TEST_P(ModuleDeletePaths, RemovesTheModuleAndLeavesNoViewOnTheFreedProcessor) {
    runPath(std::get<0>(GetParam()), std::get<1>(GetParam()));
}

std::string paramName(const ::testing::TestParamInfo<Param>& info) {
    const auto& type = std::get<0>(info.param);
    for (const auto& entry : kPaths)
        if (entry.first == std::get<1>(info.param))
            return std::string((gtestSafe(type) + "_" + entry.second).toRawUTF8());
    return std::string();
}

INSTANTIATE_TEST_SUITE_P(EveryTypeEveryPath, ModuleDeletePaths,
                         ::testing::Combine(::testing::ValuesIn(factoryTypes()),
                                            ::testing::Values(Path::removeNodesNowNarrow, Path::removeNodesNowBroad,
                                                              Path::deleteSelection, Path::macroKeepTrack,
                                                              Path::macroRemoveTrack, Path::cut, Path::undoOfAdd,
                                                              Path::redoOfDelete)),
                         paramName);

// The sweep must really cover the factory: a count that drifts from the registry means the list above stopped
// following it.
TEST(ModuleDeletePathsCoverage, SweepsEveryRegisteredType) {
    EXPECT_EQ(factoryTypes().size(), synth::detail::moduleFactory().size());
    EXPECT_GE(factoryTypes().size(), 47u);
}
