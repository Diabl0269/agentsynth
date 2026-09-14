// PianoRollComponent — note audition: the keys-column virtual keyboard down the left gutter, note
// audition on click/drag (see onAuditionNote's contract), clip-overrun handling after a resize, and
// this surface's tooltips. The class itself is declared in PianoRollComponent.h; sibling
// PianoRoll<Concern>.cpp units in this directory hold the rest (construction/geometry, scale
// assist, painting, edit tools, clipboard, mouse, zoom).

#include "PianoRollComponent.h"

#include "AppUndoManager.h"
#include "PianoRollInternal.h"
#include "ShortcutManager.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

using namespace synth::ui::detail;

//==============================================================================
// ---- Keys-column audition (the virtual keyboard down the left gutter) ----

juce::Rectangle<int> PianoRollComponent::keyRowRect(int pitch) const noexcept {
    if (pitch < 0 || pitch > 127 || keysColumnBounds_.isEmpty())
        return {};
    const int rowHeight = std::max(1, (int)pixelsPerSemitone_);
    return {keysColumnBounds_.getX(), yForPitch(pitch), keysColumnBounds_.getWidth(), rowHeight};
}

bool PianoRollComponent::isKeysColumnPoint(juce::Point<int> pos) const noexcept {
    // keysColumnBounds_ already excludes the scale panel (it is carved AFTER it in resized()) and
    // starts at canvasTop(), so it excludes the toolbar row and the ruler band too — one rect test
    // covers every "not actually a key" case.
    return !keysColumnBounds_.isEmpty() && keysColumnBounds_.contains(pos);
}

void PianoRollComponent::beginKeysColumnPress(juce::Point<int> pos) {
    if (!isKeysColumnPoint(pos))
        return;
    const int pitch = juce::jlimit(0, 127, pitchForY(pos.y));
    keysColumnPressing_ = true;
    const int previous = keysColumnPitch_;
    keysColumnPitch_ = pitch;
    // Straight through the SAME audition seam a note click uses — the owner cannot tell the two
    // apart, and does not need to: both are "the user asked to hear this pitch on this track".
    startAudition(pitch, kKeysColumnVelocity);
    if (previous != pitch && previous >= 0)
        repaint(keyRowRect(previous));
    repaint(keyRowRect(pitch));
}

void PianoRollComponent::updateKeysColumnPress(juce::Point<int> pos) {
    if (!keysColumnPressing_)
        return;
    // Clamped to the column's own y range rather than abandoned when the pointer strays sideways: a
    // finger sliding down a keyboard routinely drifts off the keys horizontally, and dropping the
    // gesture there would leave the note held with no way to release it.
    const int clampedY = juce::jlimit(keysColumnBounds_.getY(), keysColumnBounds_.getBottom() - 1, pos.y);
    const int pitch = juce::jlimit(0, 127, pitchForY(clampedY));
    if (pitch == keysColumnPitch_)
        return; // the gate: sliding inside one key costs no MIDI and no repaint
    const int previous = keysColumnPitch_;
    keysColumnPitch_ = pitch;
    retriggerAudition(pitch);
    if (previous >= 0)
        repaint(keyRowRect(previous));
    repaint(keyRowRect(pitch));
}

void PianoRollComponent::endKeysColumnPress() {
    if (!keysColumnPressing_)
        return;
    const int previous = keysColumnPitch_;
    keysColumnPressing_ = false;
    keysColumnPitch_ = -1;
    // stopAudition() is NOT called here: mouseUp already calls it unconditionally, and so does every
    // cancel path (openClip/closeRoll/visibilityChanged/the destructor/a tool switch). Releasing it
    // twice would be harmless but this way there is exactly one owner of the note-off.
    if (previous >= 0)
        repaint(keyRowRect(previous));
}

void PianoRollComponent::visibilityChanged() {
    // Going away mid-gesture means no mouseUp is coming (the panel swapped the clip lanes back in,
    // the timeline panel collapsed, the window closed) — the one path a stuck note would otherwise
    // come from. Becoming visible is a no-op: nothing can be sounding yet.
    if (!isVisible()) {
        stopAudition();
        endKeysColumnPress();
    }
}

//==============================================================================
// ---- Note audition (see onAuditionNote) ----

void PianoRollComponent::startAudition(int pitch, int velocity) {
    if (!onAuditionNote)
        return;
    // Release first, unconditionally: a second note-on without a release cannot be right, and
    // leaving the old one sounding would break the one-off-per-on contract for it.
    stopAudition();
    const int clamped = juce::jlimit(0, 127, pitch);
    auditionVelocity_ = juce::jlimit(1, 127, velocity);
    auditionPitch_ = clamped;
    auditionActive_ = true;
    onAuditionNote(clamped, (float)auditionVelocity_ / 127.0f, true);
}

void PianoRollComponent::retriggerAudition(int pitch) {
    if (!auditionActive_)
        return; // nothing sounding: a drag that never started a preview must not start one now
    const int clamped = juce::jlimit(0, 127, pitch);
    if (clamped == auditionPitch_)
        return; // the gate that makes a horizontal drag inside one row cost nothing
    // Carries the velocity the gesture started on (see auditionVelocity_) — a Move drag changes
    // pitch, never velocity.
    startAudition(clamped, auditionVelocity_);
}

void PianoRollComponent::stopAudition() {
    if (!auditionActive_)
        return;
    const int pitch = auditionPitch_;
    const int velocity = auditionVelocity_;
    // Cleared BEFORE the callback: the owner's handler is free to do anything (including something
    // that re-enters this component), and it must never see a note this call has already ended.
    auditionActive_ = false;
    auditionPitch_ = -1;
    if (onAuditionNote)
        onAuditionNote(pitch, (float)velocity / 127.0f, false);
}

//==============================================================================
// ---- Clip overrun after a resize ----

double PianoRollComponent::maxNoteEndAmong(const std::vector<synth::NoteId>& ids) const {
    if (doc_ == nullptr)
        return 0.0;
    double maxEnd = 0.0;
    for (auto id : ids)
        if (const auto* note = doc_->getNote(id))
            maxEnd = std::max(maxEnd, note->startBeat + note->lengthBeats);
    return maxEnd;
}

bool PianoRollComponent::extendClipTo(synth::ClipId clipId, double lengthBeats) {
    // `clipId` is a PARAMETER, never clipId_ — see the declaration. The lookup below is also what
    // makes "the clip was deleted while the alert was open" a silent no-op rather than a crash.
    if (doc_ == nullptr || !clipId.isValid() || !std::isfinite(lengthBeats) || lengthBeats <= 0.0)
        return false;
    const auto* clip = doc_->getClip(clipId);
    if (clip == nullptr || clip->lengthBeats >= lengthBeats - kBeatEpsilon)
        return false; // gone, or already long enough — nothing to do, and no undo step to push

    bool changed = false;
    auto mutate = [this, clipId, lengthBeats, &changed] { changed = doc_->resizeClip(clipId, lengthBeats); };
    // Its OWN undo step, deliberately not merged with the resize that provoked it: the user answered
    // a second question, so undo should take them back one answer at a time. Same
    // recordTimelineChange shape TimelineClipLaneArea's own clip resize uses.
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
    repaint();
    return changed;
}

void PianoRollComponent::applyExtendPromptAnswer(synth::ClipId clipId, double requiredLengthBeats, bool extend) {
    if (!extend)
        return; // "Keep": the notes stay overrunning, and playback simply truncates them at the clip
                // boundary (TimelineSnapshot clamps every event end to the clip's end and drops notes
                // starting past it), so the overrun is inaudible rather than wrong.
    extendClipTo(clipId, requiredLengthBeats);
}

void PianoRollComponent::promptExtendClipToFitNotes(synth::ClipId clipId, double requiredLengthBeats) {
    lastExtendPromptClip_ = clipId;
    lastExtendPromptLength_ = requiredLengthBeats;

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("Notes Past the Clip End")
                       .withMessage("Extend clip to fit the resized notes?")
                       .withButton("Extend")
                       .withButton("Keep");
    // ASYNC, never a modal loop: this runs while the mouse-up is still unwinding, and a nested
    // message loop there would re-enter the very gesture that opened it. SafePointer because the
    // answer can arrive after the roll has closed or the panel has been destroyed — the same pattern
    // AIChatComponent::confirmAndClearHistory uses.
    //
    // `clipId` is captured BY VALUE and is the only thing that decides which clip grows. A modal
    // window blocks user INPUT, not the message thread: an AI action, an undo/redo or a timer can
    // openClip() a different clip while this alert is up, so re-deriving the target at answer time
    // (from clipId_, or from a member) would grow whichever clip happened to be open. It is also why
    // nothing here reads lastExtendPromptClip_ — that member is a test hook, and a second prompt
    // would overwrite it before the first was answered.
    juce::Component::SafePointer<PianoRollComponent> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis, clipId, requiredLengthBeats](int result) {
        if (auto* self = safeThis.getComponent())
            self->applyExtendPromptAnswer(clipId, requiredLengthBeats, result == 1);
    });
}

//==============================================================================
// ---- Tooltips ----

juce::String PianoRollComponent::getTooltipFor(juce::Point<int> pos) const {
    if (snapButtonBounds_.contains(pos))
        return snapTooltipText();
    if (quantiseButtonBounds_.contains(pos))
        return quantiseTooltipText();
    if (quantisePitchButtonBounds_.contains(pos))
        return quantisePitchTooltipText();
    if (scaleButtonBounds_.contains(pos))
        return scaleTooltipText();
    if (scaleFilterButtonBounds_.contains(pos))
        return scaleFilterTooltipText();
    return {};
}

juce::String PianoRollComponent::getTooltip() { return getTooltipFor(getMouseXYRelative()); }

// Rebuilt fresh on every call (see the header comment) — reading shortcuts_ live is what makes a
// rebind visible the very next time either tooltip is queried, with no cache and no listener.
juce::String PianoRollComponent::snapTooltipText() const {
    // "Snap" is the word everywhere in the roll now (Cubase parity): the chip, this tooltip, the docs.
    // Still the SHARED "timelineSnapToggle" action (now J, not Q) — one binding for one switch.
    const auto hint = shortcutHintFor(shortcuts_, "timelineSnapToggle", plainKey('j'));
    juce::String text = "Snap to grid on/off";
    if (hint.isNotEmpty())
        text += " (" + hint + ")";
    text += juce::String::fromUTF8(" \xE2\x80\x94 magnetism only: the chosen grid stays VISIBLE either way");
    return text;
}

juce::String PianoRollComponent::quantiseTooltipText() const {
    const auto hint = shortcutHintFor(shortcuts_, "pianoRollQuantise", plainKey('q'));
    juce::String text = "Quantize note starts to the grid";
    if (hint.isNotEmpty())
        text += " (" + hint + ")";
    text += juce::String::fromUTF8(" \xE2\x80\x94 the selected notes, or all notes when nothing is selected. "
                                   "Works even while snap is off");
    return text;
}

juce::String PianoRollComponent::quantisePitchTooltipText() const {
    const auto hint = shortcutHintFor(shortcuts_, "pianoRollQuantisePitches",
                                      modKey('q', juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier));
    juce::String text = "Quantize note pitches into the scale";
    if (hint.isNotEmpty())
        text += " (" + hint + ")";
    text += juce::String::fromUTF8(" \xE2\x80\x94 the selected notes, or all notes when nothing is selected. "
                                   "Needs a scale picked in Scale Assist");
    return text;
}

juce::String PianoRollComponent::scaleTooltipText() const {
    const auto hint =
        shortcutHintFor(shortcuts_, "pianoRollToggleScalePanel", modKey('s', juce::ModifierKeys::ctrlModifier));
    juce::String text = "Scale assist";
    if (hint.isNotEmpty())
        text += " (" + hint + ")";
    text += juce::String::fromUTF8(" \xE2\x80\x94 pick a scale, or generate random notes");
    return text;
}

juce::String PianoRollComponent::scaleFilterTooltipText() const {
    const auto hint =
        shortcutHintFor(shortcuts_, "pianoRollToggleScaleFilter", modKey('s', juce::ModifierKeys::altModifier));
    juce::String text = "Show only scale notes";
    if (hint.isNotEmpty())
        text += " (" + hint + ")";
    text += juce::String::fromUTF8(" \xE2\x80\x94 collapses the out-of-scale rows, and makes Up/Down step by scale "
                                   "degree. Needs a scale picked in Scale Assist");
    return text;
}

//==============================================================================
// ---- Simple accessors (moved out of the header — see PianoRollComponent.h for each contract) ----
int PianoRollComponent::getAuditionPitchForTest() const noexcept { return auditionActive_ ? auditionPitch_ : -1; }
bool PianoRollComponent::isAuditionActiveForTest() const noexcept { return auditionActive_; }
int PianoRollComponent::getPressedKeyForTest() const noexcept { return keysColumnPressing_ ? keysColumnPitch_ : -1; }

double PianoRollComponent::getLastExtendPromptLengthForTest() const noexcept { return lastExtendPromptLength_; }
synth::ClipId PianoRollComponent::getLastExtendPromptClipForTest() const noexcept { return lastExtendPromptClip_; }

} // namespace synth::ui
