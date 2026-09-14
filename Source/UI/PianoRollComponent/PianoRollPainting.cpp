// PianoRollComponent — painting: the grid/keyboard/note rendering (paint()), the header row's chip
// glyphs, and the local playhead line (TimelinePlayheadOverlay::LocalPlayheadClient — see the
// header's class comment for why the roll draws its own playhead instead of the panel-wide
// overlay). The class itself is declared in PianoRollComponent.h; sibling PianoRoll<Concern>.cpp
// units in this directory hold the rest (construction/geometry, scale assist, edit tools, audition,
// clipboard, mouse, zoom).

#include "PianoRollComponent.h"

#include "../Theme/AppLookAndFeel.h"
#include "../TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "PianoRollInternal.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

using namespace synth::ui::detail;

//==============================================================================
// ---- Painting ----

void PianoRollComponent::paint(juce::Graphics& g) {
    using namespace synth::theme;
    juce::Colour bg = juce::Colours::black;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel()))
        bg = lf->getTheme().colors.bg0;
    g.fillAll(bg);

    if (doc_ == nullptr || !clipId_.isValid())
        return;

    paintGrid(g);
    // Over the notes (both are about a note that is there or about to be), still under the keys
    // column and the header so everything is clipped by the same gutter.
    paintDrawPreview(g);
    paintSplitPreview(g);
    // Before the keys column and the header, so both clip the line the same way they clip a note
    // that has scrolled off to the left.
    paintPlayhead(g);
    paintKeysColumn(g);
    paintHeader(g);

    if (dragMode_ == DragMode::Marquee)
        paintMarquee(g);
}

PianoRollComponent::LineRange PianoRollComponent::visibleLineRange(double spacingBeats) const noexcept {
    LineRange range;
    // The shared density guard: too dense to read means this level is dropped entirely, never
    // drawn as a wall of touching pixels (see gridLevelIsReadable).
    if (!gridLevelIsReadable(spacingBeats, rollView_.pixelsPerBeat))
        return range;

    const auto grid = gridRegion();
    if (grid.isEmpty())
        return range;

    constexpr double kEps = 1.0e-9;
    const double leftBeat = xToBeat((double)grid.getX());
    const double rightBeat = xToBeat((double)grid.getRight());
    range.first = (long long)std::ceil(leftBeat / spacingBeats - kEps);
    range.last = (long long)std::floor(rightBeat / spacingBeats + kEps);
    return range;
}

int PianoRollComponent::getGridLineCountForTest(double spacingBeats) const noexcept {
    return visibleLineRange(spacingBeats).count();
}

void PianoRollComponent::paintGridLines(juce::Graphics& g, juce::Colour lineColour, juce::Colour background) {
    const auto grid = gridRegion();
    const float top = (float)grid.getY();
    const float bottom = (float)grid.getBottom();

    // Faintest level first so a bar line always wins a pixel it shares with a beat or sub-beat line.
    // Every level's colour comes from the ONE shared policy (TimelineClipLaneArea.h) rather than a
    // local alpha, so bar >= beat >= subdivision holds here and on the lanes by construction, and
    // the sub-beat level is a readable hint rather than the near-invisible hairline it used to be on
    // dark themes.
    const auto drawLevel = [&](double spacingBeats, GridLineLevel level) {
        const auto range = visibleLineRange(spacingBeats);
        if (range.count() == 0)
            return;
        g.setColour(gridLineColourFor(level, lineColour, background));
        for (long long i = range.first; i <= range.last; ++i) {
            const int x = (int)std::llround(beatToX((double)i * spacingBeats));
            if (x < grid.getX() || x > grid.getRight())
                continue;
            g.drawVerticalLine(x, top, bottom);
        }
    };

    // BEAT lines are drawn unconditionally (subject only to the density guard), including when the
    // snap division is COARSER than a beat — Snap::Bar/Whole/Half must not cost the user the beat
    // grid they read tempo from. The subdivision level is the chosen division and only exists when it
    // is finer than a beat; visibleLineRange drops it when its lines would land under ~3 px apart.
    //
    // drawnGridBeats(), NOT currentGridBeats(): the RAW division, so switching snap OFF no longer
    // erases the sub-beat lines. It used to, and that was backwards — free-hand editing is exactly
    // when the user needs to SEE the grid they are placing notes against. Snap decides whether edits
    // are magnetic; it has no business deciding what the canvas shows. Only Snap::Off (no division
    // chosen at all) has no sub-beat level, which is what the > 0.0 test still covers.
    const double division = drawnGridBeats();
    if (division > 0.0 && division < 1.0)
        drawLevel(division, GridLineLevel::Subdivision);
    drawLevel(1.0, GridLineLevel::Beat);
    drawLevel(currentBeatsPerBar(), GridLineLevel::Bar);
}

void PianoRollComponent::paintGrid(juce::Graphics& g) {
    using namespace synth::theme;
    juce::Colour whiteRow, blackRow, rowSep, dimColour;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        whiteRow = c.bg1;
        blackRow = c.surfaceHi;
        rowSep = c.border;
        dimColour = c.bg0;
    } else {
        whiteRow = juce::Colours::darkgrey.darker(0.3f);
        blackRow = juce::Colours::darkgrey.darker(0.6f);
        rowSep = juce::Colours::grey;
        dimColour = juce::Colours::black;
    }

    const int width = getWidth();
    const int height = getHeight();
    const int rowHeight = std::max(1, (int)pixelsPerSemitone_);
    const int visibleRows = std::max(0, (height - canvasTop()) / rowHeight) + 2;
    const long long totalRows = (long long)visiblePitches_.size();
    if (totalRows > 0) {
        const long long firstRow = (long long)nearestVisibleRowIndex(firstVisiblePitch_);
        for (int i = -1; i <= visibleRows; ++i) {
            const long long row = firstRow - i;
            if (row < 0 || row >= totalRows)
                continue;
            const int pitch = visiblePitches_[(size_t)row];
            const int y = yForPitch(pitch);
            g.setColour(isBlackKeyPitchClass(((pitch % 12) + 12) % 12) ? blackRow : whiteRow);
            g.fillRect(0, y, width, rowHeight);
            g.setColour(rowSep.withAlpha(0.35f));
            g.drawHorizontalLine(y, 0.0f, (float)width);
        }
    }

    // Vertical lines at the CURRENT snap division (lightest), beats (medium) and bars (strongest) —
    // pure state, redrawn whenever the snap selector changes because the panel repaints us then.
    // dimColour is bg0, the same fill paint() puts down behind everything, and is what the shared
    // policy contrasts the lines against.
    paintGridLines(g, rowSep, dimColour);

    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return;

    // Dim the part of the grid outside [clipStart, clipEnd) — notes can only exist inside the
    // clip, so anything outside it is never editable.
    const float top = (float)canvasTop();
    const float bottom = (float)height;
    const float startX = (float)beatToX(clip->startBeat);
    const float endX = (float)beatToX(clip->startBeat + clip->lengthBeats);
    g.setColour(dimColour.withAlpha(0.55f));
    if (startX > 0.0f)
        g.fillRect(juce::Rectangle<float>(0.0f, top, juce::jlimit(0.0f, (float)width, startX), bottom - top));
    if (endX < (float)width) {
        const float clampedEnd = juce::jlimit(0.0f, (float)width, endX);
        g.fillRect(juce::Rectangle<float>(clampedEnd, top, (float)width - clampedEnd, bottom - top));
    }

    for (const auto& note : clip->notes)
        paintNote(g, note);
}

void PianoRollComponent::paintNote(juce::Graphics& g, const synth::MidiNote& note) {
    const auto* clip = doc_->getClip(clipId_); // guaranteed non-null by paintGrid's caller
    const auto geom = effectiveGeometryFor(note);
    const auto rect = computeNoteRect(clip->startBeat + geom.startBeat, geom.lengthBeats, geom.pitch);
    if (rect.getRight() < 0 || rect.getX() > getWidth())
        return; // cheap offscreen cull, same reasoning as TimelineClipLaneArea::paintClip

    const bool selected = selection_.contains(note.id);
    // geom.pitch/geom.velocity (not note.pitch/note.velocity) so a mid-drag move or velocity scrub
    // is coloured by what is actually being previewed — including the out-of-scale colour, if the
    // drag has carried the note out of the active scale.
    const auto paint = resolveNoteColourFor(geom.pitch, geom.velocity, selected, note.muted);
    const auto bodyBounds = rect.toFloat().reduced(0.5f, 1.0f);

    // The muted dimming (desaturate + lower alpha, see NoteColour.h's kMutedNote* constants) is
    // baked into `paint` already — a muted note still gets exactly this same fill+outline paint,
    // just dimmed, so its selection halo survives the dimming the same way it always has.
    g.setColour(paint.fill);
    g.fillRoundedRectangle(bodyBounds, 2.0f);
    g.setColour(paint.border);
    g.drawRoundedRectangle(bodyBounds, 2.0f, selected ? 2.0f : 1.0f);
}

void PianoRollComponent::paintDrawPreview(juce::Graphics& g) {
    if (dragMode_ != DragMode::DrawNew || drawLengthBeats_ <= 0.0)
        return;
    const auto* clip = doc_->getClip(clipId_);
    if (clip == nullptr)
        return;

    juce::Colour accent = juce::Colours::cyan;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        accent = lf->getTheme().colors.accent;

    const auto rect = computeNoteRect(clip->startBeat + drawStartBeat_, drawLengthBeats_, drawPitch_);
    const auto bounds = rect.toFloat().reduced(0.5f, 1.0f);
    g.setColour(accent.withAlpha(0.25f));
    g.fillRoundedRectangle(bounds, 2.0f);
    g.setColour(accent.withAlpha(0.8f));
    g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
}

void PianoRollComponent::paintSplitPreview(juce::Graphics& g) {
    if (!hasSplitPreview_)
        return;
    const auto* clip = doc_->getClip(clipId_);
    const auto* note = doc_->getNote(splitPreviewNote_);
    if (clip == nullptr || note == nullptr)
        return;

    juce::Colour accent = juce::Colours::cyan;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        accent = lf->getTheme().colors.accent;

    const float x = (float)beatToX(clip->startBeat + splitPreviewBeat_);
    const float y = (float)yForPitch(note->pitch);
    g.setColour(accent);
    g.fillRect(x - 0.5f, y, 1.0f, (float)std::max(1, (int)pixelsPerSemitone_));
}

void PianoRollComponent::paintPlayhead(juce::Graphics& g) {
    if (!hasPlayheadX_)
        return; // nothing has told us where the transport is yet

    const auto grid = gridRegion();
    const int x = getPlayheadLineX();
    if (grid.isEmpty() || x < grid.getX() || x > grid.getRight())
        return;

    juce::Colour accent = juce::Colours::cyan;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        accent = lf->getTheme().colors.accent;

    g.setColour(accent);
    g.fillRect((float)x - kPlayheadLineWidth * 0.5f, (float)grid.getY(), kPlayheadLineWidth, (float)grid.getHeight());
}

void PianoRollComponent::paintKeysColumn(juce::Graphics& g) {
    using namespace synth::theme;
    juce::Colour whiteKey, blackKey, sep, pressed;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        whiteKey = c.pianoKeyWhite;
        blackKey = c.pianoKeyBlack;
        sep = c.border;
        // The SAME token every other "this control is switched on" state in the timeline uses (the
        // edit-tool strip, the follow-playhead button, the roll's own Snap chip), so a held key reads
        // as lit rather than as a differently-coloured key.
        pressed = c.toolActive;
    } else {
        whiteKey = juce::Colours::whitesmoke;
        blackKey = juce::Colours::black;
        sep = juce::Colours::grey;
        pressed = juce::Colours::cyan;
    }

    // White fills the FULL column width first; every black-key row then overlays a narrower rect
    // flush left, leaving its own right portion showing the white fill underneath — the "gap
    // between black keys" a real keyboard shows when viewed side-on.
    g.setColour(whiteKey);
    g.fillRect(keysColumnBounds_);

    const int rowHeight = std::max(1, (int)pixelsPerSemitone_);
    // The label font SCALES with the row height rather than sitting fixed at the theme's micro
    // size: at the default 10px row a flat ~8.5px caption was unreadably small, and it stayed that
    // size at every zoom level, so a taller row from zooming in never bought a bigger label.
    // Clamped to [9, 13] — the floor matches keyLabelFor's own "too short a row for a per-key
    // label" cutoff (rowHeightPx < 9, below which every label collapses to C-only regardless of
    // this font size), and the ceiling brackets the theme's `type.label` token (10.5 — "knob
    // names, port labels, section heads", the nearest built-in size above `micro`) so a tall,
    // zoomed-in row reads like every other themed label instead of growing past it.
    const float labelFontPx = juce::jlimit(9.0f, 13.0f, (float)rowHeight - 3.0f);
    const int visibleRows = std::max(0, keysColumnBounds_.getHeight() / rowHeight) + 2;
    const int blackWidth = blackKeyInsetForTest();
    const long long totalRows = (long long)visiblePitches_.size();
    if (totalRows == 0) {
        g.setColour(sep);
        g.drawVerticalLine(keysColumnBounds_.getRight() - 1, (float)keysColumnBounds_.getY(),
                           (float)keysColumnBounds_.getBottom());
        return;
    }
    const long long firstRow = (long long)nearestVisibleRowIndex(firstVisiblePitch_);

    for (int i = -1; i <= visibleRows; ++i) {
        const long long row = firstRow - i;
        if (row < 0 || row >= totalRows)
            continue;
        const int pitch = visiblePitches_[(size_t)row];
        const int pitchClass = ((pitch % 12) + 12) % 12;
        const int y = yForPitch(pitch);
        const juce::Rectangle<int> rowRect(keysColumnBounds_.getX(), y, keysColumnBounds_.getWidth(), rowHeight);
        const bool isBlack = isBlackKeyPitchClass(pitchClass);
        // A key held down by the pointer (see beginKeysColumnPress) paints in the lit token instead of
        // its own fill — over the key's OWN rect, so a black key lights up only across its narrower
        // flush-left area and the white showing through beside it stays white. keyFill is reassigned
        // too, not just the drawn colour, so labelColourFor still contrasts against what is actually
        // underneath the text.
        const bool keyHeld = keysColumnPressing_ && pitch == keysColumnPitch_;
        const juce::Colour keyFill = keyHeld ? pressed : (isBlack ? blackKey : whiteKey);
        if (isBlack) {
            g.setColour(keyFill);
            g.fillRect(rowRect.getX(), rowRect.getY(), blackWidth, rowRect.getHeight());
        } else if (keyHeld) {
            g.setColour(keyFill);
            g.fillRect(rowRect);
        }

        // A real keyboard's only FULL-width key separators are between the two white-key pairs
        // with no black key between them (E/F, B/C) — every other boundary gets a subtler hint,
        // since a black key already does most of the visual separating there.
        const bool fullWidthSep = (pitchClass == 4 || pitchClass == 11); // boundary above E, above B
        g.setColour(sep.withAlpha(fullWidthSep ? 0.5f : 0.15f));
        g.drawHorizontalLine(y, (float)rowRect.getX(), (float)rowRect.getRight());

        const auto label = keyLabelFor(pitch, keyLabelMode_, rowHeight);
        if (label.isNotEmpty()) {
            // labelColourFor contrasts against THIS row's own keyFill — never a single colour
            // shared by both key colours. The drawn RECT matters just as much as the colour: a
            // sharp (black-key) label is confined to the black key's own (narrower, flush-left)
            // rect rather than the full column width. Right-aligning across the full column would
            // draw part of a light "contrasts against black" colour over the WHITE fill showing
            // through past the black key's right edge — light-on-light, nearly invisible (see
            // labelColourFor's comment). A natural (white-key) label still uses the full column:
            // white fills it edge to edge, so there is no second fill for the label to straddle.
            const juce::Rectangle<int> labelRect =
                isBlack ? juce::Rectangle<int>(rowRect.getX(), rowRect.getY(), blackWidth, rowRect.getHeight())
                              .reduced(3, 0)
                        : rowRect.reduced(3, 0);
            g.setColour(labelColourFor(keyFill));
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), labelFontPx, juce::Font::plain));
            g.drawText(label, labelRect, juce::Justification::centredRight, false);
        }
    }

    g.setColour(sep);
    g.drawVerticalLine(keysColumnBounds_.getRight() - 1, (float)keysColumnBounds_.getY(),
                       (float)keysColumnBounds_.getBottom());
}

void PianoRollComponent::paintHeader(juce::Graphics& g) {
    using namespace synth::theme;
    juce::Colour bg, border, restingFill, activeFill;
    float pillRadius = 3.0f;
    // The header's drawn button labels ("Clips", "Q", "Scale") used to share the theme's micro
    // size (~8.5px) with the keys column's captions — unreadably small for text meant to be READ
    // rather than glanced at as a row hint. Bumped to the theme's next size up: TimelineTransportBar
    // uses exactly this `type.value + 1.0f` expression (~11px) for its own drawn text (the tempo/
    // position readout), so this reuses that precedent rather than inventing a second "one size
    // above micro" constant. The header stays 20px tall and every button rect is reduced to ~16px,
    // comfortably clearing an 11px font with room to spare.
    float buttonFontPx = 11.0f;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        bg = c.surface;
        border = c.border;
        // Resting chip fill is surfaceHi ("raised surface"), never `surface` again — the header
        // strip itself is filled with `surface` (bg above), and a chip painted in the SAME colour
        // as what is behind it would be invisible. The active/toggled fill is toolActive — the
        // SAME token the timeline's edit-tool strip and follow-playhead button use for their own
        // lit state (see TimelinePanelComponent::applyToolStripTheme), so "this control is switched
        // on" reads identically everywhere in the timeline.
        restingFill = c.surfaceHi;
        activeFill = c.toolActive;
        buttonFontPx = lf->getTheme().type.value + 1.0f;
        pillRadius = lf->getTheme().metrics.pillRadius;
    } else {
        bg = juce::Colours::darkslategrey;
        border = juce::Colours::grey;
        restingFill = juce::Colours::darkslategrey.brighter(0.15f);
        activeFill = juce::Colours::cyan;
    }

    g.setColour(bg);
    g.fillRect(0, 0, getWidth(), kToolbarHeight);
    g.setColour(border);
    g.drawHorizontalLine(kToolbarHeight - 1, 0.0f, (float)getWidth());

    // Every drawn header control gets the SAME chip treatment a real juce::TextButton gets from
    // AppLookAndFeel::drawButtonBackground (resting = a raised surface, hover = .brighter(0.12f),
    // always the theme's border token as the outline) — so "Clips"/"Q"/"Scale" read as buttons
    // rather than bare text with a hairline outline. Returns the fill actually painted, so the
    // caller's text/glyph colour can contrast against THAT rather than a fixed textCol that could
    // sit invisible on the active (toolActive) fill.
    const auto paintChip = [&](juce::Rectangle<int> bounds, bool active, bool hovered) {
        juce::Colour fill = active ? activeFill : restingFill;
        if (hovered)
            fill = fill.brighter(0.12f);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds.toFloat(), pillRadius);
        g.setColour(border);
        g.drawRoundedRectangle(bounds.toFloat(), pillRadius, 1.0f);
        return fill;
    };

    // Back button: chip background first, then the arrow triangle + "Clips" text on top (never a
    // Unicode glyph through a themed font — the same "draw it, don't asset it" rule
    // TimelineTransportBar's GlyphButton follows for its own one-off shapes). No active state: it
    // is an action, not a toggle.
    const auto backFill = paintChip(backButtonBounds_, /*active=*/false, hoveredHeaderButton_ == HeaderButtonId::Back);
    const auto backTextCol = backFill.contrasting(0.9f);
    auto arrowArea = backButtonBounds_.withWidth(9).reduced(0, 4);
    juce::Path arrow;
    arrow.addTriangle((float)arrowArea.getRight(), (float)arrowArea.getY(), (float)arrowArea.getRight(),
                      (float)arrowArea.getBottom(), (float)arrowArea.getX(), (float)arrowArea.getCentreY());
    g.setColour(backTextCol);
    g.fillPath(arrow);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), buttonFontPx, juce::Font::plain));
    g.drawText("Clips", backButtonBounds_.withTrimmedLeft(12), juce::Justification::centredLeft, false);

    // SNAP toggle: the one chip here that paints lit for grid magnetism (snapEnabled AND a division to
    // snap to) — a real on/off switch, not an action. It used to be labelled "Q", which was the bug
    // this glyph fixes: "Q" is the letter the TIMELINE binds to snap, so the same letter meant "snap"
    // on one surface and "quantise" on the other. A drawn magnet says which it is in any language.
    const bool snapOn = viewState_.snapEnabled && viewState_.snap != TimelineViewState::Snap::Off;
    const auto snapFill = paintChip(snapButtonBounds_, snapOn, hoveredHeaderButton_ == HeaderButtonId::Snap);
    drawSnapGlyph(g, snapButtonBounds_, snapFill.contrasting(0.9f));

    // QUANTISE (note starts -> grid): an ACTION, never lit, but dimmed when it would do nothing (no
    // division chosen, or an empty clip). The momentary press flash — ended by the one-shot timer in
    // timerCallback() — sits ON TOP of whichever base fill is showing, so every press is acknowledged
    // even when the click is a no-op.
    const auto quantiseFill =
        paintChip(quantiseButtonBounds_, /*active=*/false, hoveredHeaderButton_ == HeaderButtonId::Quantise);
    if (quantiseFlash_) {
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillRoundedRectangle(quantiseButtonBounds_.toFloat(), pillRadius);
    }
    drawQuantiseGlyph(g, quantiseButtonBounds_,
                      quantiseFill.contrasting(0.9f).withMultipliedAlpha(isQuantiseEnabled() ? 1.0f : 0.45f));

    // QUANTISE PITCHES (note pitches -> scale): the same action-and-dimmed treatment, and a glyph
    // that differs from the one above on the AXIS it snaps along — horizontal blocks onto vertical
    // gridlines up there, a note head onto horizontal rows here.
    const auto pitchFill = paintChip(quantisePitchButtonBounds_, /*active=*/false,
                                     hoveredHeaderButton_ == HeaderButtonId::QuantisePitches);
    drawQuantisePitchGlyph(g, quantisePitchButtonBounds_,
                           pitchFill.contrasting(0.9f).withMultipliedAlpha(isPitchQuantiseEnabled() ? 1.0f : 0.45f));

    // Scale button: keeps its word, because "Scale" is the one label here that names a NOUN (a panel)
    // rather than a verb, and a glyph for "the scale picker" would be a guess. Lit state tracks the
    // LOGICAL target (scalePanelVisible_ — what the panel is animating TOWARDS) rather than the child
    // component's own on-screen flag, which stays true for the whole close slide too.
    const auto scaleFill =
        paintChip(scaleButtonBounds_, scalePanelVisible_, hoveredHeaderButton_ == HeaderButtonId::Scale);
    g.setColour(scaleFill.contrasting(0.9f));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), buttonFontPx, juce::Font::plain));
    g.drawText("Scale", scaleButtonBounds_, juce::Justification::centred, false);

    // SHOW ONLY SCALE NOTES: a toggle, so it paints lit like Snap does. Dimmed with no scale chosen —
    // the flag is still remembered (arm it first, pick the scale second), it simply has no visible
    // effect yet, and saying so is better than a control that looks live and does nothing.
    const auto filterFill =
        paintChip(scaleFilterButtonBounds_, isScaleFilterOn(), hoveredHeaderButton_ == HeaderButtonId::ScaleFilter);
    drawScaleFilterGlyph(
        g, scaleFilterButtonBounds_,
        filterFill.contrasting(0.9f).withMultipliedAlpha(activeScaleForOpenClip().has_value() ? 1.0f : 0.45f));
}

//==============================================================================
// ---- Header chip glyphs ----
//
// All four are pure drawing against the chip's rect: no theme lookup (the caller resolves the colour
// against the fill it actually painted), no state, no font. Stroke widths are fixed at 1.0-1.6 px
// because these are ~16x16 px chips — a themed line-width token would round to the same pixel and
// only add a way for them to disagree with each other.

void PianoRollComponent::drawSnapGlyph(juce::Graphics& g, juce::Rectangle<int> chip, juce::Colour colour) {
    // A MAGNET, horseshoe up: a half-annulus arc with two legs, the universal "magnetic snap" mark
    // (Cubase, Blender, every CAD tool). Reads as a distinct silhouette at 16 px, which a letter
    // sharing its shape with the timeline's own snap key did not.
    const auto area = chip.toFloat().reduced(4.5f, 4.0f);
    if (area.getWidth() < 4.0f || area.getHeight() < 4.0f)
        return;

    const float cx = area.getCentreX();
    const float legTop = area.getCentreY() + area.getHeight() * 0.05f;
    const float radius = area.getWidth() * 0.5f;
    const float thickness = juce::jmax(1.2f, area.getWidth() * 0.28f);

    juce::Path arc;
    // Outer half-circle across the top, then back along the inner radius — one closed shape, so the
    // horseshoe's opening is a real hole rather than a stroke that thins at the crown.
    arc.addCentredArc(cx, legTop, radius, radius, 0.0f, -juce::MathConstants<float>::halfPi,
                      juce::MathConstants<float>::halfPi, true);
    arc.addCentredArc(cx, legTop, radius - thickness, radius - thickness, 0.0f, juce::MathConstants<float>::halfPi,
                      -juce::MathConstants<float>::halfPi, false);
    arc.closeSubPath();
    g.setColour(colour);
    g.fillPath(arc);

    // The two poles below the crown, one per leg.
    const float legBottom = area.getBottom();
    g.fillRect(juce::Rectangle<float>(cx - radius, legTop, thickness, legBottom - legTop));
    g.fillRect(juce::Rectangle<float>(cx + radius - thickness, legTop, thickness, legBottom - legTop));
}

void PianoRollComponent::drawQuantiseGlyph(juce::Graphics& g, juce::Rectangle<int> chip, juce::Colour colour) {
    // Note blocks LANDED on VERTICAL gridlines: two faint full-height lines, and two small filled
    // blocks sitting flush on them at different heights. The axis is the whole point — this verb moves
    // notes horizontally in time, so the grid it snaps to is vertical.
    const auto area = chip.toFloat().reduced(3.5f, 3.0f);
    if (area.getWidth() < 5.0f || area.getHeight() < 5.0f)
        return;

    const float leftLine = area.getX() + area.getWidth() * 0.22f;
    const float rightLine = area.getX() + area.getWidth() * 0.72f;
    g.setColour(colour.withMultipliedAlpha(0.45f));
    g.fillRect(juce::Rectangle<float>(leftLine, area.getY(), 1.0f, area.getHeight()));
    g.fillRect(juce::Rectangle<float>(rightLine, area.getY(), 1.0f, area.getHeight()));

    // Flush-LEFT on each line (their leading edge is what a start quantise aligns), and vertically
    // staggered so the pair reads as two separate notes rather than one bar.
    const float blockW = juce::jmax(3.0f, area.getWidth() * 0.28f);
    const float blockH = juce::jmax(2.0f, area.getHeight() * 0.26f);
    g.setColour(colour);
    g.fillRect(juce::Rectangle<float>(leftLine, area.getY() + area.getHeight() * 0.12f, blockW, blockH));
    g.fillRect(juce::Rectangle<float>(rightLine, area.getBottom() - area.getHeight() * 0.12f - blockH, blockW, blockH));
}

void PianoRollComponent::drawQuantisePitchGlyph(juce::Graphics& g, juce::Rectangle<int> chip, juce::Colour colour) {
    // A note head landing on a HORIZONTAL row, with a down arrow pushing it there: three faint row
    // lines, a filled ellipse on the bottom one, an arrow above it. Same "snapped onto the grid" idea
    // as the chip to its left, rotated 90 degrees — which is exactly the difference between the two
    // verbs, and the reason a viewer can tell them apart without the tooltip.
    const auto area = chip.toFloat().reduced(3.0f, 3.0f);
    if (area.getWidth() < 5.0f || area.getHeight() < 6.0f)
        return;

    const float rowSpacing = area.getHeight() / 3.0f;
    g.setColour(colour.withMultipliedAlpha(0.45f));
    for (int i = 1; i <= 3; ++i)
        g.fillRect(
            juce::Rectangle<float>(area.getX(), area.getY() + rowSpacing * (float)i - 0.5f, area.getWidth(), 1.0f));

    const float headW = juce::jmax(3.5f, area.getWidth() * 0.46f);
    const float headH = juce::jmax(2.5f, rowSpacing * 0.9f);
    const float headY = area.getY() + rowSpacing * 3.0f - headH * 0.5f;
    g.setColour(colour);
    g.fillEllipse(area.getX(), headY, headW, headH);

    // The arrow shares the note head's x centre and points DOWN at it, so the glyph reads as one
    // motion rather than two unrelated marks.
    const float ax = area.getX() + headW * 0.5f;
    const float arrowTop = area.getY();
    const float arrowTip = headY - 1.0f;
    if (arrowTip > arrowTop) {
        g.fillRect(juce::Rectangle<float>(ax - 0.5f, arrowTop, 1.0f, arrowTip - arrowTop));
        juce::Path head;
        const float halfSpan = juce::jmax(1.5f, area.getWidth() * 0.16f);
        head.addTriangle(ax - halfSpan, arrowTip - halfSpan, ax + halfSpan, arrowTip - halfSpan, ax, arrowTip);
        g.fillPath(head);
    }
}

void PianoRollComponent::drawScaleFilterGlyph(juce::Graphics& g, juce::Rectangle<int> chip, juce::Colour colour) {
    // A FUNNEL: the one mark that reads as "filter" everywhere, and what this toggle does — it filters
    // which pitch ROWS the grid offers. Deliberately not an eye (that would say "hide/show a layer",
    // and the out-of-scale rows are removed from the row mapping, not merely made invisible) and not a
    // keyboard (indistinguishable from the keys column two pixels below it).
    const auto area = chip.toFloat().reduced(4.0f, 3.5f);
    if (area.getWidth() < 4.0f || area.getHeight() < 5.0f)
        return;

    const float mouthY = area.getY() + area.getHeight() * 0.52f;
    const float stemHalf = juce::jmax(0.75f, area.getWidth() * 0.10f);
    const float cx = area.getCentreX();

    juce::Path funnel;
    funnel.startNewSubPath(area.getX(), area.getY());
    funnel.lineTo(area.getRight(), area.getY());
    funnel.lineTo(cx + stemHalf, mouthY);
    funnel.lineTo(cx + stemHalf, area.getBottom());
    funnel.lineTo(cx - stemHalf, area.getBottom());
    funnel.lineTo(cx - stemHalf, mouthY);
    funnel.closeSubPath();

    g.setColour(colour);
    g.fillPath(funnel);
}

void PianoRollComponent::paintMarquee(juce::Graphics& g) {
    if (marqueeRect_.isEmpty())
        return;

    // SAME token recipe as GraphEditor's module-marquee band (GraphEditor.cpp
    // GraphEditor::Content::paint, "Marquee selection band", issue #156) and
    // TimelineClipLaneArea::paintMarquee: accent fill at low alpha + a brighter accent border at
    // the theme's guideLineWidth. Previously a flat white at low alpha, easy to lose against a
    // light theme's background; this keeps all three marquees moving together on a theme change.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour accentColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colours::cyan;
    const float lineWidth = lf != nullptr ? lf->getTheme().metrics.guideLineWidth : 1.5f;

    const auto bandF = marqueeRect_.toFloat();
    g.setColour(accentColour.withAlpha(0.12f));
    g.fillRect(bandF);
    g.setColour(accentColour.withAlpha(0.80f));
    g.drawRect(bandF, lineWidth);
}

//==============================================================================
// ---- Local playhead (driven by TimelinePlayheadOverlay — no timer here) ----

int PianoRollComponent::getPlayheadLineX() const noexcept {
    // Guard rail identical to the overlay's: llround of an unbounded double is not well-behaved,
    // and nothing beyond a few screen widths can ever be visible.
    constexpr double kMaxLineXMagnitude = 1.0e7;
    return (int)std::llround(std::clamp(beatToX(playheadBeat_), -kMaxLineXMagnitude, kMaxLineXMagnitude));
}

juce::Rectangle<int> PianoRollComponent::playheadStripFor(int x) const noexcept {
    return {x - kPlayheadStripHalfWidth, 0, 2 * kPlayheadStripHalfWidth + 1, getHeight()};
}

void PianoRollComponent::requestRepaintStrip(juce::Rectangle<int> strip) { repaint(strip); }

void PianoRollComponent::requestRepaintPreviewStrip(juce::Rectangle<int> strip) { repaint(strip); }

void PianoRollComponent::requestRepaintHeaderButtonStrip(juce::Rectangle<int> strip) { repaint(strip); }

juce::Rectangle<int> PianoRollComponent::headerButtonBoundsFor(HeaderButtonId which) const noexcept {
    switch (which) {
    case HeaderButtonId::Back:
        return backButtonBounds_;
    case HeaderButtonId::Snap:
        return snapButtonBounds_;
    case HeaderButtonId::Quantise:
        return quantiseButtonBounds_;
    case HeaderButtonId::QuantisePitches:
        return quantisePitchButtonBounds_;
    case HeaderButtonId::ScaleFilter:
        return scaleFilterButtonBounds_;
    case HeaderButtonId::Scale:
        return scaleButtonBounds_;
    case HeaderButtonId::None:
        break;
    }
    return {};
}

void PianoRollComponent::updateHeaderButtonHover(juce::Point<int> pos) {
    HeaderButtonId next = HeaderButtonId::None;
    if (backButtonBounds_.contains(pos))
        next = HeaderButtonId::Back;
    else if (snapButtonBounds_.contains(pos))
        next = HeaderButtonId::Snap;
    else if (quantiseButtonBounds_.contains(pos))
        next = HeaderButtonId::Quantise;
    else if (quantisePitchButtonBounds_.contains(pos))
        next = HeaderButtonId::QuantisePitches;
    else if (scaleFilterButtonBounds_.contains(pos))
        next = HeaderButtonId::ScaleFilter;
    else if (scaleButtonBounds_.contains(pos))
        next = HeaderButtonId::Scale;

    if (next == hoveredHeaderButton_)
        return; // state-change gate: hovering the SAME chip (or none) costs nothing
    const auto oldRect = headerButtonBoundsFor(hoveredHeaderButton_);
    hoveredHeaderButton_ = next;
    const auto newRect = headerButtonBoundsFor(hoveredHeaderButton_);
    if (!oldRect.isEmpty())
        requestRepaintHeaderButtonStrip(oldRect);
    if (!newRect.isEmpty())
        requestRepaintHeaderButtonStrip(newRect);
}

void PianoRollComponent::setPlayheadBeat(double absoluteBeat) {
    if (!std::isfinite(absoluteBeat))
        return;
    playheadBeat_ = absoluteBeat;

    // Follow: page-flip the roll's OWN view BEFORE the strip diff below runs, so that diff is
    // computed against the (possibly just-moved) mapping the line will actually draw at. Gated on
    // no drag being in flight and the edge-auto-scroll timer being idle — either one is already
    // steering rollView_ on its own terms, and a follow flip landing on top of it would fight the
    // gesture the user is mid-way through. The check costs nothing while the beat is already
    // inside the view (the common, playing-and-visible case): it returns without ever calling
    // setHorizontalView, so the zero-repaint-while-unmoved contract below is untouched — follow
    // adds no work at all until the line is about to leave the visible grid.
    if (followPlayhead_ && dragMode_ == DragMode::None && !autoScrollTimer_.isTimerRunning()) {
        const auto grid = gridRegion();
        if (!grid.isEmpty() && rollView_.pixelsPerBeat > 0.0) {
            const int rawX = (int)std::llround(beatToX(absoluteBeat));
            if (rawX < grid.getX() || rawX > grid.getRight()) {
                // Land the beat a TENTH of the visible span past the gutter rather than flush
                // against it — a beat sitting exactly on the edge would immediately re-trigger the
                // same flip on the very next tick once it advances one more sample.
                const double visibleBeats = (double)grid.getWidth() / rollView_.pixelsPerBeat;
                setHorizontalView(rollView_.pixelsPerBeat, std::max(0.0, absoluteBeat - 0.1 * visibleBeats));
            }
        }
    }

    const int x = getPlayheadLineX();
    // The FIRST beat after an open is a real change (there is no line on screen yet), so it costs
    // exactly one strip. Every later beat that lands on the same pixel costs nothing — that is what
    // keeps a stopped transport at zero repaints.
    const int previousX = hasPlayheadX_ ? playheadLineX_ : x;
    if (hasPlayheadX_ && x == playheadLineX_)
        return;
    hasPlayheadX_ = true;
    playheadLineX_ = x;

    const auto strip = playheadStripFor(previousX).getUnion(playheadStripFor(x)).getIntersection(gridRegion());
    if (!strip.isEmpty())
        requestRepaintStrip(strip);
}

//==============================================================================
void PianoRollComponent::resized() {
    // THE one carve-up. Vertically the roll is three bands, top to bottom:
    //
    //   1. the CHIP TOOLBAR      (kToolbarHeight)   — this component's own chrome
    //   2. the RULER band        (rulerBandHeight_) — left blank for the panel's ruler, a SIBLING
    //                                                 drawn on top of it (see setRulerBandHeight)
    //   3. the NOTE CANVAS       (the remainder)    — keys column gutter + grid
    //
    // Band 2 is why the toolbar ends up ABOVE the ruler instead of sandwiched between the ruler and
    // the notes: the roll's rect spans the whole unit and simply does not paint that strip.
    // rulerBandHeight_ is 0 by default, which collapses this to the original toolbar-then-canvas
    // layout for a bare roll. Adding another context toolbar later is one more removeFromTop HERE and
    // a second constant — no y-coordinate anywhere else in the file changes, because everything below
    // reads canvasTop().
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(kToolbarHeight);
    bounds.removeFromTop(rulerBandHeight_);
    backButtonBounds_ = header.removeFromLeft(60).reduced(3, 2);
    header.removeFromLeft(4);
    // Six chips. The GAPS carry meaning: 4 px separates groups, 2 px separates members of one group,
    // so "snap + the two quantise verbs" read as one cluster and "scale + its row filter" as another.
    // Every glyph chip is the same 24 px so the row reads as a toolbar rather than a ransom note.
    snapButtonBounds_ = header.removeFromLeft(24).reduced(2, 2);
    header.removeFromLeft(2);
    quantiseButtonBounds_ = header.removeFromLeft(24).reduced(2, 2);
    header.removeFromLeft(2);
    quantisePitchButtonBounds_ = header.removeFromLeft(24).reduced(2, 2);
    header.removeFromLeft(4);
    scaleButtonBounds_ = header.removeFromLeft(50).reduced(2, 2);
    header.removeFromLeft(2);
    scaleFilterButtonBounds_ = header.removeFromLeft(24).reduced(2, 2);

    // The panel sits WEST of the keys column, below the toolbar and ruler rows (its own controls, not
    // the toolbar's chips, are how the user works it) — carved BEFORE the keys column, at its
    // CURRENT animated width (see leftGutterWidth()), so leftGutterWidth() and this layout can
    // never disagree about where the grid starts, at any point in the slide.
    const int scaleWidth = leftGutterWidth() - kKeysColumnWidth;
    if (scaleWidth > 0)
        scalePanel_.setBounds(bounds.removeFromLeft(scaleWidth));
    else
        scalePanel_.setBounds({});

    keysColumnBounds_ = bounds.removeFromLeft(kKeysColumnWidth);
    noteGridBounds_ = bounds; // a REAL gutter: beatToX(firstVisibleBeat) == this rect's left edge
}

} // namespace synth::ui
