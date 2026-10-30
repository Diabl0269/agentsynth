// UndoGlideSurfacesTests.cpp -- the guard behind the standing rule that anything which glides forward glides on undo
// and redo: it walks UndoGlideSurfaces.h's one list, so a move path that snaps back fails here by name.
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
        EXPECT_NEAR(s->drawn(), before, 1.5) << "it is at its new place on the first frame";
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
