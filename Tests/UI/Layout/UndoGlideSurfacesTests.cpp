// UndoGlideSurfacesTests.cpp -- the guard behind the standing rule that anything which glides forward glides on undo
// and redo: it walks UndoGlideSurfaces.h's one list, so a move path that snaps back fails here by name.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/PolyChain/PolyChainController.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/ReducedMotion.h"
#include "UndoGlideSurfaces.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace undo_glide_test;

namespace {

struct FullMotion {
    FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        synth::ui::setReducedMotionForTest(false);
    }
    ~FullMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

// One restore step: drawn at `from` on the first frame, strictly between the two places a little later, settled at
// `to`.
void expectGlide(Surface& s, double from, double to, const char* what) {
    SCOPED_TRACE(what);
    ASSERT_GT(std::abs(to - from), 4.0) << "the move changed the place";
    EXPECT_TRUE(s.gliding()) << "a glide is running";
    EXPECT_NEAR(s.drawn(), from, 1.5) << "the first frame is still at the old place";
    s.stepTo(40.0);
    const double mid = s.drawn();
    const double fraction = (mid - from) / (to - from);
    EXPECT_GT(fraction, 0.02) << "it has left the old place";
    EXPECT_LT(fraction, 0.98) << "it has not snapped to the new one";
    s.stepTo(1000.0);
    EXPECT_FALSE(s.gliding()) << "it stops when settled";
    EXPECT_NEAR(s.drawn(), to, 1.5);
}

// Software-rendered (never the default image type or createComponentSnapshot: Windows CI reads zeros).
juce::Image render(juce::Component& c) {
    juce::Image img(juce::Image::ARGB, c.getWidth(), c.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(img);
    c.paintEntireComponent(g, false);
    return img;
}

// Clears and re-renders only `dirty` into `shown`: what the screen holds after those repaints and nothing else.
void repaintInto(juce::Image& shown, juce::Component& c, const juce::RectangleList<int>& dirty) {
    if (dirty.isEmpty())
        return;
    for (const auto& r : dirty)
        shown.clear(r); // transparent: nothing of the old frame shows through
    juce::Graphics g(shown);
    g.reduceClipRegion(dirty);
    c.paintEntireComponent(g, false);
}

int differingPixels(const juce::Image& a, const juce::Image& b, juce::Point<int>& first) {
    int count = 0;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x) {
            const auto p = a.getPixelAt(x, y);
            const auto q = b.getPixelAt(x, y);
            if (std::abs(p.getRed() - q.getRed()) > 8 || std::abs(p.getGreen() - q.getGreen()) > 8 ||
                std::abs(p.getBlue() - q.getBlue()) > 8 || std::abs(p.getAlpha() - q.getAlpha()) > 8) {
                if (count++ == 0)
                    first = {x, y};
            }
        }
    return count;
}

/** Collects what the tweens ask to repaint for one component while it lives. */
struct DirtyCollector {
    explicit DirtyCollector(juce::Component* c)
        : watched(c) {
        synth::ui::setKeyedTweenDirtyProbeForTest([this](juce::Component& owner, juce::Rectangle<int> area) {
            if (&owner == watched)
                rects.add(area);
        });
    }
    ~DirtyCollector() { synth::ui::setKeyedTweenDirtyProbeForTest({}); }
    juce::Component* watched;
    juce::RectangleList<int> rects;
};

// One restore step, frame by frame: the screen as the requested repaints leave it equals a full repaint, every frame.
void expectEveryFrameRepainted(Surface& s, juce::Component& c, bool undo, const char* what) {
    SCOPED_TRACE(what);
    DirtyCollector collected(&c);
    ASSERT_TRUE(undo ? s.undo() : s.redo());
    // The restore's own repaints (the owner's model refresh) are not the tween's: the screen starts as a full render.
    auto shown = render(c);
    collected.rects.clear();
    for (int frame = 1; frame <= 80; ++frame) {
        s.stepTo(16.0 * frame);
        repaintInto(shown, c, collected.rects);
        collected.rects.clear();
        juce::Point<int> where;
        const int bad = differingPixels(shown, render(c), where);
        ASSERT_EQ(bad, 0) << "frame " << frame << " (" << 16 * frame << " ms): " << bad
                          << " pixels differ from a full repaint, first at " << where.x << "," << where.y;
        if (!s.gliding())
            return;
    }
    FAIL() << "the glide never settled";
}

} // namespace

TEST(UndoGlideSurfaces, EveryGlideFrameRepaintsEverythingThatChanged) {
    for (const auto& entry : surfaces()) {
        for (const bool reduced : {false, true}) {
            SCOPED_TRACE(std::string(entry.name) + (reduced ? " (Reduce Motion)" : ""));
            if (reduced && !entry.fadesUnderReduceMotion)
                continue;
            FullMotion motion;
            auto s = entry.make();
            auto* c = s->paintedComponent();
            if (c == nullptr) {
                ASSERT_FALSE(entry.fadesUnderReduceMotion)
                    << "a surface drawn through a KeyedTween must return its component from paintedComponent()";
                continue;
            }
            if (c->getWidth() < 1 || c->getHeight() < 1)
                c->setSize(400, 200);
            s->move();
            s->stepTo(1000.0);
            if (reduced) {
                synth::ui::setAnimationMode(synth::ui::AnimationMode::reduced);
                synth::ui::setReducedMotionForTest(true);
            }
            expectEveryFrameRepainted(*s, *c, true, "undo");
            expectEveryFrameRepainted(*s, *c, false, "redo");
        }
    }
}

TEST(UndoGlideSurfaces, EveryMoveSurfaceGlidesOnUndoAndRedo) {
    ASSERT_FALSE(surfaces().empty());
    for (const auto& entry : surfaces()) {
        SCOPED_TRACE(entry.name);
        FullMotion motion;
        auto s = entry.make();
        const double before = s->drawn();
        s->move();
        s->stepTo(1000.0);
        const double moved = s->drawn();
        ASSERT_TRUE(s->undo());
        expectGlide(*s, moved, before, "undo glides back");
        ASSERT_TRUE(s->redo());
        expectGlide(*s, before, moved, "redo glides forward");
    }
}

// A snapped drag step eases: dragging a thing across a grid line with the button down draws it gliding from the old
// line to the new one, never popping, and Reduce Motion lands it at once. Surfaces with a snapped drag opt in through
// Surface::dragStep.
TEST(UndoGlideSurfaces, EverySnappedDragStepEases) {
    int covered = 0;
    for (const auto& entry : surfaces()) {
        SCOPED_TRACE(entry.name);
        FullMotion motion;
        auto probe = entry.make();
        const double from = probe->drawn();
        if (!probe->dragStep())
            continue;
        ++covered;
        probe->stepTo(1000.0);
        const double to = probe->drawn();
        probe->releaseDrag();

        auto s = entry.make();
        ASSERT_TRUE(s->dragStep());
        expectGlide(*s, from, to, "a snapped drag step glides");
        s->releaseDrag();
        EXPECT_NEAR(s->drawn(), to, 1.5) << "the drop does not jump";
        s->stepTo(1000.0);

        synth::ui::setAnimationMode(synth::ui::AnimationMode::reduced);
        synth::ui::setReducedMotionForTest(true);
        auto plain = entry.make();
        ASSERT_TRUE(plain->dragStep());
        EXPECT_FALSE(plain->gliding()) << "Reduce Motion lands the step at once";
        EXPECT_NEAR(plain->drawn(), to, 1.5);
        plain->releaseDrag();
    }
    EXPECT_GT(covered, 0);
}

TEST(UndoGlideSurfaces, UnderReduceMotionARestoredThingLandsAtOnceAndFadesIn) {
    for (const auto& entry : surfaces()) {
        SCOPED_TRACE(entry.name);
        FullMotion motion;
        auto s = entry.make();
        const double before = s->drawn();
        s->move();
        s->stepTo(1000.0);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::reduced);
        synth::ui::setReducedMotionForTest(true);
        ASSERT_TRUE(s->undo());
        if (entry.shortGlideUnderReduceMotion) {
            EXPECT_TRUE(s->gliding()) << "the shortened glide runs";
            s->stepTo(80.0);
            EXPECT_FALSE(s->gliding()) << "and has landed within the 80 ms of Reduce Motion";
        } else {
            EXPECT_NEAR(s->drawn(), before, 1.5) << "it is at its new place on the first frame";
        }
        if (entry.fadesUnderReduceMotion) {
            EXPECT_LT(s->alpha(), 0.9f) << "and starts faint";
            EXPECT_TRUE(s->gliding()) << "the fade runs";
            s->stepTo(1000.0);
        }
        EXPECT_FALSE(s->gliding()) << "settled";
        EXPECT_FLOAT_EQ(s->alpha(), 1.0f);
        EXPECT_NEAR(s->drawn(), before, 1.5);
    }
}

TEST(UndoGlideSurfaces, WithAnimationsOffEveryRestoreIsInstant) {
    for (const auto& entry : surfaces()) {
        SCOPED_TRACE(entry.name);
        FullMotion motion;
        auto s = entry.make();
        const double before = s->drawn();
        s->move();
        s->stepTo(1000.0);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
        ASSERT_TRUE(s->undo());
        EXPECT_FALSE(s->gliding());
        EXPECT_FLOAT_EQ(s->alpha(), 1.0f);
        EXPECT_NEAR(s->drawn(), before, 1.5);
    }
}

// ---- Structural graph edits: every entry point that adds or removes a card must glide like its undo ----
// A click that inserts or removes modules and cables (the Poly pill, the track header's Poly switch) runs through
// the same bracket Cmd+Z does (CardGlideAnimator::Scope + retractCablesGoneSince, see PolyChainController::commit).
// A new structural click adds a row to structuralEdits() so a version of it that pops fails here by name.
namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;

struct StructuralRig {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID source, osc, adsr, vca;

    StructuralRig() {
        engine.getGraph().setPlayConfigDetails(2, 2, 44100.0, 512);
        undo.setGraphEditor(&editor);
        editor.setSize(3000, 1600);
        editor.getCardGlideForTest().setForceAnimateForTest(true); // not on screen: the glide is what is under test
        source = add("Midi Input", 320);
        osc = add("Oscillator", 640);
        adsr = add("ADSR", 960);
        vca = add("VCA", 1280);
        const int midi = juce::AudioProcessorGraph::midiChannelIndex;
        engine.getGraph().addConnection({{source, midi}, {osc, midi}});
        engine.getGraph().addConnection({{source, midi}, {adsr, midi}});
        engine.getGraph().addConnection({{osc, 0}, {vca, 0}});
        engine.getGraph().addConnection({{adsr, 0}, {vca, 1}});
    }
    NodeID add(const juce::String& type, int x) {
        auto node = engine.getGraph().addNode(synth::AIStateMapper::createModule(type));
        node->properties.set("uuid", juce::Uuid().toDashedString());
        node->properties.set("x", x);
        node->properties.set("y", 100);
        editor.updateComponents();
        return node->nodeID;
    }
};

struct StructuralEdit {
    const char* name;
    bool startsPoly; // the rig is switched to poly first, so the edit removes the Poly MIDI node
    std::function<void(StructuralRig&)> run;
};

const std::vector<StructuralEdit>& structuralEdits() {
    static const std::vector<StructuralEdit> edits{
        {"Poly pill on", false, [](StructuralRig& r) { r.editor.getPolyChain().polyPillClicked(r.osc, true); }},
        {"Poly pill off", true, [](StructuralRig& r) { r.editor.getPolyChain().polyPillClicked(r.osc, false); }},
        {"Track header Poly switch on", false,
         [](StructuralRig& r) { r.editor.getPolyChain().trackPolyClicked(r.source, true); }},
        {"Track header Poly switch off", true,
         [](StructuralRig& r) { r.editor.getPolyChain().trackPolyClicked(r.source, false); }},
        // Return on a library row or a double click: no drag brought the card, so it grows in at its slot.
        {"Library add", false, [](StructuralRig& r) { r.editor.addModuleAtCanvasPosition("Filter", {320, 700}, {}); }},
    };
    return edits;
}

} // namespace

TEST(UndoGlideSurfaces, EveryStructuralGraphEditEntryPointGlidesLikeUndo) {
    ASSERT_FALSE(structuralEdits().empty());
    for (const auto& edit : structuralEdits()) {
        SCOPED_TRACE(edit.name);
        FullMotion motion;
        StructuralRig rig;
        auto& glide = rig.editor.getCardGlideForTest();
        if (edit.startsPoly) {
            rig.editor.getPolyChain().polyPillClicked(rig.osc, true);
            rig.editor.finishCardGlideForTest();
        }
        edit.run(rig);
        EXPECT_GE(glide.enterGhostCount() + glide.exitGhostCount(), 1)
            << "an added card grows in and a removed one shrinks away";
        rig.editor.finishCardGlideForTest();

        // The same edit under Animations Off is instant, never a glide.
        StructuralRig off;
        if (edit.startsPoly) {
            off.editor.getPolyChain().polyPillClicked(off.osc, true);
            off.editor.finishCardGlideForTest();
        }
        synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
        edit.run(off);
        EXPECT_EQ(
            off.editor.getCardGlideForTest().enterGhostCount() + off.editor.getCardGlideForTest().exitGhostCount(), 0);
    }
}
