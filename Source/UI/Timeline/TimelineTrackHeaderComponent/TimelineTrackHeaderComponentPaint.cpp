// TimelineTrackHeaderComponentPaint.cpp
//
// Layout and painting for TimelineTrackHeaderComponent: theme-derived colours, resized() and the
// fold-arrow/badge layout, paint(), the reorder-drag lift and paintOverChildren(). The class is
// declared in TimelineTrackHeaderComponent.h; sibling TimelineTrackHeaderComponent*.cpp files in
// this directory hold the rest of it.

#include "TimelineTrackHeaderComponent.h"
#include "TimelineTrackHeaderInternal.h"

#include "UI/Layout/FocusRegion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kSwatchWidth = 8;
constexpr int kHeightHandleThickness = 5; // the strip along the row's bottom edge
// Widened from 20 (laid out edge-to-edge, no gap): the M/S/R toggles read as one fused
// block at that width, and this row was the worst offender in the timeline-panel button-size
// sweep. Paired with kToggleGap below rather than just grown, so the buttons are also visually
// separable now.
constexpr int kToggleWidth = 24;
// Inter-toggle gap, applied only BETWEEN adjacent M/S/R buttons (never before the first or after
// the last) — see TimelineTrackHeaderComponent::resized(). Growing kToggleWidth alone would have
// left them still touching.
constexpr int kToggleGap = 4;
constexpr int kRowPadding = 3;
// Narrowed from 34 now that the badge draws a themed icon rather than "MIDI"/"AUD"/"AUTO" text —
// the icon needs far less width than the longest label did, and the freed space goes to the name.
constexpr int kKindBadgeWidth = 20;
constexpr float kKindBadgeIconSize = 14.0f;
constexpr int kLaneBadgeHeight = 14;

// The theme's colours the header paints with.
struct HeaderColours {
    juce::Colour surface;
    juce::Colour surfaceHi;
    juce::Colour border;
    juce::Colour text;
    juce::Colour textMuted;
    juce::Colour warning;
    juce::Colour accent;
    juce::Colour bg0;
    juce::Colour muteOn;
    juce::Colour soloOn;
    juce::Colour armOn;
};

HeaderColours coloursFor(const juce::Component& component) {
    const auto& c = synth::theme::themeOf(component).colors;
    HeaderColours result;
    result.surface = c.surface;
    result.surfaceHi = c.surfaceHi;
    result.border = c.border;
    result.text = c.textPrimary;
    result.textMuted = c.textMuted;
    result.warning = c.warning;
    result.accent = c.accent;
    result.bg0 = c.bg0;
    result.muteOn = c.trackMuteOn;
    result.soloOn = c.trackSoloOn;
    result.armOn = c.trackArmOn;
    return result;
}

} // namespace

//==============================================================================
// Every colour this component bakes via setColour rather than reading live in paint(): the
// binding chip's warning/normal treatment (moved here unchanged from refreshFromDoc(), which
// used to be the ONLY place that applied it — hence the theme-switch bug this fixes) plus the
// M/S/R buttons' active-state colours. `chipWarning_` and the chip's visibility are DOC state, so
// this only ever re-applies colours for whatever state refreshFromDoc() last computed; it never
// recomputes which state that is.
void TimelineTrackHeaderComponent::applyThemeDerivedColours() {
    const auto colours = coloursFor(*this);

    bindingChip_.setColour(juce::TextButton::buttonColourId, chipWarning_ ? colours.warning : colours.surface);
    bindingChip_.setColour(juce::TextButton::textColourOffId, chipWarning_ ? colours.surface : colours.text);

    // Active-state fill for each of M/S/R, with a dark contrasting label so the button text still
    // reads once the fill turns bright orange/yellow/red — the same buttonOnColourId/bg0 pairing
    // AppLookAndFeel's own defaults use for the accent-coloured "on" state everywhere else.
    muteButton_.setColour(juce::TextButton::buttonOnColourId, colours.muteOn);
    muteButton_.setColour(juce::TextButton::textColourOnId, colours.bg0);
    soloButton_.setColour(juce::TextButton::buttonOnColourId, colours.soloOn);
    soloButton_.setColour(juce::TextButton::textColourOnId, colours.bg0);
    armButton_.setColour(juce::TextButton::buttonOnColourId, colours.armOn);
    armButton_.setColour(juce::TextButton::textColourOnId, colours.bg0);
}

void TimelineTrackHeaderComponent::lookAndFeelChanged() { applyThemeDerivedColours(); }

//==============================================================================
void TimelineTrackHeaderComponent::resized() {
    if (isSectionHeader()) { // one short row: arrow, "Unassigned automation", lane badge
        auto row = getLocalBounds().reduced(kRowPadding, 0).withTrimmedLeft(kRowPadding);
        layoutFoldArrowAndBadges(row);
        nameLabel_.setBounds(row);
        heightHandle_.setVisible(false); // a fixed section row, not a clip row to resize
        return;
    }
    heightHandle_.setVisible(true);
    heightHandle_.setBounds(getLocalBounds().removeFromBottom(kHeightHandleThickness));
    heightHandle_.toFront(false);
    auto bounds = getLocalBounds().reduced(kRowPadding);

    colourSwatch_.setBounds(bounds.removeFromLeft(kSwatchWidth));
    bounds.removeFromLeft(kRowPadding);

    // Top row: (fold arrow) + kind badge + (folded lane badge) + name, then R/S/M right to left.
    // Bottom row: the binding chip, full width.
    auto topRow = bounds.removeFromTop(bounds.getHeight() / 2);
    armButton_.setBounds(topRow.removeFromRight(kToggleWidth));
    topRow.removeFromRight(kToggleGap);
    soloButton_.setBounds(topRow.removeFromRight(kToggleWidth));
    topRow.removeFromRight(kToggleGap);
    muteButton_.setBounds(topRow.removeFromRight(kToggleWidth));

    layoutFoldArrowAndBadges(topRow);
    nameLabel_.setBounds(topRow);

    // Bottom row: the binding chip, sharing with the channel chip when one is showing.
    auto bottomRow = bounds.reduced(0, 1);
    const auto showSlot = bottomRow.removeFromRight(TrackShowModuleButton::kSize);
    showModuleButton_.setBounds(showSlot.withSizeKeepingCentre(
        TrackShowModuleButton::kSize, juce::jmin(TrackShowModuleButton::kSize, showSlot.getHeight())));
    bottomRow.removeFromRight(kRowPadding);
    if (const float p = channelChipFade_.progress(); p > 0.0f) { // the chip's share grows and shrinks with its fade
        channelChip_.setBounds(
            bottomRow.removeFromRight(juce::roundToInt(static_cast<float>(bottomRow.getWidth() / 2) * p)));
        bottomRow.removeFromRight(juce::roundToInt(static_cast<float>(kRowPadding) * p));
    }
    bindingChip_.setBounds(bottomRow);
}

// The arrow leads the name row so it lines up with the lane rows indented beneath it. Only the
// section header draws a lane-count badge: a track row's header column is too narrow to fit one
// beside the name, so a folded track's arrow says the count in its name and tooltip instead.
void TimelineTrackHeaderComponent::layoutFoldArrowAndBadges(juce::Rectangle<int>& row) {
    const auto* t = track();
    const int laneCount = t != nullptr ? shownLaneCount() : 0;
    if (const float p = foldArrowFade_.progress(); p > 0.0f) // the arrow's slot opens and closes with its fade
        foldArrow_.setBounds(row.removeFromLeft(juce::roundToInt(static_cast<float>(TrackFoldArrow::kSize) * p))
                                 .withSizeKeepingCentre(TrackFoldArrow::kSize, TrackFoldArrow::kSize));
    kindBadgeBounds_ = isSectionHeader() ? juce::Rectangle<int>() : row.removeFromLeft(kKindBadgeWidth);
    laneBadgeBounds_ = {};
    if (laneCount > 0 && isSectionHeader()) {
        const int width = laneCountBadgeWidth(laneCount);
        laneBadgeBounds_ = row.removeFromRight(width).withSizeKeepingCentre(width, kLaneBadgeHeight);
    }
}

void TimelineTrackHeaderComponent::paint(juce::Graphics& g) {
    const auto colours = coloursFor(*this);

    if (isSectionHeader()) {
        g.fillAll(colours.surfaceHi);
        g.setColour(colours.border);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, (float)getWidth());
        if (const auto* t = track())
            paintLaneCountBadge(g, laneBadgeBounds_, shownLaneCount(), *this);
        return;
    }

    g.fillAll(colours.surface);
    if (selected_) {
        g.setColour(colours.accent.withAlpha(0.16f));
        g.fillRect(getLocalBounds());
    }

    // Tinted left edge in the track's colour — the row's identity at a glance, even when the
    // swatch button itself is under the cursor.
    g.setColour(resolvedColour_.withMultipliedAlpha(0.35f));
    g.fillRect(0, 0, 3, getHeight());

    g.setColour(colours.border);
    g.drawHorizontalLine(getHeight() - 1, 0.0f, (float)getWidth());

    // Track-kind badge: a themed glyph right of the swatch, before the name. Fixed per TrackKind
    // (see kindBadgeIcon above) — this is identity chrome, not a control. Falls back to the old
    // text pill when there's no themed LnF (headless) or the icon asset is absent, so a headless
    // build/test still gets a legible badge.
    if (const auto* t = track()) {
        auto badgeBounds = kindBadgeBounds_.reduced(1, 3).toFloat();
        g.setColour(colours.textMuted.withAlpha(0.15f));
        g.fillRoundedRectangle(badgeBounds, 3.0f);
        g.setColour(colours.textMuted.withAlpha(0.6f));
        g.drawRoundedRectangle(badgeBounds, 3.0f, 1.0f);

        const auto* icon = [this, t]() -> const juce::Drawable* {
            if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
                return lf->peekIcon(detail::kindBadgeIcon(t->kind));
            return nullptr;
        }();

        if (icon != nullptr) {
            const auto iconArea =
                juce::Rectangle<float>(kKindBadgeIconSize, kKindBadgeIconSize).withCentre(badgeBounds.getCentre());
            icon->drawWithin(g, iconArea, juce::RectanglePlacement::centred, 1.0f);
        } else {
            float microSize = 8.5f;
            if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
                microSize = lf->getTheme().type.micro;
            g.setColour(colours.textMuted);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), microSize, juce::Font::plain));
            g.drawText(detail::kindBadgeText(t->kind), kindBadgeBounds_, juce::Justification::centred, false);
        }
        paintLaneCountBadge(g, laneBadgeBounds_, shownLaneCount(), *this);
    }
}

// 0..1 strength of the lifted look (a light wash and an accent border) while the row is being
// reorder-dragged; a no-op when unchanged.
void TimelineTrackHeaderComponent::setLift(float lift) {
    if (lift == lift_)
        return;
    lift_ = lift;
    repaint();
}

void TimelineTrackHeaderComponent::paintOverChildren(juce::Graphics& g) {
    if (lift_ > 0.0f) {
        const auto accent = synth::theme::themeOf(*this).colors.accent;
        g.setColour(juce::Colours::white.withAlpha(0.05f * lift_));
        g.fillRect(getLocalBounds());
        g.setColour(accent.withMultipliedAlpha(lift_));
        g.drawRect(getLocalBounds(), 1);
    }
    // Reuses the region-root outline verbatim (same colour/alpha/thickness) rather than a
    // bespoke treatment — a track header row is now a real focusable leaf exactly the way a region
    // root is, just nested one level deeper (see docs/timeline/tracks.md#focus-outline).
    synth::ui::paintFocusRegionOutline(*this, g);
}

} // namespace synth::ui
