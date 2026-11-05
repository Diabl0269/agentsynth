// NoteRangePickerGestures.cpp -- what the pointer and the keys do to a NoteRangePicker: edges add or remove notes, the
// middle moves the range note by note (Shift: octaves), a click on empty keys moves it there, the overview bar and the
// ends of the strip scroll, a click on a note name types one. The maths of each is NoteRangeGeometry.h.
// docs/layout/note-range-picker.md.
#include "NoteRangeGeometry.h"
#include "NoteRangePicker.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

using namespace noterange;

namespace {

constexpr float kClickSlopPx = 3.0f;
constexpr int kDragRepeatMs = 25;
constexpr float kWheelKeysPerUnit = 16.0f;
constexpr double kLongestDragStep = 0.1; // seconds: a stalled app does not throw the strip across the keyboard
constexpr float kEditorWidth = 40.0f;
constexpr float kEditorHeight = 14.0f;

} // namespace

// ---- Mouse ------------------------------------------------------------------------------------------------

void NoteRangePicker::beginDrag(const juce::MouseEvent& e) {
    const auto p = e.position;
    drag_ = {};
    drag_.startX = p.x;
    drag_.startLow = drag_.anchorLow = low_;
    drag_.startHigh = drag_.anchorHigh = high_;
    drag_.lastTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (overviewHeight() > 0 && p.y < (float)stripTop()) {
        drag_.kind = DragKind::Overview;
        drag_.moved = true;
        setScrollNow(overviewScrollFor(p.x, (float)getWidth(), keys_));
        return;
    }
    const auto part = partAt(p.x);
    drag_.label = labelAt(p);
    const auto* scale = scale_.has_value() ? &*scale_ : nullptr;
    if (part.has_value() && *part != Part::Band) {
        drag_.kind = DragKind::Edge;
        drag_.part = *part;
    } else {
        drag_.kind = DragKind::Band;
        drag_.part = Part::Band;
        drag_.grabPitch = pitchAtX(p.x, shownScroll_, keyW_);
        if (!part.has_value()) {
            // A click on empty keys moves the range there, the same width; the drag goes on from there.
            const auto moved = bandCentredOn(drag_.grabPitch, low_, high_, scale);
            drag_.anchorLow = moved.lo;
            drag_.anchorHigh = moved.hi;
            commit(moved.lo, moved.hi, true, scroll_);
        }
    }
    getStop(drag_.part).grabKeyboardFocus();
}

void NoteRangePicker::mouseDown(const juce::MouseEvent& e) {
    if (noteEditor_->isVisible())
        finishNoteEdit(false);
    beginDrag(e);
    // Keeps mouseDrag coming while the pointer rests past the end of the strip, which is what scrolls it.
    if (drag_.kind != DragKind::None && isShowing())
        juce::Component::beginDragAutoRepeat(kDragRepeatMs);
}

void NoteRangePicker::mouseDrag(const juce::MouseEvent& e) {
    if (drag_.kind == DragKind::None)
        return;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = std::clamp(now - drag_.lastTime, 0.0, kLongestDragStep);
    drag_.lastTime = now;
    advanceDrag(e.position.x, e.mods, dt);
}

void NoteRangePicker::dragForTest(float x, double dtSeconds, juce::ModifierKeys mods) {
    advanceDrag(x, mods, dtSeconds);
}

// One step of a drag. A pointer at an end of the strip (or past it, even outside the component) scrolls the strip
// `dt` seconds' worth, so the key under the pointer, and with it the edge or the band, keeps moving.
void NoteRangePicker::advanceDrag(float x, juce::ModifierKeys mods, double dt) {
    if (drag_.kind == DragKind::None)
        return;
    if (drag_.kind == DragKind::Overview) {
        setScrollNow(overviewScrollFor(x, (float)getWidth(), keys_));
        return;
    }
    if (!drag_.moved && std::abs(x - drag_.startX) >= kClickSlopPx)
        drag_.moved = true;
    if (!drag_.moved)
        return;
    const float speed = autoScrollSpeed(x, (float)getWidth(), keyW_);
    if (speed != 0.0f)
        scrollBy(speed * (float)dt);
    const float inside = std::clamp(x, 0.0f, (float)getWidth());
    if (drag_.kind == DragKind::Edge)
        dragEdge(drag_.part, inside);
    else
        dragBand(inside, mods);
}

void NoteRangePicker::mouseUp(const juce::MouseEvent&) {
    if (drag_.kind == DragKind::None)
        return;
    const Drag done = drag_;
    drag_ = {};
    juce::Component::beginDragAutoRepeat(0);
    if (done.kind == DragKind::Overview)
        return;
    const bool changed = low_ != done.startLow || high_ != done.startHigh;
    if (changed && onGestureEnd)
        onGestureEnd(low_, high_);
    if (!changed && !done.moved && done.label.has_value())
        beginNoteEdit(*done.label);
}

void NoteRangePicker::mouseMove(const juce::MouseEvent& e) {
    auto cursor = juce::MouseCursor::PointingHandCursor;
    if (overviewHeight() > 0 && e.position.y < (float)stripTop())
        cursor = juce::MouseCursor::DraggingHandCursor;
    else if (labelAt(e.position).has_value())
        cursor = juce::MouseCursor::IBeamCursor;
    else if (const auto part = partAt(e.position.x))
        cursor = *part == Part::Band ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor;
    setMouseCursor(cursor);
}

// A sideways swipe scrolls the strip; a plain wheel is left to whatever scrolls the panel around it.
void NoteRangePicker::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (std::abs(wheel.deltaX) <= std::abs(wheel.deltaY)) {
        juce::Component::mouseWheelMove(e, wheel);
        return;
    }
    scrollBy(-wheel.deltaX * kWheelKeysPerUnit);
}

// The edge goes to the key under the pointer, then to the nearest scale note; it never passes the other edge.
void NoteRangePicker::dragEdge(Part part, float x) {
    const bool low = part == Part::LowEdge;
    const int pitch = edgePitchAt(low, x, shownScroll_, keyW_, scale_.has_value() ? &*scale_ : nullptr);
    if (low)
        commit(std::min(pitch, high_), high_, true, scroll_);
    else
        commit(low_, std::max(pitch, low_), true, scroll_);
}

// The range follows the note under the pointer: note by note (scale note by scale note with a scale), with Shift by
// octaves.
void NoteRangePicker::dragBand(float x, juce::ModifierKeys mods) {
    const int pointer = pitchAtX(x, shownScroll_, keyW_);
    const auto moved = dragBandRange(drag_.anchorLow, drag_.anchorHigh, drag_.grabPitch, pointer, mods.isShiftDown(),
                                     scale_.has_value() ? &*scale_ : nullptr);
    commit(moved.lo, moved.hi, true, scroll_);
}

// ---- Keyboard ---------------------------------------------------------------------------------------------

// One finished keyboard (or typed) change: the range, the strip brought to the part that moved, one undo step.
void NoteRangePicker::finishUserChange(int lo, int hi, Part part) {
    const int lowBefore = low_;
    const int highBefore = high_;
    commit(lo, hi, true, revealTarget(part, lo, hi));
    if ((low_ != lowBefore || high_ != highBefore) && onGestureEnd)
        onGestureEnd(low_, high_);
}

// Left and Right: the focused edge moves one scale note (Shift: an octave); the range moves one scale note (Shift: an
// octave). Page Up and Page Down show the octave above or below. Enter on an edge types a note. Returns whether the
// key was one of those.
bool NoteRangePicker::keyPressedOn(Part part, const juce::KeyPress& key) {
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey) {
        scrollTo(scroll_ + (code == juce::KeyPress::pageUpKey ? 7.0f : -7.0f));
        return true;
    }
    if (code == juce::KeyPress::returnKey && part != Part::Band) {
        beginNoteEdit(part);
        return true;
    }
    if (code != juce::KeyPress::leftKey && code != juce::KeyPress::rightKey)
        return false;
    const int direction = code == juce::KeyPress::rightKey ? 1 : -1;
    const bool octave = key.getModifiers().isShiftDown();
    const auto snapped = [this](int pitch) {
        pitch = std::clamp(pitch, 0, 127);
        return scale_.has_value() ? scale_->snapPitch(pitch) : pitch;
    };
    if (part == Part::LowEdge) {
        const int pitch = octave ? snapped(low_ + direction * 12) : stepNote(low_, direction);
        finishUserChange(std::clamp(pitch, 0, high_), high_, part);
    } else if (part == Part::HighEdge) {
        const int pitch = octave ? snapped(high_ + direction * 12) : stepNote(high_, direction);
        finishUserChange(low_, std::clamp(pitch, low_, 127), part);
    } else if (!octave) {
        const int nextLo = stepNote(low_, direction);
        const int nextHi = stepNote(high_, direction);
        if (nextLo != low_ && nextHi != high_)
            finishUserChange(nextLo, nextHi, part);
    } else {
        const int shift = std::clamp(direction * 12, -low_, 127 - high_);
        finishUserChange(low_ + shift, high_ + shift, part);
    }
    return true;
}

// ---- Typing a note ----------------------------------------------------------------------------------------

bool NoteRangePicker::isEditingNote() const noexcept { return noteEditor_->isVisible(); }

// Opens the note editor over an edge's name. The strip first brings that edge into view.
void NoteRangePicker::beginNoteEdit(Part edge) {
    if (edge == Part::Band)
        return;
    editing_ = edge;
    setScrollNow(revealTarget(edge, low_, high_));
    const auto& c = theme::themeOf(*this).colors;
    noteEditor_->setColour(juce::TextEditor::backgroundColourId, c.bg0);
    noteEditor_->setColour(juce::TextEditor::textColourId, c.textPrimary);
    noteEditor_->setColour(juce::TextEditor::highlightColourId, c.accent.withAlpha(0.4f));
    noteEditor_->setColour(juce::TextEditor::highlightedTextColourId, c.textPrimary);
    noteEditor_->setColour(juce::TextEditor::outlineColourId, c.accent);
    noteEditor_->setColour(juce::TextEditor::focusedOutlineColourId, c.accent);
    noteEditor_->setFont(theme::AppLookAndFeel::uiFont(9.0f));
    const juce::String title = edge == Part::LowEdge ? "Type the lowest note" : "Type the highest note";
    noteEditor_->setTitle(title);
    noteEditor_->setTooltip("Type a note such as C#3 and press Enter");
    const auto area = labelArea(edge);
    const float w = (float)getWidth();
    float x = edge == Part::LowEdge ? area.getX() - 2.0f : area.getRight() + 2.0f - kEditorWidth;
    x = std::clamp(x, 0.0f, std::max(0.0f, w - kEditorWidth));
    noteEditor_->setBounds(juce::Rectangle<float>(x, area.getY() - 2.0f, kEditorWidth, kEditorHeight).toNearestInt());
    noteEditor_->setText(nameOf(edge == Part::LowEdge ? low_ : high_), false);
    noteEditor_->setVisible(true);
    noteEditor_->grabKeyboardFocus();
    noteEditor_->selectAll();
}

// Enter applies a typed note: the edge goes exactly there (no scale snap), and the other edge follows when the new
// one would pass it. A name that does not parse stays in the editor, in the error colour. Escape and leaving close it.
void NoteRangePicker::finishNoteEdit(bool accept) {
    if (!noteEditor_->isVisible())
        return;
    const Part edge = editing_;
    if (accept) {
        const auto pitch = parseNoteName(noteEditor_->getText().toStdString(), middleCOctave_);
        if (!pitch.has_value()) {
            noteEditor_->applyColourToAllText(theme::themeOf(*this).colors.error);
            return;
        }
        noteEditor_->setVisible(false);
        if (edge == Part::LowEdge)
            finishUserChange(*pitch, std::max(high_, *pitch), edge);
        else
            finishUserChange(std::min(low_, *pitch), *pitch, edge);
        getStop(edge).grabKeyboardFocus();
    } else {
        noteEditor_->setVisible(false);
    }
}

} // namespace synth::ui
