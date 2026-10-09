// ProjectLoadPipelineTests.cpp -- a project opened on screen comes to life as it loads (ProjectLoadPipeline.h): a
// fast load plays the short wave and never shows the stage line; a slow one outlines the cards still waiting, says
// what it is waiting for after 400 ms, holds edits, and the audio gate opens only once the last cable has landed.
// The on-screen path is forced (no window in a test) and the reveal is stepped by hand.

#include "../../../Mixer/ChannelFlow/ChannelFlowTestFixture.h"

#include "Modules/OscillatorModule.h"
#include "Modules/SamplerModule.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Graph/ProjectLoad/EditBlockOverlay.h"
#include "UI/Graph/ProjectLoad/LoadRevealAnimator.h"
#include "UI/Graph/ProjectLoad/ProjectLoadPipeline.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

class ProjectLoadPipelineTest : public ChannelFlowTest {
protected:
    void SetUp() override {
        ChannelFlowTest::SetUp();
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        dir_ = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("LoadPipeline", "");
        dir_.createDirectory();
        wav_ = dir_.getChildFile("loop.wav");
        juce::AudioBuffer<float> buffer(1, 9600);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(0, i, 0.4f * std::sin((float)i * 0.07f));
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(new juce::FileOutputStream(wav_), 48000.0, 1, 16, {}, 0));
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }
    void TearDown() override {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        dir_.deleteRecursively();
        ChannelFlowTest::TearDown();
    }

    template <typename T>
    static NodeID findNode(MainComponent& mc) {
        for (auto* node : mc.getAudioEngine().getGraph().getNodes())
            if (dynamic_cast<T*>(node->getProcessor()) != nullptr)
                return node->nodeID;
        return {};
    }

    static NodeID outputNode(MainComponent& mc) {
        for (auto* node : mc.getAudioEngine().getGraph().getNodes())
            if (mc.getGraphEditor().isOutputDockNode(node->nodeID) && node->getProcessor()->getName() == "Audio Output")
                return node->nodeID;
        return {};
    }

    static ModuleComponent* cardFor(MainComponent& mc, NodeID id) {
        for (auto* card : mc.getGraphEditor().getModuleComponents())
            if (card != nullptr && card->getNodeId() == id)
                return card;
        return nullptr;
    }

    // An oscillator cabled to the output, plus (`withSample`) a Sampler holding a real file, saved as a bundle.
    juce::File saveProject(bool withSample) {
        const auto bundle = dir_.getChildFile(withSample ? "Slow.agsproj" : "Fast.agsproj");
        MainComponent mc(std::make_unique<MockProviderCFT>());
        mc.setSize(1400, 900);
        mc.getAudioEngine().suspendDeviceCallback();
        auto& editor = mc.getGraphEditor();
        editor.addModuleAtCanvasPosition("Oscillator", {200, 200}, {});
        if (withSample)
            editor.addModuleAtCanvasPosition("Sampler", {200, 520}, [this](juce::AudioProcessor& p) {
                static_cast<SamplerModule&>(p).loadSampleFile(wav_);
            });
        editor.connectPorts(findNode<OscillatorModule>(mc), 0, outputNode(mc), 0, false);
        EXPECT_TRUE(mc.saveProjectForTest(bundle));
        return bundle;
    }

    // A window-less MainComponent whose opens take the on-screen path, on a clock the test moves. The animation mode
    // is set after the MainComponent exists, since building one applies the user's saved choice.
    struct Opener {
        MainComponent mc{std::make_unique<MockProviderCFT>()};
        double clock = 0.0;
        explicit Opener(synth::ui::AnimationMode mode = synth::ui::AnimationMode::full) {
            synth::ui::setAnimationMode(mode);
            mc.setSize(1400, 900);
            mc.getAudioEngine().suspendDeviceCallback();
            pipeline().setForceInteractiveForTest(true);
            pipeline().setClockForTest([this] { return clock; });
        }
        synth::ui::ProjectLoadPipeline& pipeline() { return mc.getProjectLoadPipeline(); }
        LoadRevealAnimator& reveal() { return mc.getGraphEditor().getLoadReveal(); }
        AudioEngine& engine() { return mc.getAudioEngine(); }
    };

    juce::File dir_;
    juce::File wav_;
};

} // namespace

TEST_F(ProjectLoadPipelineTest, AFastLoadPlaysTheShortWaveAndNeverShowsTheStageLine) {
    const auto bundle = saveProject(false);
    Opener o;
    ASSERT_TRUE(o.mc.openProjectForTest(bundle));
    auto& reveal = o.reveal();
    ASSERT_TRUE(reveal.isLive());
    EXPECT_FALSE(o.pipeline().isLoading()) << "nothing to wait for";
    EXPECT_FALSE(reveal.isBlockingEdits()) << "a fast load never holds edits, even while the wave plays";
    EXPECT_LE(reveal.endMs(), 400.0) << "the whole wave within about 400 ms";
    EXPECT_FALSE(o.engine().isLoadGateOpen()) << "silent until the last cable lands";

    const auto osc = findNode<OscillatorModule>(o.mc);
    const auto out = outputNode(o.mc);
    auto* oscCard = cardFor(o.mc, osc);
    auto* outCard = cardFor(o.mc, out);
    ASSERT_NE(oscCard, nullptr);
    ASSERT_NE(outCard, nullptr);
    const auto outBounds = outCard->getBounds();
    EXPECT_EQ(outCard->getAlpha(), 0.0f) << "hidden before its turn";
    EXPECT_LT(reveal.timeline().appearedMs(reveal.groupOfNode(osc.uid)),
              reveal.timeline().appearedMs(reveal.groupOfNode(out.uid)))
        << "the source comes before the output";

    // The cable waits for both of its ends.
    const double bothIn = std::max(reveal.timeline().appearedMs(reveal.groupOfNode(osc.uid)),
                                   reveal.timeline().appearedMs(reveal.groupOfNode(out.uid)));
    reveal.applyAtMs(bothIn - 1.0);
    EXPECT_EQ(reveal.cableProgress(osc.uid, out.uid), 0.0f);
    EXPECT_GT(oscCard->getAlpha(), 0.0f) << "the source is in";
    reveal.applyAtMs(bothIn + 30.0);
    EXPECT_GT(reveal.cableProgress(osc.uid, out.uid), 0.0f);
    EXPECT_LT(reveal.cableProgress(osc.uid, out.uid), 1.0f);
    EXPECT_EQ(outCard->getBounds(), outBounds) << "a pop never moves a card";

    o.clock = 1000.0;
    o.pipeline().pollForTest();
    EXPECT_FALSE(o.pipeline().isStageLineShown()) << "never shown for a fast load";

    reveal.applyAtMs(reveal.endMs());
    EXPECT_FALSE(reveal.isLive());
    EXPECT_TRUE(o.engine().isLoadGateOpen()) << "the gate opens with the last cable";
    EXPECT_EQ(outCard->getAlpha(), 1.0f);
    EXPECT_TRUE(outCard->getTransform().isIdentity());
    EXPECT_EQ(outCard->getBounds(), outBounds);
}

TEST_F(ProjectLoadPipelineTest, ASlowLoadOutlinesWaitsAndSaysWhatItIsWaitingFor) {
    const auto bundle = saveProject(true);
    Opener o;
    o.pipeline().assetLoads().holdDecodesForTest(true);
    ASSERT_TRUE(o.mc.openProjectForTest(bundle));
    auto& reveal = o.reveal();
    auto& editor = o.mc.getGraphEditor();
    const auto sampler = findNode<SamplerModule>(o.mc);
    auto* samplerCard = cardFor(o.mc, sampler);
    ASSERT_NE(samplerCard, nullptr);

    EXPECT_TRUE(o.pipeline().isLoading());
    EXPECT_EQ(o.pipeline().stageText(), "Loading samples 0/1");
    EXPECT_EQ(o.pipeline().progress(), 0.0f);
    reveal.applyAtMs(1000.0); // everything else has popped by now
    EXPECT_EQ(samplerCard->getAlpha(), 0.0f) << "the sampler waits for its file";
    EXPECT_GE(reveal.outlineCount(), 1) << "its spot is outlined";
    EXPECT_TRUE(reveal.isLive());

    o.clock = 399.0;
    o.pipeline().pollForTest();
    EXPECT_FALSE(o.pipeline().isStageLineShown()) << "not before 400 ms";
    o.clock = 401.0;
    o.pipeline().pollForTest();
    EXPECT_TRUE(o.pipeline().isStageLineShown());

    EXPECT_FALSE(o.engine().isLoadGateOpen());
    EXPECT_FALSE(o.engine().isMasterMuted()) << "the gate never touches the user's mute";

    // Edits wait: a library add, a canvas Delete and an Edit command are all refused with a status message.
    const int cards = editor.getModuleComponents().size();
    editor.addModuleAtCanvasPosition("LFO", {600, 200}, {});
    EXPECT_EQ(editor.getModuleComponents().size(), cards);
    EXPECT_EQ(o.mc.getStatusBar().getTransientMessageForTest(), "Still loading");
    editor.selectModule(findNode<OscillatorModule>(o.mc), false);
    EXPECT_TRUE(editor.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey)));
    o.mc.getCommandManager().invokeDirectly(AppCommands::duplicateSelection, false);
    EXPECT_EQ(editor.getModuleComponents().size(), cards);
    EXPECT_FALSE(editor.isInterestedInFileDrag({wav_.getFullPathName()})) << "nothing can be dropped";
    // Scrolling and zooming still work.
    const auto viewBefore = editor.getVisibleCanvasRect();
    editor.zoomAroundCentre(1.0f);
    EXPECT_NE(editor.getVisibleCanvasRect(), viewBefore);

    o.pipeline().assetLoads().finishAllForTest();
    EXPECT_EQ(o.pipeline().assetLoads().counts(synth::DeferredAssetLoads::Kind::Sample).done, 1);
    EXPECT_EQ(o.pipeline().assetLoads().counts(synth::DeferredAssetLoads::Kind::Sample).total, 1);
    EXPECT_EQ(o.pipeline().progress(), 1.0f);
    EXPECT_FALSE(o.pipeline().isLoading());
    EXPECT_FALSE(reveal.isBlockingEdits());
    EXPECT_FALSE(o.pipeline().isStageLineShown());
    EXPECT_FALSE(o.engine().isLoadGateOpen()) << "the sampler still has to pop and its cables to land";

    reveal.applyAtMs(reveal.endMs());
    EXPECT_FALSE(reveal.isLive());
    EXPECT_EQ(samplerCard->getAlpha(), 1.0f);
    EXPECT_TRUE(o.engine().isLoadGateOpen());
    auto* samplerModule = dynamic_cast<SamplerModule*>(o.engine().getGraph().getNodeForId(sampler)->getProcessor());
    EXPECT_EQ(samplerModule->getSampleFilePath(), wav_.getFullPathName());
    bool labelShowsFile = false;
    std::function<void(juce::Component&)> find = [&](juce::Component& c) {
        if (auto* l = dynamic_cast<juce::Label*>(&c))
            labelShowsFile |= l->getText() == wav_.getFileName();
        for (auto* child : c.getChildren())
            find(*child);
    };
    find(*samplerCard);
    EXPECT_TRUE(labelShowsFile) << "the card names the file once it lands";

    editor.addModuleAtCanvasPosition("LFO", {600, 200}, {});
    EXPECT_EQ(editor.getModuleComponents().size(), cards + 1) << "edits are back";
}

TEST_F(ProjectLoadPipelineTest, AnOffScreenOpenStaysSynchronous) {
    const auto bundle = saveProject(true);
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1400, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    ASSERT_TRUE(mc.openProjectForTest(bundle));
    EXPECT_FALSE(mc.getGraphEditor().getLoadReveal().isLive());
    EXPECT_FALSE(mc.getProjectLoadPipeline().isLoading());
    EXPECT_TRUE(mc.getAudioEngine().isLoadGateOpen());
    auto* sampler = dynamic_cast<SamplerModule*>(
        mc.getAudioEngine().getGraph().getNodeForId(findNode<SamplerModule>(mc))->getProcessor());
    EXPECT_EQ(sampler->getSampleFilePath(), wav_.getFullPathName()) << "decoded during the open, as always";
    for (auto* card : mc.getGraphEditor().getModuleComponents())
        EXPECT_EQ(card->getAlpha(), 1.0f);
}

TEST_F(ProjectLoadPipelineTest, ReduceMotionFadesTheCanvasButStillOutlinesASlowLoad) {
    const auto bundle = saveProject(true);
    Opener o(synth::ui::AnimationMode::reduced);
    o.pipeline().assetLoads().holdDecodesForTest(true);
    ASSERT_TRUE(o.mc.openProjectForTest(bundle));
    auto& reveal = o.reveal();
    auto* oscCard = cardFor(o.mc, findNode<OscillatorModule>(o.mc));
    auto* samplerCard = cardFor(o.mc, findNode<SamplerModule>(o.mc));
    EXPECT_EQ(oscCard->getAlpha(), 1.0f) << "no pop: the canvas fades in as a whole";
    EXPECT_TRUE(oscCard->getTransform().isIdentity());
    EXPECT_EQ(samplerCard->getAlpha(), 0.0f);
    EXPECT_GE(reveal.outlineCount(), 1) << "the outlines carry information, so they stay";
    o.clock = 500.0;
    o.pipeline().pollForTest();
    EXPECT_TRUE(o.pipeline().isStageLineShown());
    o.pipeline().assetLoads().finishAllForTest();
    reveal.applyAtMs(synth::ui::load_reveal::kReducedFadeMs);
    EXPECT_FALSE(reveal.isLive());
    EXPECT_EQ(samplerCard->getAlpha(), 1.0f);
    EXPECT_TRUE(o.engine().isLoadGateOpen());
}

TEST_F(ProjectLoadPipelineTest, AnotherOpenMidLoadReleasesTheFirst) {
    const auto slow = saveProject(true);
    const auto fast = saveProject(false);
    Opener o;
    o.pipeline().assetLoads().holdDecodesForTest(true);
    ASSERT_TRUE(o.mc.openProjectForTest(slow));
    ASSERT_TRUE(o.pipeline().isLoading());
    o.pipeline().assetLoads().holdDecodesForTest(false);
    ASSERT_TRUE(o.mc.openProjectForTest(fast));
    EXPECT_FALSE(o.pipeline().isLoading());
    EXPECT_FALSE(o.reveal().isBlockingEdits());
    o.reveal().applyAtMs(o.reveal().endMs());
    EXPECT_TRUE(o.engine().isLoadGateOpen());
}

// The edit block holds for every way in, not just the canvas: an AI edit, a detached panel and a knob's arrow keys.

TEST_F(ProjectLoadPipelineTest, AnAiEditWaitsForTheLoadAndAppliesAfterIt) {
    const auto bundle = saveProject(true);
    Opener o;
    o.pipeline().assetLoads().holdDecodesForTest(true);
    ASSERT_TRUE(o.mc.openProjectForTest(bundle));
    ASSERT_TRUE(o.pipeline().isLoading());
    auto& graph = o.engine().getGraph();
    auto& ai = o.mc.getAiServiceForTest();
    const int nodes = graph.getNumNodes();
    const juce::String plan = R"({"mode": "merge", "nodes": [{"id": 9101, "type": "LFO"}]})";
    const auto envelope = juce::JSON::parse(R"({"timelineOps": []})");

    o.mc.getStatusBar().showMessage("idle");
    const auto result = ai.applyProjectEdit(juce::JSON::parse(plan));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "Still loading");
    EXPECT_EQ(o.mc.getStatusBar().getTransientMessageForTest(), "Still loading");
    EXPECT_FALSE(ai.applyPatch(plan, true));
    EXPECT_EQ(ai.getLastPatchError(), "Still loading");
    const auto timeline = ai.applyTimelineOps(envelope);
    EXPECT_FALSE(timeline.ok);
    EXPECT_EQ(timeline.message, "Still loading");
    EXPECT_EQ(graph.getNumNodes(), nodes) << "nothing applied half-way";

    o.pipeline().assetLoads().finishAllForTest();
    ASSERT_FALSE(o.pipeline().isLoading());
    EXPECT_EQ(graph.getNumNodes(), nodes) << "and nothing arrives once the load is done";
    const auto applied = ai.applyProjectEdit(juce::JSON::parse(plan));
    EXPECT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(graph.getNumNodes(), nodes + 1);
}

TEST_F(ProjectLoadPipelineTest, ADetachedPanelRefusesClicksDuringTheLoadAndWorksAfterIt) {
    const auto bundle = saveProject(true);
    Opener o;
    auto& host = o.mc.getBottomDock().getTimelineHost();
    host.setDetached(true);
    auto* window = host.getDetachedWindowForTest();
    ASSERT_NE(window, nullptr);
    EXPECT_FALSE(window->areEditsBlocked());

    o.pipeline().assetLoads().holdDecodesForTest(true);
    ASSERT_TRUE(o.mc.openProjectForTest(bundle));
    ASSERT_TRUE(o.pipeline().isLoading());
    EXPECT_TRUE(window->areEditsBlocked());
    auto* block = window->getEditBlockForTest();
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(block->isVisible());
    EXPECT_EQ(block->getBounds(), window->getPanelForTest().getBounds()) << "it covers the panel";

    o.mc.getStatusBar().showMessage("idle");
    const auto click = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {5.0f, 5.0f}, {}, 0.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, block, block, juce::Time::getCurrentTime(), {}, {}, 1, false);
    block->mouseDown(click);
    EXPECT_EQ(o.mc.getStatusBar().getTransientMessageForTest(), "Still loading");

    // A panel detached while the load runs starts out blocked too.
    auto& mixerHost = o.mc.getBottomDock().getMixerHost();
    mixerHost.setDetached(true);
    ASSERT_NE(mixerHost.getDetachedWindowForTest(), nullptr);
    EXPECT_TRUE(mixerHost.getDetachedWindowForTest()->areEditsBlocked());

    o.pipeline().assetLoads().finishAllForTest();
    ASSERT_FALSE(o.pipeline().isLoading());
    EXPECT_FALSE(window->areEditsBlocked());
    EXPECT_FALSE(block->isVisible());
    EXPECT_FALSE(mixerHost.getDetachedWindowForTest()->areEditsBlocked());
}

TEST_F(ProjectLoadPipelineTest, KnobArrowKeysWaitForTheLoadAndTurnAfterIt) {
    const auto bundle = saveProject(true);
    Opener o;
    o.pipeline().assetLoads().holdDecodesForTest(true);
    ASSERT_TRUE(o.mc.openProjectForTest(bundle));
    ASSERT_TRUE(o.pipeline().isLoading());
    auto* card = cardFor(o.mc, findNode<OscillatorModule>(o.mc));
    ASSERT_NE(card, nullptr);
    synth::ui::CardKnobSlider* knob = nullptr;
    for (auto* stop : card->getKeyboardControls())
        if (knob == nullptr)
            knob = dynamic_cast<synth::ui::CardKnobSlider*>(stop);
    ASSERT_NE(knob, nullptr);
    const double before = knob->getValue();
    const juce::KeyPress up(juce::KeyPress::upKey);
    ASSERT_TRUE(knob->valueForKey(up).has_value());
    ASSERT_NE(*knob->valueForKey(up), before);

    o.mc.getStatusBar().showMessage("idle");
    EXPECT_TRUE(knob->keyPressed(up)) << "the key is taken, not passed on";
    EXPECT_EQ(knob->getValue(), before);
    EXPECT_FALSE(o.mc.getUndoManager().canUndo());
    EXPECT_EQ(o.mc.getStatusBar().getTransientMessageForTest(), "Still loading");

    o.pipeline().assetLoads().finishAllForTest();
    ASSERT_FALSE(o.pipeline().isLoading());
    EXPECT_EQ(knob->getValue(), before) << "nothing arrives once the load is done";
    EXPECT_TRUE(knob->keyPressed(up));
    EXPECT_NE(knob->getValue(), before);
}
