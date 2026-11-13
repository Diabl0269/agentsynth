// UndoGlideSurfacesClipDrag.cpp -- the registry fixtures for the clip itself in the clip lane: its left edge, its right
// edge and its body, dragged across a grid line with snap on (docs/layout/undo-glide.md). One surface per part, built
// by looping over a list, so a new draggable part joins the snapped-drag test and the undo and redo tests by adding a
// row. drawn() is the clip's drawn left edge (the right edge for the right-edge drag).
#include "../Timeline/TimelineClipLane/TimelineClipLaneTestFixture.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UndoGlideSurfaces.h"
#include <list>
#include <string>

namespace undo_glide_test {

namespace {

enum class Part { LeftEdge, RightEdge, Body };

constexpr float kDragPx = 80.0f; // two grid lines at 40 px per beat on the Quarter snap

class ClipDragSurface : public Surface {
public:
    explicit ClipDragSurface(Part part)
        : part_(part) {
        const auto track = f_.doc.addTrack(synth::TrackKind::Midi, "Track 1");
        clip_ = f_.doc.addClip(track, 4.0, 8.0, "Clip");
        f_.doc.addNote(clip_, makeNote(1.0, 60, 1.0));
        f_.lane.refreshFromDoc();
        f_.lane.getClipGlideForTest().forceAnimateForTest(true);
    }

    void move() override {
        dragStep();
        releaseDrag();
    }
    bool dragStep() override {
        const auto rect = f_.lane.getClipRect(clip_);
        const float x = part_ == Part::LeftEdge    ? (float)rect.getX() + 3.0f
                        : part_ == Part::RightEdge ? (float)rect.getRight() - 3.0f
                                                   : (float)rect.getCentreX();
        grab_ = {x, (float)rect.getCentreY()};
        // The left edge drags into the clip, the right edge out of it, the body to the right.
        dragged_ = {grab_.x + (part_ == Part::RightEdge ? -kDragPx : kDragPx), grab_.y};
        f_.lane.mouseDown(leftClick(f_.lane, grab_));
        f_.lane.mouseDrag(leftDrag(f_.lane, dragged_, grab_));
        return true;
    }
    void releaseDrag() override {
        f_.lane.mouseUp(leftDrag(f_.lane, dragged_, grab_));
        f_.lane.refreshFromDoc();
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override {
        const auto rect = f_.lane.getDrawnClipRectForTest(clip_);
        return part_ == Part::RightEdge ? rect.getRight() : rect.getX();
    }
    bool gliding() const override { return f_.lane.getClipGlideForTest().isAnimating(); }
    void stepTo(double elapsedMs) override { f_.lane.getClipGlideForTest().stepForTest(elapsedMs); }
    float alpha() const override { return f_.lane.getClipGlideForTest().alphaFor(clip_.value); }
    juce::Component* paintedComponent() override { return &f_.lane; }

private:
    bool restore(bool forward) {
        const bool did = forward ? f_.undo.redo() : f_.undo.undo();
        f_.lane.refreshFromDoc();
        return did;
    }

    mutable ClipLaneFixture f_;
    synth::ClipId clip_;
    Part part_;
    juce::Point<float> grab_, dragged_;
};

} // namespace

void appendClipDragEntries(std::vector<Entry>& list) {
    static std::list<std::string> names; // the entries point at these
    for (const auto& [part, name] : {std::pair{Part::LeftEdge, "Timeline clip left edge drag"},
                                     std::pair{Part::RightEdge, "Timeline clip right edge drag"},
                                     std::pair{Part::Body, "Timeline clip body drag"}}) {
        names.emplace_back(name);
        list.push_back({names.back().c_str(), true, [part] { return std::make_unique<ClipDragSurface>(part); }});
    }
}

} // namespace undo_glide_test
