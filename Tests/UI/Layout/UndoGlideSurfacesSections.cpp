// UndoGlideSurfacesSections.cpp -- the registry fixtures for section moves: the clips of a moved section in the clip
// lanes, and the block itself in the Sections strip (docs/layout/undo-glide.md). Both views are refreshed on every doc
// notification, the way the timeline panel does it.
#include "../Timeline/TimelineClipLane/TimelineClipLaneTestFixture.h"
#include "../Timeline/TimelineSectionStrip/TimelineSectionStripFixture.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Timeline/TimelineClipLaneArea/ClipLaneEnterExit.h"
#include "UndoGlideSurfaces.h"

namespace undo_glide_test {

namespace {

constexpr double kSpb = 0.5;

/** The Intro's clip, moved by covering the Verse (whose clip leaves, and comes back on undo). */
class SectionClipsSurface
    : public Surface
    , private synth::TimelineDoc::Listener {
public:
    SectionClipsSurface() {
        const auto track = f_.doc.addTrack(synth::TrackKind::Midi, "Track 1");
        clip_ = f_.doc.addClip(track, 0.0, 4.0, "Intro clip");
        f_.doc.addNote(clip_, makeNote(0.0, 60, 1.0));
        f_.doc.addClip(track, 16.0, 4.0, "Verse clip");
        f_.doc.makeSection(0.0, 8.0, "Intro", 0, kSpb);
        f_.doc.makeSection(16.0, 24.0, "Verse", 0, kSpb);
        intro_ = f_.doc.getPlacements().front().id;
        f_.lane.refreshFromDoc();
        f_.lane.getClipGlideForTest().forceAnimateForTest(true);
        f_.lane.getEnterExitForTest().forceAnimateForTest(true);
        f_.doc.addListener(this);
    }
    ~SectionClipsSurface() override { f_.doc.removeListener(this); }

    void move() override {
        f_.undo.recordTimelineChange(
            f_.doc, [this] { f_.doc.movePlacement(intro_, 16.0, synth::SectionDropMode::Cover, kSpb); });
    }
    bool undo() override { return f_.undo.undo(); }
    bool redo() override { return f_.undo.redo(); }
    double drawn() const override { return f_.lane.getDrawnClipRectForTest(clip_).getX(); }
    bool gliding() const override {
        return f_.lane.getClipGlideForTest().isAnimating() || f_.lane.getEnterExitForTest().isExitingForTest();
    }
    void stepTo(double elapsedMs) override {
        f_.lane.getClipGlideForTest().stepForTest(elapsedMs);
        f_.lane.getEnterExitForTest().stepForTest(elapsedMs);
    }
    float alpha() const override { return f_.lane.getClipGlideForTest().alphaFor(clip_.value); }
    juce::Component* paintedComponent() override { return &f_.lane; }

private:
    void timelineChanged(const synth::TimelineDoc&) override { f_.lane.refreshFromDoc(); }

    mutable ClipLaneFixture f_;
    synth::ClipId clip_;
    synth::PlacementId intro_;
};

/** The Intro block, dragged on the strip past the Verse (make room). */
class SectionBlockSurface
    : public Surface
    , private synth::TimelineDoc::Listener {
public:
    SectionBlockSurface() {
        f_.doc.addClip(f_.track, 0.0, 4.0, "Intro clip");
        f_.doc.addClip(f_.track, 8.0, 4.0, "Verse clip");
        f_.doc.makeSection(0.0, 8.0, "Intro", 0xff56D4C0, kSpb);
        f_.doc.makeSection(8.0, 16.0, "Verse", 0xff4FC1FF, kSpb);
        f_.strip.refresh();
        intro_ = f_.doc.getPlacements().front().id;
        f_.strip.getDropGlideForTest().forceAnimateForTest(true);
        f_.doc.addListener(this);
    }
    ~SectionBlockSurface() override { f_.doc.removeListener(this); }

    void move() override {
        const juce::Point<float> from{f_.x(2.0), 12.0f}, to{f_.x(10.0), 12.0f};
        f_.strip.mouseDown(f_.event(from, from, false));
        f_.strip.mouseDrag(f_.event(to, from, true));
        f_.strip.mouseUp(f_.event(to, from, true));
    }
    bool undo() override { return f_.undo.undo(); }
    bool redo() override { return f_.undo.redo(); }
    double drawn() const override { return f_.state.beatToX(f_.strip.getDrawnStartBeatForTest(intro_)); }
    bool gliding() const override { return f_.strip.getDropGlideForTest().isAnimating(); }
    void stepTo(double elapsedMs) override {
        f_.strip.getDropGlideForTest().stepForTest(elapsedMs);
        f_.strip.getMotionForTest().stepForTest(elapsedMs);
    }
    float alpha() const override { return f_.strip.getDropGlideForTest().alphaFor(intro_.value); }
    juce::Component* paintedComponent() override { return &f_.strip; }

private:
    void timelineChanged(const synth::TimelineDoc&) override { f_.strip.refresh(); }

    mutable sectionstrip_test::StripFixture f_;
    synth::PlacementId intro_;
};

} // namespace

std::unique_ptr<Surface> makeSectionMoveClips() { return std::make_unique<SectionClipsSurface>(); }
std::unique_ptr<Surface> makeSectionStripBlock() { return std::make_unique<SectionBlockSurface>(); }

} // namespace undo_glide_test
