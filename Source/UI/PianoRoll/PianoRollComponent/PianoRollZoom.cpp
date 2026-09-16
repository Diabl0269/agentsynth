// PianoRollComponent — anchored zoom (the one implementation the wheel, the trackpad pinch and the
// public zoomHorizontal/zoomVertical API all share) and this surface's keyPressed dispatch
// (quantise/snap/scale-filter/scale-panel shortcuts, Escape/Delete, and arrow-key nudge/transpose/
// navigate, resolved through ShortcutManager via matchesAction). The class itself is declared in
// PianoRollComponent.h; sibling PianoRoll<Concern>.cpp units in this directory hold the rest
// (construction/geometry, scale assist, painting, edit tools, audition, clipboard, mouse).

#include "PianoRollComponent.h"

#include "AppUndoManager.h"
#include "PianoRollInternal.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/ScrollPolicy.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

using namespace synth::ui::detail;

//==============================================================================
// ---- Anchored zoom (one implementation; the wheel, the pinch and the public API share it) ----

void PianoRollComponent::zoomHorizontalAroundX(double factor, double anchorGridX) {
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    rollView_.zoomAroundX(factor, anchorGridX);
    repaint();
    if (onHorizontalViewChanged)
        onHorizontalViewChanged();
}

void PianoRollComponent::zoomVerticalAroundY(double factor, double anchorY) {
    if (!std::isfinite(factor) || factor <= 0.0 || !std::isfinite(anchorY) || visiblePitches_.empty())
        return;
    // The anchor is held in fractional ROW-INDEX space — topRowPosition_ itself — for the same
    // reason the wheel-scroll branch walks visiblePitches_ by index: with rows collapsed, "row
    // under the cursor" and "pitch under the cursor" can disagree, and the row is what visually
    // sits at anchorY. Unlike before this change, nothing here rounds the result back to a whole
    // row: the anchored row's y stays pinned across the zoom to floating-point precision instead of
    // "within one row".
    const double rowsAbove = (anchorY - (double)canvasTop()) / pixelsPerSemitone_;
    const double anchorRow = topRowPosition_ - rowsAbove;
    setPixelsPerSemitone(pixelsPerSemitone_ * factor);
    const double newRowsAbove = (anchorY - (double)canvasTop()) / pixelsPerSemitone_;
    setTopRowPosition(anchorRow + newRowsAbove);
    repaint();
}

double PianoRollComponent::wheelZoomFactor(const juce::MouseWheelDetails& wheel) const noexcept {
    // wheelGestureIsUpward already recovers the PHYSICAL direction from isReversed XOR the delta's
    // sign (see ScrollPolicy.h) — "up" here means the same finger motion regardless of the OS's
    // natural-scrolling setting. zoomScrollInverted_ is the app-level preference stacked on top,
    // independent of scrollInverted_ (that one only ever governs the plain-scroll branches, which
    // deliberately keep following the OS convention instead — see mouseWheelMove).
    const bool zoomIn = wheelGestureIsUpward(wheel) != zoomScrollInverted_;
    // Magnitude only — the sign now comes entirely from zoomIn above, not from dominantWheelDelta's
    // own sign, otherwise isReversed (folded into the delta already) would double-count direction.
    const double magnitude = std::abs(dominantWheelDelta(wheel)) * kZoomWheelSensitivity;
    return std::exp(zoomIn ? magnitude : -magnitude);
}

void PianoRollComponent::zoomHorizontal(double factor) {
    // The view centre, expressed in the same grid-relative coordinate the Cmd+wheel branch hands
    // over (x - leftGutterWidth()), so both paths run identical anchor math.
    zoomHorizontalAroundX(factor, (double)gridRegion().getWidth() * 0.5);
}

void PianoRollComponent::zoomVertical(double factor) {
    const auto grid = gridRegion();
    zoomVerticalAroundY(factor, (double)grid.getY() + (double)grid.getHeight() * 0.5);
}

void PianoRollComponent::mouseMagnify(const juce::MouseEvent& e, float scaleFactor) {
    // Trackpad pinch, same pair as the panel's: plain = horizontal zoom around the pinch point
    // (through the roll's OWN mapping), Shift = vertical (pixels-per-semitone) zoom.
    // A pinch carries no wheel deltas at all, so there is no axis to disambiguate here — it goes
    // straight to the same anchored-zoom helpers the Cmd+wheel branches use.
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
        return;
    const auto pos = e.getPosition();
    if (e.mods.isShiftDown()) {
        zoomVerticalAroundY((double)scaleFactor, (double)pos.y);
        return;
    }
    zoomHorizontalAroundX((double)scaleFactor, std::max(0.0, (double)pos.x - (double)leftGutterWidth()));
}

//==============================================================================
bool PianoRollComponent::matchesAction(const juce::KeyPress& key, const juce::String& actionId,
                                       const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    const auto binding = shortcuts_->getBinding(actionId);
    // An invalid binding is "this action has no key": either the user cleared it, or this build's
    // ShortcutManager has never heard of the id (getBinding answers an unknown id with a
    // default-constructed KeyPress). Falling back to `fallback` here would resurrect a key the user
    // deliberately unbound, so it deliberately does not.
    //
    // Routed through ShortcutManager::keyPressMatches rather than raw juce::KeyPress::operator==:
    // macOS delivers a Shift-chorded symbol key as the SHIFTED CHARACTER ('!' not '1', '+' not '='),
    // never the base key plus a Shift modifier flag, so exact equality would silently never match a
    // user rebind onto such a chord — keyPressMatches shift-normalizes exactly that case.
    return binding.isValid() && ShortcutManager::keyPressMatches(binding, key);
}

bool PianoRollComponent::keyPressed(const juce::KeyPress& key) {
    // Q IS QUANTISE HERE, and J is snap. Cubase parity, and the user's call: on a note editor the
    // bare, most reachable key should be the verb you reach for constantly, not an on/off switch you
    // set once a session. Snap is still the SHARED "timelineSnapToggle" action rather than a
    // piano-roll one of its own — two rebindable "Toggle Snap" actions defaulting to the same key and
    // flipping the same TimelineViewState flag would be a Settings list nobody could reason about —
    // so this is one binding for one switch, whichever surface has focus; only the key it sits on
    // moved (Q -> J), and it moved for BOTH surfaces at once, which is the point.
    //
    // Option+Shift+Q (pitch quantise) is matched FIRST because it is the most specific chord of the
    // family, so a user who rebinds only one member cannot end up with a broader one swallowing it —
    // the same ordering rule the Shift+Up/Up pair follows below. macOS delivers Option+letter as a
    // Unicode glyph rather than the letter, which is why it is stored and matched as KEY CODE +
    // modifier set (see matchesAction/ShortcutManager::keyPressMatches) and never as a character.
    if (matchesAction(key, "pianoRollQuantisePitches",
                      modKey('q', juce::ModifierKeys::altModifier | juce::ModifierKeys::shiftModifier))) {
        // Returns the action's OWN applicability, so the key falls through when there is no scale to
        // quantise into rather than being silently swallowed.
        return quantisePitchesToActiveScale();
    }
    if (matchesAction(key, "pianoRollQuantise", plainKey('q'))) {
        flashQuantiseButton();
        performQuantise();
        return true;
    }
    // Alt+Q: the length twin of bare Q above. Exact modifier equality (keyPressMatches) keeps this
    // clear of both bare Q (position quantise) and Alt+Shift+Q (pitch quantise) — no ordering needed
    // between the three. It has its own header chip (QuantiseLength, FRO107) now, so this flashes it
    // exactly like bare Q flashes its own chip above.
    if (matchesAction(key, "pianoRollQuantiseLength", modKey('q', juce::ModifierKeys::altModifier))) {
        flashQuantiseLengthButton();
        performQuantiseLength();
        return true;
    }
    if (matchesAction(key, "timelineSnapToggle", plainKey('j'))) {
        toggleSnap();
        return true;
    }

    // Option+S: "show only scale notes", the keyboard twin of the header's funnel chip. Clear of
    // Ctrl+S (the scale PANEL toggle below) on every platform — different modifier, and modifier
    // equality is exact — including Windows/Linux, where JUCE's Cmd IS Ctrl but Alt is still Alt.
    if (matchesAction(key, "pianoRollToggleScaleFilter", modKey('s', juce::ModifierKeys::altModifier))) {
        if (!clipId_.isValid())
            return false; // nothing open to filter: let the key mean whatever it means elsewhere
        toggleScaleFilter();
        return true;
    }

    // Ctrl+S (the ACTUAL Control key — modKey(..., ctrlModifier), never Cmd, which is the app's
    // Save Preset shortcut) toggles the scale-assist panel, the keyboard equivalent of the header's
    // "Scale" button. Guarded against a focused child TEXT EDITOR (the custom-scale name field is
    // the only one this surface owns) because juce::TextEditor does not treat a Ctrl-chorded letter
    // as text input, so an unhandled Ctrl+S bubbles straight up to us mid-typing — toggling the
    // panel out from under someone naming a scale would be a surprise none of this file's other
    // bindings risk, since none of them share a key with anything a text field would otherwise
    // consume.
    if (matchesAction(key, "pianoRollToggleScalePanel", modKey('s', juce::ModifierKeys::ctrlModifier))) {
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (dynamic_cast<juce::TextEditor*>(focused) != nullptr)
            return false;
        toggleScalePanel();
        return true;
    }

    // Escape and Delete/Backspace are FIXED, never manager-resolved: "cancel" and "delete the
    // selection" are platform conventions every surface in the app answers identically, not app
    // shortcuts a user would expect to find in a rebinding list.
    if (key == juce::KeyPress::escapeKey) {
        if (!selection_.isEmpty()) {
            selection_.clear();
            repaint();
            return true;
        }
        requestClose();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        auto ids = selection_.getSelected();
        if (ids.empty() || doc_ == nullptr)
            return false;

        auto mutate = [this, ids] {
            for (auto id : ids)
                doc_->removeNote(id);
        };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();

        selection_.clear();
        repaint();
        return true;
    }

    // The rest of the surface's keys act on the SELECTION and nothing else: with nothing selected
    // they fall through (return false), so the panel — and the graph behind it — keep whatever those
    // keys mean there. Nudge/transpose EDIT the selected notes; the two navigation actions only MOVE
    // the selection between notes.
    //
    // Each one is resolved through matchesAction, so the defaults listed below are exactly that:
    // defaults. The order matters only where one default is a modified form of another (Shift+Up vs
    // Up, Alt+Left vs Left) — the more specific action is tested first so a user who rebinds only
    // one of a pair cannot end up with the other swallowing it. juce::KeyPress equality is exact on
    // modifiers, which is what keeps Left, Shift+Left and Alt+Left three separate actions.
    //
    // Alt+Up/Down stays RESERVED: no action claims it, so it falls through for whatever the vertical
    // half of navigation turns out to be. Digit keys are deliberately absent too — tool switching
    // belongs to the panel (see setActiveTool).
    if (matchesAction(key, "pianoRollNavNextNote", modKey(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier)))
        return selectAdjacentNote(true);
    if (matchesAction(key, "pianoRollNavPrevNote", modKey(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier)))
        return selectAdjacentNote(false);

    if (matchesAction(key, "pianoRollNudgeRight", plainKey(juce::KeyPress::rightKey)))
        return nudgeSelectedNotes(1);
    if (matchesAction(key, "pianoRollNudgeLeft", plainKey(juce::KeyPress::leftKey)))
        return nudgeSelectedNotes(-1);

    // Shift is the octave jump, the same 12-semitone convention every DAW uses — a separate action
    // rather than a modifier read off the plain one, so it can be rebound on its own.
    if (matchesAction(key, "pianoRollTransposeOctaveUp",
                      modKey(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier)))
        return transposeSelectedNotes(12);
    if (matchesAction(key, "pianoRollTransposeOctaveDown",
                      modKey(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier)))
        return transposeSelectedNotes(-12);
    // Up/Down step by one VISIBLE ROW, not one semitone — which is a scale DEGREE while "show only
    // scale notes" is on and a plain semitone while it is off (visiblePitches_ is all 128 then, so a
    // row step IS a semitone step). One implementation, so the two cannot drift; the octave pair above
    // deliberately stays in semitones. See transposeSelectedNotesByRow.
    if (matchesAction(key, "pianoRollTransposeUp", plainKey(juce::KeyPress::upKey)))
        return transposeSelectedNotesByRow(1);
    if (matchesAction(key, "pianoRollTransposeDown", plainKey(juce::KeyPress::downKey)))
        return transposeSelectedNotesByRow(-1);

    return false;
}

//==============================================================================
// ---- Simple accessors (moved out of the header — see PianoRollComponent.h for each contract) ----
void PianoRollComponent::setScrollInverted(bool inverted) noexcept { scrollInverted_ = inverted; }
bool PianoRollComponent::isScrollInverted() const noexcept { return scrollInverted_; }
void PianoRollComponent::setZoomScrollInverted(bool inverted) noexcept { zoomScrollInverted_ = inverted; }
bool PianoRollComponent::isZoomScrollInverted() const noexcept { return zoomScrollInverted_; }

void PianoRollComponent::setShortcutManager(const ShortcutManager* manager) noexcept { shortcuts_ = manager; }
const ShortcutManager* PianoRollComponent::getShortcutManager() const noexcept { return shortcuts_; }

} // namespace synth::ui
