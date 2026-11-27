// CurveEditorComponent -- an undo or redo that moves the nodes of a same-shaped curve: they are drawn gliding from
// where they were (KeyedPointTween). One that changes the number of points (a drawn stretch undone or redone) has no
// node to glide, so the whole line morphs: both lines are sampled at every x either one has and the drawn level eases
// from the old line's to the new one's. The class is declared in CurveEditorComponent.h.

#include "CurveEditorComponent.h"
#include "UI/Layout/PointEditing/LineMorph.h"
#include <cmath>

namespace synth::ui {

std::vector<juce::Point<float>> CurveEditorComponent::nodePositions() const {
    std::vector<juce::Point<float>> positions;
    const auto geometry = currentGeometry();
    for (int i = 0; i < model_.getNumNodes(); ++i)
        positions.push_back(geometry.nodePosition(i));
    return positions;
}

// A node still gliding from an earlier step continues from where it is drawn, so back-to-back undos stay smooth.
void CurveEditorComponent::glideNodesFrom(const std::vector<juce::Point<float>>& before) {
    const auto after = nodePositions();
    if (before.size() != after.size())
        return;
    for (size_t i = 0; i < after.size(); ++i)
        pointTween_.retarget((KeyedPointTween::Key)i, pointTween_.drawnAt((KeyedPointTween::Key)i, before[i]), after[i],
                             true);
}

float CurveEditorComponent::drawnBend(int seg) const { return model_.getBend(seg) + bendTween_.offsetFor(seg).x; }

std::vector<float> CurveEditorComponent::drawnBends() const {
    std::vector<float> bends;
    for (int seg = 0; seg < model_.getNumSegments(); ++seg)
        bends.push_back(drawnBend(seg));
    return bends;
}

CurveModel CurveEditorComponent::drawnModel() const {
    CurveModel shown = model_;
    if (bendTween_.isAnimating())
        for (int seg = 0; seg < shown.getNumSegments(); ++seg)
            shown.setBend(seg, juce::jlimit(-1.0f, 1.0f, drawnBend(seg)));
    return shown;
}

// A segment's bend glides like the nodes beside it: from the bend drawn now (a bend still gliding from an earlier step
// continues from where it is) to the restored one, so the line between two gliding dots bows into place with them.
void CurveEditorComponent::glideBendsFrom(const std::vector<float>& before) {
    if ((int)before.size() != model_.getNumSegments())
        return;
    for (int seg = 0; seg < model_.getNumSegments(); ++seg) {
        const float now = model_.getBend(seg);
        if (std::abs(before[(size_t)seg] - now) < 1e-6f) {
            bendTween_.erase(seg);
            continue;
        }
        bendTween_.retarget(seg, {before[(size_t)seg], 0.0f}, {now, 0.0f}, true);
    }
}

// The weight runs 1 -> 0 through the shared tween, so the morph takes the app's glide time, eases like every other
// restore and is skipped under Animations Off; Reduce Motion lands the new line and fades it in (alphaFor).
void CurveEditorComponent::morphFrom(std::vector<juce::Point<float>> before) {
    if (before.empty()) {
        morphTween_.clear();
        morphFrom_.clear();
        editMorph_ = {};
        return;
    }
    morphFrom_ = std::move(before);
    morphTween_.retarget(0, {0.0f, 1.0f}, {0.0f, 0.0f}, true);
}

std::vector<juce::Point<float>> CurveEditorComponent::lineBeforeEdit() const {
    return drawnCurvePoints(currentGeometry());
}

// The one path for a changed number of points: an undo or redo (setModel), a delete and an add. Dots that glided a key
// move or a restore belong to the old shape, so they stop; the line eases from what was drawn into the new shape.
void CurveEditorComponent::glideLineFrom(std::vector<juce::Point<float>> before) {
    glideLineFrom(std::move(before), EditMorph{});
}

void CurveEditorComponent::glideLineFrom(std::vector<juce::Point<float>> before, EditMorph edit) {
    pointTween_.clear();
    bendTween_.clear();
    morphFrom(std::move(before));
    editMorph_ = edit;
}

// `bigger` has one dot more than `smaller`: the index of the extra dot when dropping it leaves `smaller` (same dots,
// same bend on every segment the extra dot does not touch), else -1. The first match wins for identical stacked dots.
static int extraDotIndex(const CurveModel& bigger, const CurveModel& smaller) {
    const auto sameDot = [](const CurveNode& a, const CurveNode& b) {
        return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-6f;
    };
    const int n = smaller.getNumNodes();
    for (int extra = 0; extra <= n; ++extra) {
        bool match = true;
        for (int i = 0; i < n && match; ++i)
            match = sameDot(smaller.getNode(i), bigger.getNode(i < extra ? i : i + 1));
        // The segments the extra dot touches (extra - 1 and extra in `bigger`) may differ; every other bend must not.
        for (int seg = 0; seg < smaller.getNumSegments() && match; ++seg) {
            if (seg == extra - 1)
                continue; // the segment that is split or merged
            const int inBigger = seg < extra - 1 ? seg : seg + 1;
            match = std::abs(smaller.getBend(seg) - bigger.getBend(inBigger)) < 1e-6f;
        }
        if (match)
            return extra;
    }
    return -1;
}

std::optional<CurveEditorComponent::EditMorph> CurveEditorComponent::singleDotEdit(const CurveModel& before,
                                                                                   const CurveModel& after) {
    if (before.getMode() != after.getMode() || before.getMode() != CurveMode::Free)
        return std::nullopt;
    const int delta = after.getNumNodes() - before.getNumNodes();
    if (delta == 1) { // an add: the new dot grows in, the two segments it splits fade their handles in
        const int index = extraDotIndex(after, before);
        if (index >= 0)
            return EditMorph{true, index, index - 1, index};
    } else if (delta == -1) { // a delete: the merged segment's handle fades in
        const int index = extraDotIndex(before, after);
        if (index >= 0)
            return EditMorph{true, -1, index - 1, index - 1};
    }
    return std::nullopt;
}

// An undo or redo that changes the point count by anything but one dot hides every dot and bend handle while the line
// morphs (the old dots are not the new ones). An add or delete, and an undo or redo of one, keeps them: the dots that
// exist on both sides stay put and drawn while the line eases, so only the dot that was added, or the bend handle of a
// segment that merged or split, fades in. Reduce Motion's fade-in of the landed line (morphTween_.alphaFor) applies to
// what is new, never to what was already there.
float CurveEditorComponent::dotMorphFade(int node) const {
    if (editMorph_.dotsStay && node != editMorph_.newNode)
        return 1.0f;
    return (1.0f - morphWeight()) * morphTween_.alphaFor(0);
}

float CurveEditorComponent::dotMorphScale(int node) const {
    if (!editMorph_.dotsStay || node != editMorph_.newNode)
        return 1.0f;
    return juce::jlimit(0.0f, 1.0f, 1.0f - morphWeight());
}

float CurveEditorComponent::bendMorphFade(int seg) const {
    if (editMorph_.dotsStay && (seg < editMorph_.firstFadeSeg || seg > editMorph_.lastFadeSeg))
        return 1.0f;
    return (1.0f - morphWeight()) * morphTween_.alphaFor(0);
}

std::vector<CurveEditorComponent::DrawnDot> CurveEditorComponent::getDrawnDotsForTest() const {
    std::vector<DrawnDot> dots;
    const auto geometry = currentGeometry();
    for (int i = 0; i < model_.getNumNodes(); ++i)
        dots.push_back(
            {geometry.nodePosition(i) + pointTween_.offsetFor(i), pointTween_.alphaFor(i) * dotMorphFade(i)});
    return dots;
}

float CurveEditorComponent::morphWeight() const {
    return morphFrom_.empty() ? 0.0f : juce::jlimit(0.0f, 1.0f, morphTween_.offsetFor(0).y);
}

// The blend keeps every sample of either line in x order, or blends by length where dots stack at one x
// (line_morph::blend).
std::vector<juce::Point<float>> CurveEditorComponent::drawnCurvePoints(const CurveEditorGeometry& geometry) const {
    return line_morph::blend(morphFrom_, modelCurvePoints(geometry), morphWeight());
}

} // namespace synth::ui
