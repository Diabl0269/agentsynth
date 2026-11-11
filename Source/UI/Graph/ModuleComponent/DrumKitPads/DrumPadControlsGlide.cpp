// DrumPadControlsGlide.cpp -- how the pad panel moves when another pad is picked or an undo changes a knob: each knob
// draws its value gliding from the one it showed to the parameter's (the parameter is final at once), the knob columns
// slide when the pad's kind moves them, Start / Plays pitch fade in place, and the picked pad's mod ring fades in as
// the old pad's fades out. The panel itself never fades.
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/ModuleComponent/DrumKitPads/DrumPadControls.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

using PadParam = DrumKitModule::PadParam;

namespace {

constexpr size_t kKnobCount = 4;

juce::Rectangle<int> between(juce::Rectangle<int> a, juce::Rectangle<int> b, float t) {
    const auto mix = [t](int from, int to) { return from + juce::roundToInt((float)(to - from) * t); };
    return {mix(a.getX(), b.getX()), mix(a.getY(), b.getY()), mix(a.getWidth(), b.getWidth()),
            mix(a.getHeight(), b.getHeight())};
}

} // namespace

// A change the parameter made (a pick, an undo, automation). Only a pick and a restore glide: automation or a MIDI
// controller moving the knob keeps it on the parameter, frame for frame. A glide that was running continues from the
// value the knob shows, never from its start.
void DrumPadControls::knobFollowedModel(size_t f, double before, double after) {
    const bool restoring = isRestoring && isRestoring();
    const auto key = (KeyedPointTween::Key)f;
    const bool sameTarget = std::abs(resting_[f] - after) < 1.0e-9; // a repeat of the notification that began a glide
    resting_[f] = after;
    released_[f] = false;
    if ((glideBinding_ || restoring) && std::abs(before - after) > 1.0e-6) {
        knobs_[f].showProportion(before);
        valueGlide_.retarget(key, {(float)before, 0.0f}, {(float)after, 0.0f}, /*fadeWhenReduced=*/true, kGlideMs);
    } else if (!sameTarget) {
        valueGlide_.erase(key);
    }
    applyDrawnValues();
}

// Draws every knob at its resting value plus what is left of its glide. A knob the user has taken hold of keeps the
// value it shows. Under Reduce Motion the value is already at its place and the knob fades in there.
void DrumPadControls::applyDrawnValues() {
    for (size_t f = 0; f < kKnobCount; ++f) {
        auto& knob = knobs_[f];
        const bool startFading = f == (size_t)PadParam::Start && fadeStart_.isFading();
        const auto key = (KeyedPointTween::Key)f;
        if (!released_[f])
            knob.showProportion(valueGlide_.drawnAt(key, {(float)resting_[f], 0.0f}).x);
        if (!startFading)
            knob.setAlpha(released_[f] ? 1.0f : valueGlide_.alphaFor(key));
    }
}

// The card paints the pad knobs' mod rings under the panel, so each frame repaints the panel's place on the card too.
void DrumPadControls::repaintCardAroundKnobs() {
    if (auto* parent = getParentComponent())
        parent->repaint(getBounds());
    repaint();
}

double DrumPadControls::shownProportion(PadParam which) const { return knobs_[(size_t)which].shownProportion(); }

bool DrumPadControls::isMoving() const noexcept {
    return valueGlide_.isAnimating() || columnDriver_.isRunning() || ringDriver_.isRunning();
}

// Test mode: the pad the knobs are on, whether anything is moving, the panel's alpha, and for the three value knobs the
// proportion drawn now and the parameter's own.
void DrumPadControls::addTestModeState(juce::DynamicObject& into) const {
    into.setProperty("boundPad", bound_ + 1);
    into.setProperty("moving", isMoving());
    into.setProperty("panelAlphaPercent", juce::roundToInt(getAlpha() * 100.0f));
    const struct {
        const char* name;
        PadParam which;
    } rows[] = {{"level", PadParam::Level}, {"tune", PadParam::Tune}, {"decay", PadParam::Decay}};
    for (const auto& row : rows) {
        const auto& knob = knobs_[(size_t)row.which];
        into.setProperty(juce::String(row.name) + "DrawnPercent", juce::roundToInt(knob.shownProportion() * 100.0));
        into.setProperty(juce::String(row.name) + "ParamPercent",
                         juce::roundToInt(resting_[(size_t)row.which] * 100.0));
    }
}

void DrumPadControls::stepMotionForTest(double ms) {
    valueGlide_.stepForTest(ms);
    columnDriver_.stepForTest(ms);
    ringDriver_.stepForTest(ms);
}

void DrumPadControls::completeMotionForTest() {
    valueGlide_.completeForTest();
    columnDriver_.completeForTest();
    ringDriver_.completeForTest();
}

// ---- Columns ----

// Level, Tune and Decay always have a column; their width follows the fourth column (Start, or Plays pitch at two
// fifths of the row so its stepper has room for "C3 (60)").
DrumPadControls::ColumnRects DrumPadControls::columnRects(juce::Rectangle<int> area) const {
    const int columns = startOn_ || pitchOn_ ? 4 : 3;
    const int colW = area.getWidth() / (pitchOn_ ? 5 : columns);
    ColumnRects out;
    for (int f = 0; f < 3; ++f) {
        auto column = juce::Rectangle<int>(area.getX() + f * colW, area.getY(), colW, area.getHeight());
        out[(size_t)f * 2] = column.removeFromTop(synth::cardbody::kLabelHeight);
        out[(size_t)f * 2 + 1] = column.removeFromTop(synth::cardbody::kKnobHeight);
    }
    return out;
}

void DrumPadControls::layoutColumns(juce::Rectangle<int> area) {
    const int columns = startOn_ || pitchOn_ ? 4 : 3;
    const int colW = area.getWidth() / (pitchOn_ ? 5 : columns);
    auto fourth = juce::Rectangle<int>(area.getX() + 3 * colW, area.getY(), colW, area.getHeight());
    if (pitchOn_) {
        fourth.setRight(area.getRight());
        pitchCaption_.setBounds(fourth.removeFromTop(synth::cardbody::kLabelHeight));
        pitch_.setBounds(fourth.removeFromTop(synth::cardbody::kRowHeight).reduced(2, 0));
    } else if (startOn_) {
        captions_[(size_t)PadParam::Start].setBounds(fourth.removeFromTop(synth::cardbody::kLabelHeight));
        knobs_[(size_t)PadParam::Start].setBounds(fourth.removeFromTop(synth::cardbody::kKnobHeight));
    }
    placeColumns(columnRects(area));
}

// A pick that moves the three always-there columns slides them (160 ms, easeOutCubic); any other layout (the card
// resized, a refresh) lands them at once. A layout that asks for the places a running slide is heading to leaves it
// running. Moving is movement, so Reduce Motion lands it too.
void DrumPadControls::placeColumns(const ColumnRects& finals) {
    const std::array<juce::Component*, 6> parts = {&captions_[0], &knobs_[0],    &captions_[1],
                                                   &knobs_[1],    &captions_[2], &knobs_[2]};
    if (columnDriver_.isRunning() && finals == columnTo_)
        return;
    columnDriver_.stop(updater_);
    ColumnRects now;
    for (size_t i = 0; i < parts.size(); ++i)
        now[i] = parts[i]->getBounds();
    if (armColumns_ && now != finals && !now[1].isEmpty() && FadeVisibility::canAnimateIn(this) &&
        !prefersReducedMotion()) {
        columnFrom_ = now;
        columnTo_ = finals;
        columnDriver_.start(
            updater_, kGlideMs, easeOutCubic,
            [this, parts](float t) {
                for (size_t i = 0; i < parts.size(); ++i)
                    parts[i]->setBounds(between(columnFrom_[i], columnTo_[i], t));
            },
            [this, parts] {
                for (size_t i = 0; i < parts.size(); ++i)
                    parts[i]->setBounds(columnTo_[i]);
            });
        return;
    }
    for (size_t i = 0; i < parts.size(); ++i)
        parts[i]->setBounds(finals[i]);
}

// ---- The mod ring ----

// The picked pad's cable ring fades in while the ring of the pad left behind fades out on its knob; the card asks
// arrivingRingAlpha() and leavingRing() when it paints. Off screen and under Animations Off both land at once.
void DrumPadControls::startRingFade(int oldPad) {
    ringDriver_.stop(updater_);
    for (size_t f = 0; f < kKnobCount; ++f) {
        auto* param = oldPad >= 0 ? kit_.padParameter(oldPad, (PadParam)f) : nullptr;
        leavingIds_[f] = param != nullptr ? param->paramID : juce::String();
    }
    if (!FadeVisibility::canAnimateIn(this)) {
        ringProgress_ = 1.0f;
        leavingIds_.fill({});
        return;
    }
    ringProgress_ = 0.0f;
    ringDriver_.start(
        updater_, motionMs(kGlideMs, kReducedMs), easeOutCubic,
        [this](float t) {
            ringProgress_ = t;
            repaintCardAroundKnobs();
        },
        [this] {
            ringProgress_ = 1.0f;
            leavingIds_.fill({});
            repaintCardAroundKnobs();
        });
}

std::optional<DrumPadControls::LeavingRing> DrumPadControls::leavingRing(const juce::String& paramId) const {
    if (ringProgress_ >= 1.0f)
        return std::nullopt;
    for (size_t f = 0; f < kKnobCount; ++f) {
        if (leavingIds_[f].isEmpty() || leavingIds_[f] != paramId)
            continue;
        const auto* now = bound_ >= 0 ? kit_.padParameter(bound_, (PadParam)f) : nullptr;
        if (now != nullptr && now->paramID == paramId)
            return std::nullopt;
        return LeavingRing{const_cast<PadKnob*>(&knobs_[f]), 1.0f - ringProgress_};
    }
    return std::nullopt;
}

} // namespace synth::ui
