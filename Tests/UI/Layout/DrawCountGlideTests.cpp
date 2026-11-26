// DrawCountGlideTests.cpp -- the guard behind "nothing pops": a Draw box's preview wave eases into the wave a new
// cycle count makes (Option+drag up or down, `[` and `]`). Every Draw surface is registered in drawSurfaces() below
// (the shape window's pen and the timeline lane's Draw box); a surface whose count change snaps fails here by name.
// Frames are stepped with the tween's own clock, like UndoGlideSurfaces.
#include "../Timeline/AutomationLanes/CountLane.h"
#include "DrawCountGlideSurfaces.h"
#include "Timeline/AutomationKernel.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/PointEditing/DrawCountGlide.h"
#include "UI/Layout/PointEditing/LineMorph.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace draw_count_glide_test;

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

class LaneDrawBox : public DrawSurface {
public:
    void begin(DrawShape shape, int cycles) override {
        lane_ = std::make_unique<automation_lanes_test::CountLane>(shape);
        lane_->gesture().setCycleWidthBeats(5.0 / cycles); // the box is always 5 beats wide
        lane_->press(0.0, 5.0);
        glide().tweenForTest().forceAnimateForTest(true);
    }
    void step(int delta) override {
        lane_->key(delta > 0 ? automation_lanes_test::kClose : automation_lanes_test::kOpen);
    }
    Line drawn() override { return lane_->gesture().drawnPreviewLine(); }
    std::string chip() override { return lane_->chip().toStdString(); }
    DrawCountGlide& glide() override { return lane_->gesture().getCountGlideForTest(); }
    juce::Component& owner() override { return *lane_->editor; }
    Line release() override {
        lane_->release();
        const auto& points = lane_->theLane().points;
        std::vector<synth::TimelineSnapshot::Point> pts;
        for (const auto& bp : points)
            pts.push_back({bp.beat, bp.value, bp.tension, bp.curve});
        Line line;
        if (pts.empty())
            return line;
        synth::AutomationCursor cursor{};
        const int x0 = (int)lane_->xAt(0.0);
        const int x1 = (int)lane_->xAt(points.back().beat);
        for (int x = x0; x <= x1; ++x) {
            const double v = synth::AutomationKernel::evaluate(
                pts.data(), (int)pts.size(), lane_->panel.getViewState().xToBeat((double)x), 0.0, cursor);
            line.push_back({(float)x, (float)lane_->editor->valueToY(v)});
        }
        return line;
    }
    bool byLength(DrawShape) const override { return false; }

private:
    std::unique_ptr<automation_lanes_test::CountLane> lane_;
};

struct Entry {
    const char* name;
    std::function<std::unique_ptr<DrawSurface>()> make;
};

/** Every Draw surface: a new one that pops on a count change fails the tests below by this name. */
const std::vector<Entry>& drawSurfaces() {
    static const std::vector<Entry> list = {
        {"Edit shapes window pen", [] { return makeShapeWindowPen(); }},
        {"Timeline lane Draw box", [] { return std::make_unique<LaneDrawBox>(); }},
    };
    return list;
}

constexpr DrawShape kShapes[] = {DrawShape::Sine, DrawShape::Square};

float distanceToLine(juce::Point<float> p, const Line& line) {
    float best = 1e9f;
    for (size_t i = 1; i < line.size(); ++i) {
        const juce::Line<float> seg(line[i - 1], line[i]);
        juce::Point<float> nearest;
        best = std::min(best, seg.getDistanceFromPoint(p, nearest));
    }
    return line.size() == 1 ? p.getDistanceFrom(line[0]) : best;
}

/** The largest distance of any point of `a` from the polyline `b`. */
float farthest(const Line& a, const Line& b) {
    float worst = 0.0f;
    for (const auto& p : a)
        worst = std::max(worst, distanceToLine(p, b));
    return worst;
}

float apart(const Line& a, const Line& b) { return std::max(farthest(a, b), farthest(b, a)); }

/** The wave `surface` shows resting at `cycles`. */
Line resting(const Entry& entry, DrawShape shape, int cycles) {
    auto s = entry.make();
    s->begin(shape, cycles);
    return s->drawn();
}

} // namespace

TEST(DrawCountGlide, EveryDrawSurfaceEasesBetweenCycleCountsInBothDirections) {
    FullMotion motion;
    for (const auto& entry : drawSurfaces())
        for (const auto shape : kShapes)
            for (const int delta : {1, -1}) {
                SCOPED_TRACE(std::string(entry.name) + " " + synth::ui::drawShapeName(shape) +
                             (delta > 0 ? " 5 -> 6" : " 6 -> 5"));
                const int from = delta > 0 ? 5 : 6;
                const auto before = resting(entry, shape, from);
                const auto after = resting(entry, shape, from + delta);
                ASSERT_GT(apart(before, after), 6.0f) << "the two counts draw different waves";

                auto s = entry.make();
                s->begin(shape, from);
                std::vector<juce::Rectangle<int>> repainted;
                synth::ui::setKeyedTweenDirtyProbeForTest([&](juce::Component& owner, juce::Rectangle<int> area) {
                    if (&owner == &s->owner())
                        repainted.push_back(area);
                });
                s->step(delta);
                synth::ui::setKeyedTweenDirtyProbeForTest({});

                EXPECT_TRUE(s->glide().isGliding()) << "a glide runs";
                EXPECT_LT(apart(s->drawn(), before), 1.5f) << "the first frame is still the old wave";
                EXPECT_NE(s->chip().find(std::to_string(from + delta) + " cycles"), std::string::npos)
                    << "the chip's number is instant: " << s->chip();
                ASSERT_FALSE(repainted.empty());
                for (const auto& area : repainted)
                    EXPECT_EQ(area, s->owner().getLocalBounds()) << "the whole editor repaints each frame";

                s->glide().tweenForTest().stepForTest(60.0);
                const auto middle = s->drawn();
                EXPECT_GT(farthest(middle, before), 1.5f) << "it has left the old wave";
                EXPECT_GT(farthest(middle, after), 1.5f) << "it has not snapped to the new one";
                float lo = 1e9f, hi = -1e9f;
                for (const auto* line : {&before, &after})
                    for (const auto& p : *line) {
                        lo = std::min(lo, p.y);
                        hi = std::max(hi, p.y);
                    }
                for (const auto& p : middle) {
                    EXPECT_GE(p.y, lo - 1.0f);
                    EXPECT_LE(p.y, hi + 1.0f) << "never overshoots the two waves";
                }
                if (!s->byLength(shape))
                    for (const auto& p : middle) {
                        const float a = synth::ui::line_morph::lineYAt(before, p.x);
                        const float b = synth::ui::line_morph::lineYAt(after, p.x);
                        const bool stepHere = std::abs(a - b) > 8.0f && shape == DrawShape::Square;
                        if (!stepHere) {
                            EXPECT_GE(p.y, std::min(a, b) - 4.0f) << "at x " << p.x;
                            EXPECT_LE(p.y, std::max(a, b) + 4.0f) << "at x " << p.x;
                        }
                    }

                s->glide().tweenForTest().stepForTest(1000.0);
                EXPECT_FALSE(s->glide().isGliding());
                EXPECT_LT(apart(s->drawn(), after), 1.5f) << "it settles on the new wave";
            }
}

TEST(DrawCountGlide, AStepMidGlideContinuesFromTheDrawnLineAndNeverQueues) {
    FullMotion motion;
    for (const auto& entry : drawSurfaces())
        for (const auto shape : kShapes) {
            SCOPED_TRACE(std::string(entry.name) + " " + synth::ui::drawShapeName(shape));
            auto s = entry.make();
            s->begin(shape, 5);
            s->step(1);
            s->glide().tweenForTest().stepForTest(70.0);
            const auto drawnNow = s->drawn();
            s->step(1); // a held `]`: 5 -> 6 -> 7
            EXPECT_LT(apart(s->drawn(), drawnNow), 1.5f) << "the next step starts from the line on screen";
            s->glide().tweenForTest().stepForTest(1000.0);
            EXPECT_LT(apart(s->drawn(), resting(entry, shape, 7)), 1.5f) << "one glide to the last count";
            EXPECT_FALSE(s->glide().isGliding());
        }
}

TEST(DrawCountGlide, ReduceMotionAndAnimationsOffLandTheNewWaveAtOnce) {
    for (const bool off : {false, true})
        for (const auto& entry : drawSurfaces())
            for (const auto shape : kShapes) {
                SCOPED_TRACE(std::string(entry.name) + " " + synth::ui::drawShapeName(shape) +
                             (off ? " (Animations Off)" : " (Reduce Motion)"));
                const auto after = resting(entry, shape, 6);
                auto s = entry.make();
                s->begin(shape, 5);
                if (off)
                    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
                else
                    synth::ui::setReducedMotionForTest(true);
                s->step(1);
                EXPECT_FALSE(s->glide().isGliding());
                EXPECT_LT(apart(s->drawn(), after), 1.5f);
                synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
                synth::ui::setReducedMotionForTest(std::nullopt);
            }
}

TEST(DrawCountGlide, TheCommittedWaveIsTheNewCountAtOnceAndTheRestingPreviewEqualsIt) {
    FullMotion motion;
    for (const auto& entry : drawSurfaces())
        for (const auto shape : kShapes) {
            SCOPED_TRACE(std::string(entry.name) + " " + synth::ui::drawShapeName(shape));
            auto s = entry.make();
            s->begin(shape, 5);
            s->step(1);
            ASSERT_TRUE(s->glide().isGliding());
            const auto committed = s->release(); // mid-glide: the stamp is the new count, not a blend
            EXPECT_FALSE(s->glide().isGliding()) << "the glide ends with the stroke";
            const auto after = resting(entry, shape, 6);
            // The committed line also holds what lay outside the box; every point of the box's wave must lie on it.
            EXPECT_LT(farthest(after, committed), 1.5f) << "what the drop writes is the wave the preview rests at";
        }
}
