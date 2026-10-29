// UndoGlideSurfacesClipContents.cpp -- the registry fixture for the notes drawn inside a MIDI clip whose edge was
// dragged (docs/layout/undo-glide.md). One surface per sizing mode and edge, built by looping over both lists, so a new
// mode or edge is covered the day it exists. Where the sizing rule leaves the notes on their beat (Trim, and Carry on
// the right edge) the notes must instead stay put under the sliding edge; that is the second test.
#include "../Timeline/TimelineClipLane/TimelineClipLaneTestFixture.h"
#include "UI/Layout/ReducedMotion.h"
#include "UndoGlideSurfaces.h"
#include <array>
#include <cmath>
#include <list>
#include <string>

namespace undo_glide_test {

namespace {

using synth::ClipEdge;
using synth::ClipSizingMode;

constexpr std::array kModes{ClipSizingMode::Trim, ClipSizingMode::CarryContents, ClipSizingMode::Stretch};
constexpr std::array kEdges{ClipEdge::Left, ClipEdge::Right};
constexpr double kClipStart = 2.0;
constexpr double kClipLength = 4.0;
constexpr float kDragPx = 52.0f; // 1.25 beats at 40 px per beat, on the Quarter snap
constexpr int kProbePitch = 64;

synth::Clip fixtureClip() {
    synth::Clip clip;
    clip.startBeat = kClipStart;
    clip.lengthBeats = kClipLength;
    clip.notes = {makeNote(0.0, 60, 1.0), makeNote(1.5, kProbePitch, 1.0)};
    return clip;
}

/** The drag's pixel offset: the left edge goes right, the right edge goes left, so the clip shrinks either way. */
double dragBeats(ClipEdge edge) { return edge == ClipEdge::Left ? 1.25 : -1.25; }

/** True when the sizing rule moves the probe note (the last one) to another beat on the timeline. */
bool probeNoteMoves(ClipSizingMode mode, ClipEdge edge) {
    const auto clip = fixtureClip();
    const double newStart = edge == ClipEdge::Left ? clip.startBeat + dragBeats(edge) : clip.startBeat;
    const double newLength =
        edge == ClipEdge::Left ? clip.lengthBeats - dragBeats(edge) : clip.lengthBeats + dragBeats(edge);
    const auto result = synth::clipEdgeResult(clip, edge, mode, newStart, newLength);
    const double before = clip.startBeat + clip.notes.back().startBeat;
    const double after = result.startBeat + result.notes.back().startBeat;
    return std::abs(after - before) > 0.1;
}

class ClipContentsSurface : public Surface {
public:
    ClipContentsSurface(ClipSizingMode mode, ClipEdge edge)
        : edge_(edge) {
        const auto track = f_.doc.addTrack(synth::TrackKind::Midi, "Track 1");
        clip_ = f_.doc.addClip(track, kClipStart, kClipLength, "Clip");
        for (const auto& note : fixtureClip().notes)
            f_.doc.addNote(clip_, note);
        f_.lane.setClipSizingMode(mode);
        f_.lane.refreshFromDoc();
        f_.lane.getClipGlideForTest().forceAnimateForTest(true);
    }

    void move() override {
        const auto rect = f_.lane.getClipRect(clip_);
        const float grabX = edge_ == ClipEdge::Left ? (float)rect.getX() + 3.0f : (float)rect.getRight() - 3.0f;
        const juce::Point<float> grab(grabX, (float)rect.getCentreY());
        const juce::Point<float> dragged(grab.x + (float)dragBeats(edge_) * 40.0f, grab.y);
        f_.lane.mouseDown(leftClick(f_.lane, grab));
        f_.lane.mouseDrag(leftDrag(f_.lane, dragged, grab));
        f_.lane.mouseUp(leftDrag(f_.lane, dragged, grab));
        f_.lane.refreshFromDoc();
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override { return f_.lane.getDrawnNoteXForTest(clip_, probeIndex()); }
    bool gliding() const override { return f_.lane.getClipGlideForTest().isAnimating(); }
    void stepTo(double elapsedMs) override { f_.lane.getClipGlideForTest().stepForTest(elapsedMs); }
    float alpha() const override { return f_.lane.getClipGlideForTest().alphaFor(clip_.value); }
    juce::Component* paintedComponent() override { return &f_.lane; }

    ClipLaneFixture& fixture() { return f_; }
    synth::ClipId clip() const { return clip_; }
    int probeIndex() const {
        const auto* clip = f_.doc.getClip(clip_);
        return clip != nullptr && !clip->notes.empty() ? (int)clip->notes.size() - 1 : 0;
    }

private:
    bool restore(bool forward) {
        const bool did = forward ? f_.undo.redo() : f_.undo.undo();
        f_.lane.refreshFromDoc(); // the panel's doc listener
        return did;
    }

    mutable ClipLaneFixture f_;
    synth::ClipId clip_;
    ClipEdge edge_;
};

std::string label(ClipSizingMode mode, ClipEdge edge) {
    return std::string("Timeline clip notes, ") + synth::clipSizingModeKey(mode) +
           (edge == ClipEdge::Left ? ", left edge" : ", right edge");
}

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

/** The brightest white-ish note line within a row or two of `y` at `x`, against the clip fill a few rows below it. */
bool noteLineAt(const juce::Image& image, int x, int y) {
    int best = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        const auto on = image.getPixelAt(x, y + dy);
        const auto off = image.getPixelAt(x, y + 8);
        const int diff = std::abs((int)on.getRed() - (int)off.getRed()) +
                         std::abs((int)on.getGreen() - (int)off.getGreen()) +
                         std::abs((int)on.getBlue() - (int)off.getBlue());
        best = std::max(best, diff);
    }
    return best > 40;
}

juce::Image paintLane(TimelineClipLaneArea& lane) {
    juce::Image image(juce::Image::ARGB, lane.getWidth(), lane.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    lane.paintEntireComponent(g, false);
    return image;
}

} // namespace

void appendClipContentsEntries(std::vector<Entry>& list) {
    static std::list<std::string> names; // the entries point at these
    for (const auto mode : kModes)
        for (const auto edge : kEdges) {
            if (!probeNoteMoves(mode, edge))
                continue;
            names.push_back(label(mode, edge));
            list.push_back({names.back().c_str(), true,
                            [mode, edge] { return std::make_unique<ClipContentsSurface>(mode, edge); }});
        }
}

} // namespace undo_glide_test

using namespace undo_glide_test;

// Where the rule keeps the notes on their beat, the glide must not move them: the edge slides over them.
TEST(UndoGlideClipContents, NotesThatKeepTheirBeatStayPutWhileTheEdgeSlides) {
    int checked = 0;
    for (const auto mode : kModes)
        for (const auto edge : kEdges) {
            if (probeNoteMoves(mode, edge))
                continue;
            ++checked;
            SCOPED_TRACE(label(mode, edge));
            FullMotion motion;
            ClipContentsSurface s(mode, edge);
            const double before = s.drawn();
            s.move();
            s.stepTo(1000.0);
            ASSERT_NEAR(s.drawn(), before, 1.5) << "the drag left the probe note on its beat";
            ASSERT_TRUE(s.undo());
            EXPECT_TRUE(s.gliding());
            for (const double at : {0.0, 40.0, 1000.0}) {
                s.stepTo(at);
                EXPECT_NEAR(s.drawn(), before, 1.5) << "at " << at << " ms";
            }
            ASSERT_TRUE(s.redo());
            EXPECT_NEAR(s.drawn(), before, 1.5);
        }
    EXPECT_GT(checked, 0);
}

// The first painted frame after an undo shows the note lines where they were before it, not at their restored place.
TEST(UndoGlideClipContents, FirstFrameAfterUndoPaintsTheNoteLinesWhereTheyWere) {
    for (const auto mode : kModes)
        for (const auto edge : kEdges) {
            if (!probeNoteMoves(mode, edge))
                continue;
            SCOPED_TRACE(label(mode, edge));
            FullMotion motion;
            ClipContentsSurface s(mode, edge);
            const double restored = s.drawn();
            s.move();
            s.stepTo(1000.0);
            const double dragged = s.drawn();
            ASSERT_TRUE(s.undo());
            auto& lane = s.fixture().lane;
            const auto rect = lane.getDrawnClipRectForTest(s.clip());
            const auto* clip = s.fixture().doc.getClip(s.clip());
            const float pitchFrac = 1.0f - (float)kProbePitch / 127.0f;
            const int y = rect.getY() + 2 + (int)std::lround(pitchFrac * (float)(rect.getHeight() - 4));
            const auto image = paintLane(lane);
            ASSERT_NE(clip, nullptr);
            EXPECT_TRUE(noteLineAt(image, (int)std::lround(dragged) + 3, y)) << "the line sits where it was dragged to";
            if (std::abs(restored - dragged) > 45.0)
                EXPECT_FALSE(noteLineAt(image, (int)std::lround(restored) + 3, y)) << "and not at its restored place";
        }
}
