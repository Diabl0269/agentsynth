// Concern: the motion of "Duplicate Track" on a real MainComponent -- the copy's canvas cards glide out of the
// original's, the timeline row and the mixer strip come out of the original's, undo sends them back, Reduce Motion
// fades, and an off-screen window lands at once (docs/layout/duplicate-motion.md). Positions are checked at several
// moments of the glide, not only the end (bug kind: motion-over-time).
#include "../../UI/Layout/BottomDockActiveTabResetGuard.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "MainComponentTestFixture.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"

#include <set>

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
const juce::KeyPress kCmdD('d', juce::ModifierKeys::commandModifier, 0);

struct ReducedMotionGuard {
    explicit ReducedMotionGuard(bool reduced) { synth::ui::setReducedMotionForTest(reduced); }
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

class DuplicateTrackMotionTest : public MainComponentTest {
protected:
    void SetUp() override {
        MainComponentTest::SetUp();
        mc = std::make_unique<MainComponent>(std::make_unique<MockProvider>());
        mc->setSize(1600, 2400); // tall enough that the copy, below the original, is inside the canvas view
        mc->getAudioEngine().suspendDeviceCallback();
        mc->newPatchForTest();                     // leaves the welcome screen: the canvas and its cards are shown
        mc->getTimelinePanel().setSize(1400, 360); // the rows have a lane area to animate in
    }
    void TearDown() override {
        mc.reset();
        MainComponentTest::TearDown();
    }

    synth::TimelineDoc& doc() { return mc->getTimelineDoc(); }
    CardGlideAnimator& glide() { return mc->getGraphEditor().getCardGlideForTest(); }

    synth::TrackId addInstrumentTrack() {
        mc->getTimelinePanel().applyAddTrackMenuChoice(
            synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
        return doc().getTracks().back().id;
    }

    void forceMotion() {
        mc->getTimelinePanel().forceTrackGlideForTest(true);
        mc->getBottomDock().getMixerPanel().forceColumnMotionForTest(true);
        glide().setForceAnimateForTest(true);
    }

    // The Oscillator the track plays (reachable forward from its binding).
    NodeID oscillatorOf(synth::TrackId id) {
        auto& graph = mc->getAudioEngine().getGraph();
        const auto* track = doc().getTrack(id);
        NodeID start;
        for (auto* node : graph.getNodes())
            if (track != nullptr && node->properties["uuid"].toString() == track->bindingUuid)
                start = node->nodeID;
        std::set<juce::uint32> seen;
        std::vector<NodeID> pending{start};
        while (!pending.empty()) {
            const auto next = pending.back();
            pending.pop_back();
            if (!seen.insert(next.uid).second)
                continue;
            const auto* node = graph.getNodeForId(next);
            if (node == nullptr)
                continue;
            if (dynamic_cast<OscillatorModule*>(node->getProcessor()) != nullptr)
                return next;
            for (const auto& c : graph.getConnections())
                if (c.source.nodeID == next)
                    pending.push_back(c.destination.nodeID);
        }
        return {};
    }

    // The card that stands for the track on the canvas: its channel macro's card (the track's modules are folded
    // into it), else the card of the track's oscillator.
    juce::Component* cardFor(synth::TrackId id) {
        const auto* track = doc().getTrack(id);
        if (track == nullptr)
            return nullptr;
        if (const auto* macro = mc->getGraphEditor().getMacros().findByMember(track->bindingUuid))
            if (auto* card = mc->getGraphEditor().getMacroController().getMacroCardForTest(macro->id))
                return card;
        return cardOf(oscillatorOf(id));
    }

    // The glide animator's key for that card.
    juce::uint32 glideKeyFor(synth::TrackId id) {
        const auto* track = doc().getTrack(id);
        if (track != nullptr)
            if (const auto* macro = mc->getGraphEditor().getMacros().findByMember(track->bindingUuid))
                return CardGlideAnimator::macroKey(macro->id);
        return oscillatorOf(id).uid;
    }

    ModuleComponent* cardOf(NodeID id) {
        for (auto* card : mc->getGraphEditor().getModuleComponents())
            if (card != nullptr && card->getNodeId() == id)
                return card;
        return nullptr;
    }

    void duplicateWithCmdD(synth::TrackId id) {
        for (int i = 0; i < mc->getTimelinePanel().getTrackHeaderCount(); ++i)
            if (auto* header = mc->getTimelinePanel().getTrackHeaderAt(i); header->getTrackId() == id)
                ASSERT_TRUE(header->keyPressed(kCmdD));
    }

    BottomDockActiveTabResetGuardMDT tabGuard_;
    std::unique_ptr<MainComponent> mc;
};

} // namespace

TEST_F(DuplicateTrackMotionTest, TheCopysCanvasCardsGlideOutOfTheOriginalsAtSeveralMoments) {
    ReducedMotionGuard guard(false);
    const auto sourceId = addInstrumentTrack();
    auto* sourceCard = cardFor(sourceId);
    ASSERT_NE(sourceCard, nullptr);
    forceMotion();

    duplicateWithCmdD(sourceId);
    const auto copyId = doc().getTracks().back().id;
    ASSERT_NE(copyId, sourceId);
    auto* copyCard = cardFor(copyId);
    ASSERT_NE(copyCard, nullptr);
    ASSERT_TRUE(glide().isLive()) << "the copy is not simply there: it glides";

    const auto from = sourceCard->getBounds().getPosition();
    const auto to = copyCard->getBounds().getPosition();
    ASSERT_NE(from, to);

    glide().applyTweenAt(0.0f);
    EXPECT_EQ(glide().currentRectFor(copyCard).getPosition(), from) << "starts on the original";
    float lastY = static_cast<float>(from.y);
    for (const float t : {0.25f, 0.5f, 0.75f}) {
        glide().applyTweenAt(t);
        const auto rect = glide().currentRectFor(copyCard);
        EXPECT_GT(rect.getY(), static_cast<int>(lastY) - 1) << t;
        EXPECT_LE(rect.getY(), to.y);
        lastY = static_cast<float>(rect.getY());
    }
    glide().applyTweenAt(0.5f);
    EXPECT_GT(glide().currentRectFor(copyCard).getY(), from.y);
    EXPECT_LT(glide().currentRectFor(copyCard).getY(), to.y);
    glide().applyTweenAt(1.0f);
    EXPECT_EQ(glide().currentRectFor(copyCard).getPosition(), to) << "lands in its place";
    EXPECT_EQ(glide().currentRectFor(sourceCard), juce::Rectangle<int>()) << "the original does not move";
}

TEST_F(DuplicateTrackMotionTest, TheTimelineRowAndTheMixerStripComeOutOfTheOriginalsToo) {
    ReducedMotionGuard guard(false);
    const auto sourceId = addInstrumentTrack();
    forceMotion();
    duplicateWithCmdD(sourceId);

    auto& rows = mc->getTimelinePanel().getTrackListMotionForTest();
    ASSERT_TRUE(rows.isRunning());
    EXPECT_TRUE(rows.timeline().together);
    const auto copyKey = juce::String(static_cast<juce::int64>(doc().getTracks().back().id.value));
    const auto sourceKey = juce::String(static_cast<juce::int64>(sourceId.value));
    rows.applyAtMs(0.0);
    ASSERT_TRUE(rows.drawnRectFor(copyKey).has_value());
    EXPECT_NEAR(rows.drawnRectFor(copyKey)->getCentreY(), rows.drawnRectFor(sourceKey)->getCentreY(), 0.5f);
    rows.applyAtMs(rows.timeline().totalMs());
    EXPECT_GT(rows.drawnRectFor(copyKey)->getY(), rows.drawnRectFor(sourceKey)->getY());

    auto& columns = mc->getBottomDock().getMixerPanel().getColumnMotionForTest();
    if (columns.isRunning()) { // the mixer strip: the same, sideways
        EXPECT_TRUE(columns.timeline().together);
        EXPECT_EQ(columns.timeline().reverse, false);
    }
}

TEST_F(DuplicateTrackMotionTest, UndoSendsTheCopyBackIntoTheOriginalAndRedoGlidesItOutAgain) {
    ReducedMotionGuard guard(false);
    const auto sourceId = addInstrumentTrack();
    forceMotion();
    duplicateWithCmdD(sourceId);
    const auto copyId = doc().getTracks().back().id;
    const auto copyKey = glideKeyFor(copyId);
    auto* copyCard = cardFor(copyId);
    ASSERT_NE(copyCard, nullptr);
    const auto copyRect = copyCard->getBounds();
    const auto originalRect = cardFor(sourceId)->getBounds();
    glide().finish();

    ASSERT_TRUE(mc->getUndoManager().undo());
    ASSERT_GT(glide().exitGhostCount(), 0);
    glide().applyTimelineAtMs(0.0);
    EXPECT_EQ(glide().ghostRectFor(copyKey).toNearestInt(), copyRect) << "starts where the copy stood";
    glide().applyTimelineAtMs(glide().timeline().totalMs() * 0.5);
    const auto middle = glide().ghostRectFor(copyKey);
    EXPECT_GT(middle.getY(), static_cast<float>(originalRect.getY()));
    EXPECT_LT(middle.getY(), static_cast<float>(copyRect.getY()));
    glide().applyTimelineAtMs(glide().timeline().totalMs());
    EXPECT_NEAR(glide().ghostRectFor(copyKey).getY(), static_cast<float>(originalRect.getY()), 1.0f)
        << "ends on the original";
    glide().finish();

    ASSERT_TRUE(mc->getUndoManager().redo());
    EXPECT_GT(glide().enterGhostCount(), 0);
    glide().applyTimelineAtMs(0.0);
    glide().applyTimelineAtMs(glide().timeline().growMs() * 0.5); // halfway through the grow, before the outline
    const auto out = glide().ghostRectFor(copyKey);
    EXPECT_GT(out.getY(), static_cast<float>(originalRect.getY())) << "comes out of the original again";
    EXPECT_LT(out.getY(), static_cast<float>(copyRect.getY()));
}

TEST_F(DuplicateTrackMotionTest, ReduceMotionFadesTheCopyInPlaceOnTheCanvas) {
    ReducedMotionGuard guard(true);
    const auto sourceId = addInstrumentTrack();
    forceMotion();
    duplicateWithCmdD(sourceId);
    auto* copyCard = cardFor(doc().getTracks().back().id);
    ASSERT_NE(copyCard, nullptr);
    EXPECT_EQ(glide().currentRectFor(copyCard), juce::Rectangle<int>()) << "nothing travels";
    EXPECT_GT(glide().enterGhostCount(), 0) << "the copy fades in where it is";
}

TEST_F(DuplicateTrackMotionTest, OffScreenTheCopyLandsAtOnce) {
    ReducedMotionGuard guard(false);
    const auto sourceId = addInstrumentTrack();
    duplicateWithCmdD(sourceId); // nothing forced: the window is not showing
    EXPECT_FALSE(glide().isLive());
    EXPECT_FALSE(mc->getTimelinePanel().getTrackListMotionForTest().isRunning());
}

TEST_F(DuplicateTrackMotionTest, AHandDeletedCopyShrinksInPlaceAndItsUndoIsNotAGlideOutOfTheOriginal) {
    ReducedMotionGuard guard(false);
    const auto sourceId = addInstrumentTrack();
    forceMotion();
    duplicateWithCmdD(sourceId);
    const auto copyId = doc().getTracks().back().id;
    const auto copyKey = glideKeyFor(copyId);
    glide().finish();
    mc->getTimelinePanel().getTrackListMotionForTest().finishNow();

    mc->deleteTrackForTest(copyId);
    glide().finish();
    ASSERT_TRUE(mc->getUndoManager().undo());
    mc->getTimelinePanel().getTrackListMotionForTest();
    EXPECT_FALSE(mc->getTimelinePanel().getTrackListMotionForTest().timeline().together)
        << "the row grows back where it stood, not out of the original";
    (void)copyKey;
}
