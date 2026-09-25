// E2EPluginCardWorkflowTests.cpp -- FRO137 (docs/control/plugin-card-layout.md): the whole
// plugin-card-knob story end to end, against the fake hosted instance (Tests/StubPluginInstance.h)
// since no real plugin binary can live in this repo. Sibling of Tests/App/E2EWorkflowTests.cpp,
// reusing its MainComponent-based setup for every test here, including the MIDI Learn one -- a
// single MainComponent for the whole test's lifetime, never a second one alongside a raw
// GraphEditor Rig, which was found (while writing this suite) to corrupt process-wide JUCE state in
// a way that crashes a LATER, unrelated test; see docs/development/testing.md#known-flaky-patterns
// for the write-up and the follow-up ticket.
//
// One flow: add a hosted plugin -> choose two knobs (the outcome the picker's applyCurrentLayout()
// produces, written directly -- PluginKnobPickerTests.cpp already drives the popover itself) ->
// save the project -> reload it -> the knobs are still on the card -> "Apply to all instances" ->
// a second instance of the same plugin shows them too -> a real right-click "MIDI Learn" on a
// hosted knob -> a fake CC (injected through AudioEngine::handleIncomingMidiMessageFromSource,
// exactly as MidiRemoteWorkflowE2ETests.cpp does for a built-in knob) drives the hosted parameter.

#include "../StubPluginInstance.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "MidiRemote/MidiLearnController.h"
#include "Modules/CardLayout.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include "ProjectBundle.h"
#include "Timeline/AutomationBinding.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

#include <chrono>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <set>
#include <vector>

using synth::HostedPluginModule;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubPluginInstance;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

synth::PluginIdentity e2ePluginIdentity() {
    synth::PluginIdentity identity;
    identity.format = "VST3";
    identity.name = "E2E Card Plugin";
    identity.uid = 0xE2EC0DE;
    return identity;
}

std::vector<StubParamSpec> e2ePluginParams() {
    return {{"cutoff", "Cutoff", 0.5f, {}, false}, {"resonance", "Resonance", 0.5f, {}, false}};
}

std::set<juce::AudioProcessorGraph::NodeID> nodeIds(juce::AudioProcessorGraph& graph) {
    std::set<juce::AudioProcessorGraph::NodeID> ids;
    for (auto* node : graph.getNodes())
        ids.insert(node->nodeID);
    return ids;
}

constexpr const char* kFakeDevice = "fro137-fake-controller";
constexpr int kCc = 20;

juce::MouseEvent childMouseEvent(juce::Component& child, juce::ModifierKeys mods) {
    const auto pos = child.getLocalBounds().getCentre().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &child, &child, juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 1, false);
}

// Same idiom as MidiRemoteWorkflowE2ETests.cpp's rightClick(): drives the control's own handler
// AND the card's registered-MouseListener half, capturing the real built PopupMenu.
juce::PopupMenu rightClick(ModuleComponent& card, juce::Component& child) {
    juce::PopupMenu captured;
    card.setShowContextMenuHookForTest([&](juce::PopupMenu& menu) { captured = menu; });
    const juce::ModifierKeys mods(juce::ModifierKeys::rightButtonModifier);
    const auto down = childMouseEvent(child, mods);
    child.mouseDown(down);
    card.mouseDown(down);
    child.mouseUp(childMouseEvent(child, mods));
    card.setShowContextMenuHookForTest(nullptr);
    return captured;
}

bool invokeMenuItem(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu);
    while (it.next()) {
        if (it.getItem().text == text && it.getItem().action) {
            it.getItem().action();
            return true;
        }
    }
    return false;
}

} // namespace

// ============================================================================
// PluginCardWorkflow -- one MainComponent for the whole test, start to finish
// ============================================================================

class E2EPluginCardWorkflowTest : public ::testing::Test {
protected:
    void SetUp() override {
        backend_ = std::make_unique<StubBackend>();
        backend_->setFactory([] {
            return std::make_unique<StubPluginInstance>(2, 2, "E2E Card Plugin", 0xE2EC0DE, "VST3", e2ePluginParams());
        });
        backendOverride_ = std::make_unique<synth::HostedPluginBackend::ScopedDefault>(backend_.get());

        bundleDir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile(juce::String("FRO137E2E") + synth::ProjectBundle::kBundleExtension);
        bundleDir_.deleteRecursively();

        mainComp_ = makeMainComponent();
        store_ = stores_.back().get();
    }

    void TearDown() override {
        mainComp_.reset();
        backendOverride_.reset();
        for (auto& dir : storeDirs_)
            dir.deleteRecursively();
        bundleDir_.deleteRecursively();
    }

    /** A REAL app only ever has one MainComponent, so it only ever has one PluginCardLayoutStore --
     *  but a "reload into a fresh session" test needs a SECOND, independent MainComponent alive
     *  briefly alongside (or right after) the first, and two cards pointed at the very same store
     *  instance is a dual-ownership shape production code never exercises. Each call gets its OWN
     *  temp-dir-backed store instead, kept alive in `stores_` for the fixture's lifetime, which is
     *  both more realistic (a relaunch reopens the real settings folder itself) and avoids that
     *  shape entirely. */
    std::unique_ptr<MainComponent> makeMainComponent() {
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("fro137-e2e-cardlayout-store-" + juce::Uuid().toString());
        dir.deleteRecursively();
        storeDirs_.push_back(dir);
        stores_.push_back(std::make_unique<synth::PluginCardLayoutStore>(dir));

        auto comp = std::make_unique<MainComponent>();
        comp->setSize(1600, 900);
        comp->getAudioEngine().getDeviceManager().closeAudioDevice();
        // Redirect the card-layout store away from the real settings folder -- see this method's
        // own comment on why each MainComponent gets its own instance.
        comp->getGraphEditor().setPluginCardLayoutStore(stores_.back().get());
        return comp;
    }

    /** Adds a hosted plugin node the same way the canvas's "Add plugin" drop does
     *  (GraphEditor::addHostedPluginAtCanvasPosition), waits for its (stub) instance, and returns
     *  the module. */
    HostedPluginModule* addPlugin(MainComponent& comp, juce::Point<int> pos = {300, 300}) {
        auto& graph = comp.getAudioEngine().getGraph();
        const auto before = nodeIds(graph);
        comp.getGraphEditor().addHostedPluginAtCanvasPosition(e2ePluginIdentity(), pos);

        HostedPluginModule* hosted = nullptr;
        pumpUntil([&] {
            for (auto* node : graph.getNodes()) {
                if (before.count(node->nodeID) > 0)
                    continue;
                if (auto* m = dynamic_cast<HostedPluginModule*>(node->getProcessor())) {
                    hosted = m;
                    return true;
                }
            }
            return false;
        });
        EXPECT_TRUE(pumpUntil([&] { return hosted != nullptr && hosted->hasInstance(); }));
        return hosted;
    }

    ModuleComponent* cardFor(MainComponent& comp, juce::AudioProcessor* module) {
        for (auto* c : comp.getGraphEditor().getModuleComponents())
            if (c->getModule() == module)
                return c;
        return nullptr;
    }

    /** The outcome the picker's "tick two" + applyCurrentLayout() produces for "This instance"
     *  (docs/control/plugin-card-layout.md#choosing-knobs-as-built-fro132) -- written directly, since
     *  driving the popover itself is PluginKnobPickerTests.cpp's job. */
    void tickTwoForThisInstance(HostedPluginModule& hosted) {
        synth::CardLayout layout;
        synth::CardSlot cutoff;
        cutoff.paramId = "cutoff";
        synth::CardSlot resonance;
        resonance.paramId = "resonance";
        layout.slots = {cutoff, resonance};
        hosted.setCardLayoutOverride(layout.toVar());
    }

    /** "Apply to all <plugin> instances": writes the plugin-type default and clears this instance's
     *  own override, exactly what the picker's scope switch does. */
    void applyToAllInstances(HostedPluginModule& hosted) {
        const auto parsed = synth::CardLayout::fromVar(hosted.getCardLayoutOverride());
        ASSERT_EQ(parsed.status, synth::CardLayout::ParseStatus::Ok);
        ASSERT_TRUE(store_->setDefault(hosted.getIdentity(), parsed.layout));
        hosted.setCardLayoutOverride(juce::var());
    }

    std::unique_ptr<StubBackend> backend_;
    std::unique_ptr<synth::HostedPluginBackend::ScopedDefault> backendOverride_;
    std::vector<juce::File> storeDirs_;
    std::vector<std::unique_ptr<synth::PluginCardLayoutStore>> stores_;
    synth::PluginCardLayoutStore* store_ = nullptr; // mainComp_'s own store -- see makeMainComponent()
    juce::File bundleDir_;
    std::unique_ptr<MainComponent> mainComp_;
};

TEST_F(E2EPluginCardWorkflowTest, AddChooseSaveReloadKeepsTheChosenKnobsOnTheCard) {
    auto* hosted = addPlugin(*mainComp_);
    ASSERT_NE(hosted, nullptr);
    tickTwoForThisInstance(*hosted);

    auto* card = cardFor(*mainComp_, hosted);
    ASSERT_NE(card, nullptr);
    EXPECT_NE(card->findChildWithID("hostedKnob:cutoff"), nullptr);
    EXPECT_NE(card->findChildWithID("hostedKnob:resonance"), nullptr);

    ASSERT_TRUE(mainComp_->saveProjectForTest(bundleDir_));

    // "Reload": a fresh MainComponent opening the same bundle, exactly what relaunching the app and
    // opening the project does.
    auto reloaded = makeMainComponent();
    ASSERT_TRUE(reloaded->openProjectForTest(bundleDir_));

    HostedPluginModule* reloadedModule = nullptr;
    for (auto* node : reloaded->getAudioEngine().getGraph().getNodes())
        if (auto* m = dynamic_cast<HostedPluginModule*>(node->getProcessor()))
            reloadedModule = m;
    ASSERT_NE(reloadedModule, nullptr);
    ASSERT_TRUE(pumpUntil([&] { return reloadedModule->hasInstance(); }));

    auto* reloadedCard = cardFor(*reloaded, reloadedModule);
    ASSERT_NE(reloadedCard, nullptr);
    EXPECT_NE(reloadedCard->findChildWithID("hostedKnob:cutoff"), nullptr)
        << "the per-instance cardLayout override survived the save/load round trip";
    EXPECT_NE(reloadedCard->findChildWithID("hostedKnob:resonance"), nullptr);
}

TEST_F(E2EPluginCardWorkflowTest, ApplyToAllInstancesShowsTheChosenKnobsOnASecondInstanceToo) {
    auto* first = addPlugin(*mainComp_, {200, 200});
    ASSERT_NE(first, nullptr);
    tickTwoForThisInstance(*first);
    applyToAllInstances(*first);

    // The first card rebuilt when its own override was cleared -- it now shows the type default
    // (the same two knobs), not the automatic set.
    auto* firstCard = cardFor(*mainComp_, first);
    ASSERT_NE(firstCard, nullptr);
    EXPECT_NE(firstCard->findChildWithID("hostedKnob:cutoff"), nullptr);
    EXPECT_NE(firstCard->findChildWithID("hostedKnob:resonance"), nullptr);

    auto* second = addPlugin(*mainComp_, {800, 200});
    ASSERT_NE(second, nullptr);
    auto* secondCard = cardFor(*mainComp_, second);
    ASSERT_NE(secondCard, nullptr);
    EXPECT_NE(secondCard->findChildWithID("hostedKnob:cutoff"), nullptr)
        << "a fresh instance of the same plugin resolves the type default with no override of its own";
    EXPECT_NE(secondCard->findChildWithID("hostedKnob:resonance"), nullptr);
}

// A real right-click "Automate" on a hosted card knob, through the production wiring
// (GraphEditor::onAutomateParameterRequested -> MainComponent::automateParameter). A hosted
// parameter is not a RangedAudioParameter, so automateParameter must hand it to the lane picker's
// hosted path; before that fix this item only printed "Can't automate: parameter not found".
TEST_F(E2EPluginCardWorkflowTest, AutomateOnAHostedKnobCreatesALaneBoundToTheHostedParameter) {
    auto* hosted = addPlugin(*mainComp_);
    ASSERT_NE(hosted, nullptr);
    auto* card = cardFor(*mainComp_, hosted);
    ASSERT_NE(card, nullptr);
    auto* knob = card->findChildWithID("hostedKnob:resonance");
    ASSERT_NE(knob, nullptr);

    ASSERT_TRUE(invokeMenuItem(rightClick(*card, *knob), "Automate 'Resonance'"));

    const juce::String uuid(hosted->getNodeUuid());
    ASSERT_TRUE(uuid.isNotEmpty()) << "automating assigns the node a uuid";
    const auto* lane = mainComp_->getTimelineDoc().getLaneForParam(uuid, "resonance");
    ASSERT_NE(lane, nullptr) << "Automate created a lane for the hosted parameter";
    EXPECT_EQ(lane->paramIndexHint, synth::captureParamIndexHint(hosted, "resonance"))
        << "the lane carries the hosted index hint, like one added from the lane picker";
    EXPECT_FALSE(lane->orphaned);
}

// ============================================================================
// MidiLearnOnAHostedKnob -- a real right-click MIDI Learn, then a fake CC, all through mainComp_
// ============================================================================

// A fake device key injected straight into AudioEngine::handleIncomingMidiMessageFromSource, the
// same seam MidiRemoteWorkflowE2ETests.cpp uses for a built-in knob -- that path never checks host
// mode, so it works on mainComp_'s (Standalone) engine exactly as it would on a Hosted one. Real
// hardware never opens headlessly, so MidiLearnController::arm()'s own refreshSources() call sees
// nothing; re-registering the fake key with RemoteEngine AFTER arm() is what a real device opening
// would have done for it.
TEST_F(E2EPluginCardWorkflowTest, MidiLearnOnAHostedKnobThenAFakeCcDrivesTheParameter) {
    auto* hosted = addPlugin(*mainComp_);
    ASSERT_NE(hosted, nullptr);

    auto* card = cardFor(*mainComp_, hosted);
    ASSERT_NE(card, nullptr);
    // No layout choice needed: "cutoff" is already on the card via the automatic default (both of
    // e2ePluginParams() are automatable, and the default shows the first eight).
    auto* knob = card->findChildWithID("hostedKnob:cutoff");
    ASSERT_NE(knob, nullptr);

    ASSERT_TRUE(invokeMenuItem(rightClick(*card, *knob), "MIDI Learn 'Cutoff'..."));
    auto& controller = mainComp_->getMidiLearnControllerForTest();
    auto& remote = mainComp_->getRemoteEngineForTest();
    ASSERT_TRUE(controller.isArmed());
    remote.setSources({juce::String(kFakeDevice)});
    // Takeover::jump, not the pickup default -- pickup's first hardware move right after a fresh
    // learn deliberately doesn't apply (it only arms the crossing check), which would be a false
    // negative here; this test is about resolution, not takeover semantics (see
    // MidiLearnControllerTests.cpp's own LearnThenImmediateCcDrivesTheParameterWithoutAnExplicitReconcile).
    remote.setDefaultTakeover(synth::Takeover::jump);

    mainComp_->getAudioEngine().handleIncomingMidiMessageFromSource(kFakeDevice,
                                                                    juce::MidiMessage::controllerEvent(1, kCc, 64));
    remote.drain();

    // Real wall-clock settle (RemoteEngine::kLearnSettleMs, 300ms) -- mainComp_'s RemoteEngine uses
    // the real clock, unlike MidiLearnControllerTests.cpp's own fixture, which injects a fake one.
    const auto settleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (controller.isArmed() && std::chrono::steady_clock::now() < settleDeadline) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        remote.drain();
    }
    ASSERT_FALSE(controller.isArmed()) << "the learn settled and bound";

    auto* cutoff = hosted->findInstanceParameter("cutoff");
    ASSERT_NE(cutoff, nullptr);
    const float before = cutoff->getValue();

    mainComp_->getAudioEngine().handleIncomingMidiMessageFromSource(kFakeDevice,
                                                                    juce::MidiMessage::controllerEvent(1, kCc, 0));
    remote.drain();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(
        50); // the drain's apply is a real gesture on the message thread

    EXPECT_NE(cutoff->getValue(), before) << "the fake CC actually moved the hosted parameter";

    // Close the still-open change gesture (RemoteEngine::kGestureIdleMs, 250ms) before mainComp_
    // (and the graph/instance under it) is torn down in TearDown() -- same reasoning as
    // MidiLearnControllerTests.cpp's own LearnThenImmediateCcDrivesTheParameterWithoutAnExplicitReconcile:
    // ~RemoteEngine's endAllGestures() would otherwise dereference a parameter the graph teardown
    // already freed.
    const auto idleDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < idleDeadline) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        remote.drain();
    }
}
