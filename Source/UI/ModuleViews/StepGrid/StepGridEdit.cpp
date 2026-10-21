// StepGridEdit.cpp -- editing the pattern with the mouse. One gesture (a pad stroke, a bar drag, a pitch drag, a menu
// pick) is ONE undo step: the module's extra state is snapshotted at the start and recorded once at the end, however
// many steps the stroke passed over. The grid's own edits update the "seen" pattern so only changes from outside
// (undo, a preset, the AI) glide.
#include "AudioEngine/GraphSnapshotCache.h"
#include "StepGridComponent.h"
#include <cmath>

namespace synth::ui {

using namespace stepgrid;

void StepGridComponent::editStep(int step, const std::function<void(SequencerStep&)>& edit) {
    if (step < 0 || step >= kMaxSequencerSteps)
        return;
    auto pattern = module_.getPattern();
    auto changed = pattern.steps[(size_t)step];
    edit(changed);
    if (changed == pattern.steps[(size_t)step])
        return;
    pattern.steps[(size_t)step] = changed;
    module_.setPattern(pattern);
    seenGeneration_ = module_.patternGeneration();
    lastSeen_ = module_.getPattern();
    repaint();
}

void StepGridComponent::commit(const juce::var& before) {
    const auto after = module_.getExtraState();
    if (!synth::sameJson(before, after) && hooks_.recordEdit)
        hooks_.recordEdit(before, after);
}

void StepGridComponent::setPlayedPitch(int step, int played) {
    const int written = juce::jlimit(0, 127, played - transpose_);
    editStep(step, [written](SequencerStep& s) { s.pitch = (std::uint8_t)written; });
}

int StepGridComponent::pitchAtY(int y, const StepGridLayout& layout) const {
    const auto lane = layout.lanes[(size_t)SequencerLane::Pitch];
    const float rowH = (float)lane.getHeight() / (float)pitchRange_.rows();
    const int fromTop = (int)std::floor((float)(y - lane.getY()) / rowH);
    return juce::jlimit(pitchRange_.lo, pitchRange_.hi, pitchRange_.hi - fromTop);
}

void StepGridComponent::beginGesture(const Hit& hit) {
    gesture_ = {};
    gesture_.active = true;
    gesture_.start = hit;
    gesture_.before = module_.getExtraState();
    gesture_.frozenRange = pitchRange_;
    const auto& entry = module_.getPattern().steps[(size_t)hit.step];
    gesture_.paintOn =
        hit.kind == Hit::Kind::Pad ? !entry.on : (hit.lane == SequencerLane::Slide ? !entry.slide : true);
    gesture_.startPlayed = playedPitchOf(hit.step);
}

void StepGridComponent::endGesture() {
    if (!gesture_.active)
        return;
    const auto before = gesture_.before;
    gesture_ = {};
    refreshPitchRange();
    commit(before);
    repaint();
}

// Applies the stroke at `p`: every step from the last one touched to this one, so a fast drag leaves no gaps; a bar
// lane or the Pitch lane interpolates its value across them.
void StepGridComponent::applyAt(const Hit& hit, juce::Point<int> p, bool shift) {
    const auto layout = currentLayout();
    const int step = hit.step;
    const int from = gesture_.lastStep < 0 ? step : gesture_.lastStep;
    const int lo = std::min(from, step);
    const int hi = std::max(from, step);

    if (hit.kind == Hit::Kind::Pad) {
        for (int s = lo; s <= hi; ++s)
            editStep(s, [on = gesture_.paintOn](SequencerStep& e) { e.on = on; });
        gesture_.lastStep = step;
        return;
    }
    if (hit.lane == SequencerLane::Slide) {
        const double rel = (double)slidePos_ * kColumns + (double)(p.x - kGutterWidth) / columnWidth();
        const int boundary = juce::jlimit(0, kMaxSequencerSteps - 2, (int)std::floor(rel + 0.5) - 1);
        editStep(boundary, [on = gesture_.paintOn](SequencerStep& e) { e.slide = on; });
        gesture_.lastStep = step;
        return;
    }

    float value = 0.0f;
    if (hit.lane == SequencerLane::Pitch) {
        int played = pitchAtY(p.y, layout);
        played = scale_.snapPitch(played);
        if (shift) {
            // Octave only: keep the step's note and move it to the octave nearest the cursor.
            const int pitchClass = ((gesture_.startPlayed % 12) + 12) % 12;
            int best = pitchClass;
            for (int candidate = pitchClass; candidate <= 127; candidate += 12)
                if (std::abs(candidate - played) < std::abs(best - played))
                    best = candidate;
            played = scale_.snapPitch(best);
        }
        value = (float)played;
    } else {
        const auto box = layout.lanes[(size_t)hit.lane];
        const float h = (float)box.getHeight() - 4.0f;
        value = juce::jlimit(0.0f, 1.0f, 1.0f - ((float)p.y - ((float)box.getY() + 2.0f)) / h);
    }
    const float startValue = gesture_.lastStep < 0 ? value : gesture_.lastNormal;
    for (int s = lo; s <= hi; ++s) {
        const float t = hi == lo ? 1.0f : (float)(s - from) / (float)(step - from);
        const float v = from == step ? value : startValue + (value - startValue) * t;
        if (hit.lane == SequencerLane::Pitch)
            setPlayedPitch(s, scale_.snapPitch(juce::roundToInt(v)));
        else
            editStep(s, [lane = hit.lane, v](SequencerStep& e) { setLaneNormal(lane, e, v); });
    }
    gesture_.lastStep = step;
    gesture_.lastNormal = value;
}

void StepGridComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu())
        return;
    const auto hit = hitTest(e.getPosition());
    grabKeyboardFocus();
    if (hit.kind == Hit::Kind::None)
        return;
    setFocusStep(hit.step);
    if (hit.kind == Hit::Kind::Lane)
        focusLane_ = hit.lane;
    if (hit.kind == Hit::Kind::Lane && hit.lane == SequencerLane::Pitch && chordLabelAt(e.getPosition(), hit.step)) {
        openChordPicker(hit.step);
        return;
    }
    if (hit.kind == Hit::Kind::Lane && hit.lane == SequencerLane::Condition) {
        openConditionMenu(hit.step);
        return;
    }
    beginGesture(hit);
    applyAt(hit, e.getPosition(), e.mods.isShiftDown());
}

void StepGridComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!gesture_.active)
        return;
    const double firstStep = (double)slidePos_ * kColumns;
    const int pageFirst = (int)std::floor(firstStep + 0.5);
    const int pageLast = std::min(kMaxSequencerSteps - 1, pageFirst + kColumns - 1);
    int step = stepAtX(e.x);
    if (step < 0)
        step = e.x < kGutterWidth ? pageFirst : pageLast;
    Hit hit = gesture_.start;
    hit.step = juce::jlimit(pageFirst, pageLast, step);
    applyAt(hit, e.getPosition(), e.mods.isShiftDown());
}

void StepGridComponent::mouseUp(const juce::MouseEvent&) { endGesture(); }

void StepGridComponent::setStepCondition(int step, int hit, int of) {
    const auto before = module_.getExtraState();
    editStep(step, [hit, of](SequencerStep& s) {
        s.condHit = (std::uint8_t)hit;
        s.condOf = (std::uint8_t)of;
    });
    commit(before);
}

void StepGridComponent::openConditionMenu(int step) {
    const auto& entry = module_.getPattern().steps[(size_t)step];
    juce::PopupMenu menu;
    const auto& choices = conditionChoices();
    for (int i = 0; i < (int)choices.size(); ++i) {
        const auto [hit, of] = choices[(size_t)i];
        const bool current = entry.condHit == hit && entry.condOf == of;
        menu.addItem(i + 1, juce::String(hit) + "/" + juce::String(of), true, current);
    }
    juce::Component::SafePointer<StepGridComponent> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetScreenArea(localAreaToGlobal(cellBounds(SequencerLane::Condition, step)))
                           .withParentComponent(nullptr),
                       [safe, step](int result) {
                           if (safe == nullptr || result <= 0)
                               return;
                           const auto [hit, of] = conditionChoices()[(size_t)(result - 1)];
                           safe->setStepCondition(step, hit, of);
                       });
}

} // namespace synth::ui
