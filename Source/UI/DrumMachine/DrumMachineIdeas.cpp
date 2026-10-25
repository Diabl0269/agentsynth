// DrumMachineComponent: the Ideas button and what each idea does to the clip. DrumIdeas.cpp makes the notes and
// DrumIdeasPanel.cpp is the popover; this unit applies a result as ONE undo step, grows the new hits in from left to
// right, and outlines the cells that changed.

#include "DrumMachineComponent.h"

#include "AppUndoManager.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace test_hooks {
std::function<void(std::unique_ptr<DrumIdeasPanel>)>& drumIdeasPanelHookForTest() {
    static std::function<void(std::unique_ptr<DrumIdeasPanel>)> hook;
    return hook;
}
} // namespace test_hooks

namespace {
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
} // namespace

void DrumMachineComponent::setupIdeas() {
    addAndMakeVisible(ideasButton_);
    ideasButton_.setComponentID("drumIdeas");
    ideasButton_.setTitle("Ideas");
    ideasButton_.setTooltip("Starters, variations and randomize");
    ideasButton_.onClick = [this] { openIdeasPanel(); };
}

drumideas::Grid DrumMachineComponent::ideaGrid() const {
    const auto* c = clip();
    return {c != nullptr ? c->lengthBeats : 0.0, beatsPerBar(), getStepBeats()};
}

DrumIdeasContext DrumMachineComponent::ideasContext() const {
    DrumIdeasContext ctx;
    ctx.grid = ideaGrid();
    if (const auto* c = clip())
        ctx.notes = c->notes;
    const double bpm = transport_ != nullptr ? transport_->getPositionSnapshot().bpm : 0.0;
    ctx.secondsPerBeat = bpm > 0.0 ? 60.0 / bpm : 0.5;
    return ctx;
}

void DrumMachineComponent::openIdeasPanel() {
    if (!clipId_.isValid())
        return;
    juce::Component::SafePointer<DrumMachineComponent> safe(this);
    auto panel = std::make_unique<DrumIdeasPanel>(
        [safe] { return safe != nullptr ? safe->ideasContext() : DrumIdeasContext{}; });
    panel->onStarter = [safe](drumideas::Style style) {
        if (safe != nullptr)
            safe->fillWithStarter(style);
    };
    panel->onRandomize = [safe](double density, bool keep, int seed) {
        if (safe != nullptr)
            safe->randomizePattern(density, keep, seed);
    };
    panel->onPick = [safe](const std::vector<synth::MidiNote>& notes, bool replace, int) {
        if (safe != nullptr)
            safe->pickTake(notes, replace);
    };
    panel->onAudition = [safe](int pitch, int velocity) {
        if (safe != nullptr)
            safe->audition(pitch, velocity);
    };
    panel->onClosed = [safe] {
        if (safe != nullptr)
            safe->focusIdeasButton();
    };
    if (auto& hook = test_hooks::drumIdeasPanelHookForTest()) {
        hook(std::move(panel));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(panel), ideasButton_.getScreenBounds(), nullptr);
}

void DrumMachineComponent::focusIdeasButton() {
    ++ideasFocusReturns_;
    ideasButton_.grabKeyboardFocus();
}

void DrumMachineComponent::announce(const juce::String& text) {
    lastAnnouncement_ = text;
    juce::AccessibilityHandler::postAnnouncement(text, juce::AccessibilityHandler::AnnouncementPriority::medium);
}

//==============================================================================
// Replaces the clip's notes with `notes` (and lifts the grid to `requiredSteps` per bar when it is finer than the
// clip's) in one undo step.
void DrumMachineComponent::applyIdeaNotes(std::vector<synth::MidiNote> notes, int requiredSteps,
                                          const juce::String& announcement) {
    const auto* c = clip();
    if (c == nullptr)
        return;
    const auto before = c->notes;
    const auto id = clipId_;
    const int steps = std::max(c->stepsPerBar, requiredSteps);
    record([this, id, steps, notes = std::move(notes)] {
        const auto* target = doc_->getClip(id);
        if (target != nullptr && target->stepsPerBar != steps)
            doc_->setClipStepsPerBar(id, steps);
        doc_->clearNotes(id);
        for (const auto& note : notes)
            doc_->addNote(id, note);
    });
    staggerNext_ = true;
    refreshFromDoc();
    staggerNext_ = false;
    if (const auto* after = clip())
        markChanged(drumideas::changedCells(before, after->notes, getStepBeats()));
    announce(announcement);
}

void DrumMachineComponent::fillWithStarter(drumideas::Style style) {
    const auto* c = clip();
    if (c == nullptr)
        return;
    const int steps = std::max(c->stepsPerBar, drumideas::requiredStepsPerBar(style));
    drumideas::Grid grid = ideaGrid();
    grid.stepBeats = drumgrid::stepBeats(steps, beatsPerBar());
    applyIdeaNotes(drumideas::starter(style, grid), steps, "Filled with " + drumideas::styleName(style) + " starter");
}

void DrumMachineComponent::randomizePattern(double density, bool keepKickAndSnare, int seed) {
    const auto* c = clip();
    if (c == nullptr)
        return;
    auto result = drumideas::randomize(c->notes, ideaGrid(), density, keepKickAndSnare, seed);
    applyIdeaNotes(std::move(result.notes), 0,
                   keepKickAndSnare ? "Randomized the other drums" : "Randomized the pattern");
}

// Replace puts the take into this clip; otherwise it becomes a new clip right after this one, with the same grid,
// swing and humanise, and the grid opens it. Either way one undo step.
synth::ClipId DrumMachineComponent::pickTake(const std::vector<synth::MidiNote>& notes, bool replace) {
    const auto* c = clip();
    if (c == nullptr)
        return {};
    if (replace) {
        applyIdeaNotes(notes, 0, "Replaced this clip with the take");
        return clipId_;
    }
    const auto* track = doc_->getTrackForClip(clipId_);
    if (track == nullptr)
        return {};
    const auto trackId = track->id;
    const double start = c->startBeat + c->lengthBeats;
    const double length = c->lengthBeats;
    const int steps = c->stepsPerBar;
    const double swing = c->swing;
    const double humanise = c->humanise;
    const auto sourceName = c->name;
    const auto name = sourceName + " take";
    synth::ClipId created;
    record([&] {
        created = doc_->addClip(trackId, start, length, name);
        if (!created.isValid())
            return;
        doc_->setClipStepsPerBar(created, steps);
        doc_->setClipSwing(created, swing);
        doc_->setClipHumanise(created, humanise);
        for (const auto& note : notes)
            doc_->addNote(created, note);
    });
    if (!created.isValid())
        return {};
    openClip(created);
    if (onClipCreated)
        onClipCreated(created);
    announce("Take added as a new clip after " + sourceName);
    return created;
}

//==============================================================================
// The cells an idea changed keep an orange outline that fades over 1.2 s. It is information, not decoration, so it
// follows Reduce Motion with the same fade; Animations Off, or a grid not on screen, lands without it.
void DrumMachineComponent::markChanged(const std::vector<drumideas::ChangedCell>& cells) {
    if (cells.empty() || !motionAllowed() || animationsOff())
        return;
    const double now = nowMs();
    for (const auto& [pitch, step] : cells)
        changed_[drumgrid::cellKey(pitch, step)] = now;
    changeAnim_.start(
        vblank_, kChangedOutlineMs, [](float t) { return t; },
        [this](float) {
            const double elapsed = nowMs();
            for (auto it = changed_.begin(); it != changed_.end();) {
                repaint(rectForKey(it->first));
                it = elapsed - it->second >= kChangedOutlineMs ? changed_.erase(it) : std::next(it);
            }
        },
        [this] {
            changed_.clear();
            repaint();
        });
    repaint();
}

float DrumMachineComponent::changedAmount(drumgrid::CellKey key) const {
    const auto it = changed_.find(key);
    if (it == changed_.end())
        return 0.0f;
    return 1.0f - juce::jlimit(0.0f, 1.0f, static_cast<float>((nowMs() - it->second) / kChangedOutlineMs));
}

bool DrumMachineComponent::isChangedOutlineActive(int row, int step) const {
    return slotIndexForRow(row) >= 0 && changed_.count(drumgrid::cellKey(slots_[(size_t)row].lane.pitch, step)) > 0;
}

} // namespace synth::ui
