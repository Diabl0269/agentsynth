// DrumMachineComponent: painting and the local playhead. The toolbar's controls are child components; this unit
// draws the clip name, the cells and the playhead line. Only the cells in view are visited, and a playhead move
// repaints just the old and new column strips and the line, so a stopped transport costs nothing.

#include "DrumMachineComponent.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kSwitchWidth = 184;
constexpr int kPlayClipSlotWidth = 72;
constexpr int kShowAllWidth = 92;
constexpr int kStepsComboWidth = 56;
constexpr int kStepsCaptionWidth = 78;
constexpr int kSwingCaptionWidth = 44;
constexpr int kSwingKnobSize = 18;
constexpr int kSwingValueWidth = 34;
constexpr int kHumaniseCaptionWidth = 62;
constexpr int kIdeasWidth = 56;
constexpr juce::uint32 kChangedOutlineArgb = 0xffFFB454;
constexpr int kToolbarPad = 4;
constexpr float kRestingAlpha = 0.55f;
constexpr float kSilencedAlpha = 0.35f;
constexpr float kMutedHitAlpha = 0.4f;
constexpr float kBeatGroupShadeAlpha = 0.03f;
constexpr float kPlayColumnAlpha = 0.16f;
constexpr float kFlashAlpha = 0.55f;
constexpr float kAccentTickHeight = 2.0f;
constexpr float kChanceFillAlpha = 0.5f;
constexpr int kDenseStepPx = 3;
constexpr float kPlayheadLineWidth = TimelinePlayheadOverlay::kLineWidth;
constexpr int kPlayheadStripHalfWidth = TimelinePlayheadOverlay::kStripHalfWidth;
constexpr double kMaxLineXMagnitude = 1.0e6;
} // namespace

static_assert(DrumMachineComponent::kToolbarHeight == 20, "the toolbar matches the piano roll's header height");

//==============================================================================
void DrumMachineComponent::layoutToolbar() {
    auto header = getLocalBounds().removeFromTop(kToolbarHeight).reduced(kToolbarPad, 1);
    const int nameWidth =
        std::max(0, header.getWidth() - kSwitchWidth - kPlayClipSlotWidth - kShowAllWidth - kStepsComboWidth -
                        kStepsCaptionWidth - kSwingCaptionWidth - kSwingKnobSize - kSwingValueWidth -
                        kHumaniseCaptionWidth - kSwingKnobSize - kSwingValueWidth - kIdeasWidth - 10 * kToolbarPad);
    nameBounds_ = header.removeFromLeft(std::min(nameWidth, 220));
    header.removeFromLeft(kToolbarPad);
    viewSwitch_.setBounds(header.removeFromLeft(kSwitchWidth));
    header.removeFromLeft(kToolbarPad);
    playClipButton_.setBounds(header.removeFromLeft(kPlayClipSlotWidth));
    header.removeFromLeft(kToolbarPad);
    showAllButton_.setBounds(header.removeFromLeft(kShowAllWidth));
    header.removeFromLeft(kToolbarPad);
    stepsCaptionBounds_ = header.removeFromLeft(kStepsCaptionWidth);
    stepsCombo_.setBounds(header.removeFromLeft(kStepsComboWidth));
    header.removeFromLeft(kToolbarPad);
    swingCaptionBounds_ = header.removeFromLeft(kSwingCaptionWidth);
    swingKnob_.setBounds(header.removeFromLeft(kSwingKnobSize).withSizeKeepingCentre(kSwingKnobSize, kSwingKnobSize));
    header.removeFromLeft(kToolbarPad);
    swingValueBounds_ = header.removeFromLeft(kSwingValueWidth);
    header.removeFromLeft(kToolbarPad);
    humaniseCaptionBounds_ = header.removeFromLeft(kHumaniseCaptionWidth);
    humaniseKnob_.setBounds(
        header.removeFromLeft(kSwingKnobSize).withSizeKeepingCentre(kSwingKnobSize, kSwingKnobSize));
    header.removeFromLeft(kToolbarPad);
    humaniseValueBounds_ = header.removeFromLeft(kSwingValueWidth);
    header.removeFromLeft(kToolbarPad);
    ideasButton_.setBounds(header.removeFromLeft(kIdeasWidth));
}

void DrumMachineComponent::resized() {
    layoutToolbar();
    layoutBottom();
    layoutRows();
}

void DrumMachineComponent::paintToolbar(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    g.setColour(theme.colors.surface);
    g.fillRect(0, 0, getWidth(), kToolbarHeight);
    g.setColour(theme.colors.border);
    g.drawHorizontalLine(kToolbarHeight - 1, 0.0f, static_cast<float>(getWidth()));
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), theme.type.value + 1.0f, juce::Font::plain));
    const auto* c = clip();
    const auto dot = juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
    g.setColour(theme.colors.textPrimary);
    g.drawText("Drums" + dot + (c != nullptr ? c->name : juce::String()), nameBounds_, juce::Justification::centredLeft,
               true);
    g.setColour(theme.colors.textMuted);
    g.drawText("Steps per bar", stepsCaptionBounds_, juce::Justification::centredRight, true);
    g.drawText("Swing", swingCaptionBounds_, juce::Justification::centredRight, true);
    g.drawText("Humanise", humaniseCaptionBounds_, juce::Justification::centredRight, true);
    g.setColour(theme.colors.textPrimary);
    g.drawText(juce::String(juce::roundToInt(swingKnob_.getValue())) + "%", swingValueBounds_,
               juce::Justification::centredLeft, true);
    g.drawText(juce::String(juce::roundToInt(humaniseKnob_.getValue())) + "%", humaniseValueBounds_,
               juce::Justification::centredLeft, true);
}

// "for Kick" beside the lane chips: the row the lanes edit.
void DrumMachineComponent::paintLaneCaption(juce::Graphics& g) {
    if (forBounds_.isEmpty())
        return;
    const auto& theme = synth::theme::themeOf(*this);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), theme.type.value + 1.0f, juce::Font::plain));
    g.setColour(theme.colors.textMuted);
    g.drawText("for " + drumLaneForPitch(selectedPitch_).name, forBounds_, juce::Justification::centredLeft, true);
}

//==============================================================================
void DrumMachineComponent::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    g.fillAll(theme.colors.bg1);
    paintToolbar(g);
    paintLaneCaption(g);
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(gridRegion());
        paintCells(g);
        paintPlayhead(g);
    }
}

// A dense grid (a step under a few pixels wide) draws plain rectangles: rounded corners and insets would eat the cell.
void DrumMachineComponent::paintCells(juce::Graphics& g) {
    const auto* c = clip();
    if (c == nullptr)
        return;
    const auto clipBounds = g.getClipBounds();
    const double stepLen = getStepBeats();
    const int steps = getStepCount();
    const int first = std::max(0, static_cast<int>(std::floor((xToBeat(clipBounds.getX()) - c->startBeat) / stepLen)));
    const int last =
        std::min(steps - 1, static_cast<int>(std::floor((xToBeat(clipBounds.getRight()) - c->startBeat) / stepLen)));
    const bool dense = stepLen * view_.pixelsPerBeat < kDenseStepPx;
    for (int row = 0; row < static_cast<int>(slots_.size()); ++row) {
        const int top = rowTop(row);
        const int height = rowHeightOf(slots_[(size_t)row]);
        if (height <= 0 || top + height < clipBounds.getY() || top > clipBounds.getBottom())
            continue;
        for (int step = first; step <= last; ++step)
            paintCell(g, slots_[(size_t)row], row, step, cellBounds(row, step), dense);
    }
}

// What the pieces of one cell's painting share.
struct DrumMachineComponent::CellPaint {
    drumgrid::CellKey key = 0;
    int pitch = 0;
    juce::Rectangle<float> area;
    float radius = 0.0f;
    float rowAlpha = 1.0f;
    bool dense = false;
};

void DrumMachineComponent::paintCell(juce::Graphics& g, const LaneSlot& slot, int row, int step,
                                     juce::Rectangle<int> rect, bool dense) {
    if (rect.isEmpty())
        return;
    const auto& theme = synth::theme::themeOf(*this);
    const int pitch = slot.lane.pitch;
    CellPaint cell;
    cell.key = drumgrid::cellKey(pitch, step);
    cell.pitch = pitch;
    cell.area = rect.toFloat();
    cell.radius = dense ? 0.0f : theme.metrics.cornerRadiusSmall;
    cell.rowAlpha = slot.reveal * (rowSilenced(pitch) ? kSilencedAlpha : 1.0f);
    cell.dense = dense;
    paintCellBase(g, cell, step);

    // A leaving hit is drawn from the velocity its motion remembers, since the cell no longer holds it.
    int velocity = getCellVelocity(row, step);
    const auto motion = hitMotions_.find(cell.key);
    if (velocity == 0 && motion != hitMotions_.end() && !motion->second.growing)
        velocity = motion->second.velocity;
    if (velocity > 0) {
        paintCellHit(g, slot, cell, velocity);
    } else {
        g.setColour(theme.colors.border.withMultipliedAlpha(cell.rowAlpha * kRestingAlpha));
        if (!dense)
            g.drawRoundedRectangle(cell.area.reduced(0.5f), cell.radius, 1.0f);
    }
    // A cell an idea just changed keeps an orange outline that fades out.
    if (const float amount = changedAmount(cell.key); amount > 0.0f) {
        g.setColour(juce::Colour(kChangedOutlineArgb).withAlpha(amount));
        g.drawRoundedRectangle(cell.area.reduced(0.75f), cell.radius, 1.5f);
    }
}

// The empty cell: its surface, the shaded beat group and the playing column's tint.
void DrumMachineComponent::paintCellBase(juce::Graphics& g, const CellPaint& cell, int step) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    // Every other beat group is shaded like the step sequencer's every other group of four, so the pulse stays
    // readable; steps per beat is a whole number for every choice.
    const double stepsPerBeat = static_cast<double>(stepsPerBar()) / std::max(1.0, beatsPerBar());
    const int group = static_cast<int>(std::floor(static_cast<double>(step) / std::max(1.0, stepsPerBeat)));
    g.setColour(colors.surface.withMultipliedAlpha(cell.rowAlpha));
    g.fillRoundedRectangle(cell.area, cell.radius);
    if (group % 2 == 1) {
        g.setColour(colors.textPrimary.withAlpha(kBeatGroupShadeAlpha * cell.rowAlpha));
        g.fillRoundedRectangle(cell.area, cell.radius);
    }
    if (showsPlayhead() && step == playStep_) {
        g.setColour(colors.accent.withAlpha(kPlayColumnAlpha * cell.rowAlpha));
        g.fillRoundedRectangle(cell.area, cell.radius);
    }
}

// A hit: the fill to its velocity (dimmer when it only plays some of the time), the accent tick, its marks and the
// flash, all in the hit's grow-in or shrink-out motion.
void DrumMachineComponent::paintCellHit(juce::Graphics& g, const LaneSlot& slot, const CellPaint& cell, int velocity) {
    const auto& area = cell.area;
    float motionAlpha = 1.0f;
    const float scale = hitScaleAt(cell.key, motionAlpha);
    const auto found = cells_.find(cell.key);
    const bool present = found != cells_.end();
    const bool noteMuted = present && found->second.muted;
    const auto marks =
        present ? describeCellMarks(found->second, swing_, static_cast<int>(cell.key % 1000000)) : DrumCellMarks{};
    const float alpha = cell.rowAlpha * motionAlpha * (noteMuted ? kMutedHitAlpha : 1.0f);
    const float fillHeight = std::max(1.0f, area.getHeight() * static_cast<float>(velocity) / 127.0f);
    const auto fill = area.withTop(area.getBottom() - fillHeight);
    const auto centre = area.getCentre();
    const auto scaled = juce::AffineTransform::scale(scale, scale, centre.x, centre.y);
    juce::Path body;
    body.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), cell.radius);
    body.applyTransform(scaled);
    juce::Path fillPath;
    fillPath.addRoundedRectangle(fill.getX(), fill.getY(), fill.getWidth(), fill.getHeight(), cell.radius);
    fillPath.applyTransform(scaled);
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(body.getBounds().getSmallestIntegerContainer());
        g.setColour(slot.lane.colour.withMultipliedAlpha(alpha * (marks.dimmed ? kChanceFillAlpha : 1.0f)));
        g.fillPath(fillPath);
        if (drumgrid::isAccent(velocity) && !cell.dense) {
            const auto tick = juce::Rectangle<float>(area.getX() + 2.0f, area.getY() + 1.0f, area.getWidth() - 4.0f,
                                                     kAccentTickHeight);
            g.setColour(juce::Colours::white.withMultipliedAlpha(alpha));
            g.fillRect(tick.transformedBy(scaled).getIntersection(area));
        }
    }
    {
        // The marks move with the hit and may sit just outside its body (an early tick), so they are not clipped to it.
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(scaled);
        paintCellMarks(g, area, marks, alpha, cell.dense);
    }
    const float flash = flashAmount(cell.key);
    if (flash > 0.0f) {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(body.getBounds().getSmallestIntegerContainer());
        g.setColour(juce::Colours::white.withAlpha(kFlashAlpha * flash * alpha));
        g.fillPath(fillPath);
    }
}

//==============================================================================
void DrumMachineComponent::paintPlayhead(juce::Graphics& g) {
    if (!hasPlayheadX_)
        return;
    const auto grid = gridRegion();
    if (playheadLineX_ < grid.getX() || playheadLineX_ > grid.getRight())
        return;
    g.setColour(synth::theme::themeOf(*this).colors.accent);
    g.fillRect(static_cast<float>(playheadLineX_) - kPlayheadLineWidth * 0.5f, static_cast<float>(grid.getY()),
               kPlayheadLineWidth, static_cast<float>(grid.getHeight()));
}

void DrumMachineComponent::paintOverChildren(juce::Graphics& g) {
    if (!hasKeyboardFocus(false) || !clipId_.isValid() || slots_.empty())
        return;
    const auto rect = cellBounds(focus_.row, focus_.step);
    if (rect.isEmpty() || !gridRegion().intersects(rect))
        return;
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(gridRegion());
    paintFocusRingAlways(g, rect.toFloat().expanded(1.5f), *this,
                         synth::theme::themeOf(*this).metrics.cornerRadiusSmall);
}

//==============================================================================
void DrumMachineComponent::setTransportPlaying(bool playing) {
    if (playing)
        stopClipPreview(); // the song plays alone
    if (playing_ == playing)
        return;
    playing_ = playing;
    if (playStep_ >= 0)
        requestRepaintStrip(columnStrip(playStep_));
}

void DrumMachineComponent::setPlayheadBeat(double absoluteBeat) {
    followPreviewBeat(absoluteBeat);
    if (!std::isfinite(absoluteBeat))
        return;
    playheadBeat_ = absoluteBeat;
    const auto* c = clip();

    // The playing column: the step under the playhead, none outside the clip.
    int step = -1;
    if (c != nullptr) {
        const double rel = absoluteBeat - c->startBeat;
        const int candidate = rel < 0.0 ? -1 : drumgrid::cellOf(rel, getStepBeats());
        step = candidate >= 0 && candidate < getStepCount() ? candidate : -1;
    }
    if (step != playStep_) {
        const int previous = playStep_;
        playStep_ = step;
        if (showsPlayhead()) {
            if (previous >= 0)
                requestRepaintStrip(columnStrip(previous));
            if (step >= 0) {
                requestRepaintStrip(columnStrip(step));
                flashSkippedSteps(previous, step);
            }
        }
    }
    if (c != nullptr)
        flushSwungFlashes(absoluteBeat - c->startBeat);

    const int x =
        static_cast<int>(std::llround(std::clamp(beatToX(absoluteBeat), -kMaxLineXMagnitude, kMaxLineXMagnitude)));
    const int previousX = hasPlayheadX_ ? playheadLineX_ : x;
    if (hasPlayheadX_ && x == playheadLineX_)
        return;
    hasPlayheadX_ = true;
    playheadLineX_ = x;
    const auto strip = [this](int lineX) {
        return juce::Rectangle<int>(lineX - kPlayheadStripHalfWidth, canvasTop(), 2 * kPlayheadStripHalfWidth + 1,
                                    gridRegion().getHeight());
    };
    const auto dirty = strip(previousX).getUnion(strip(x)).getIntersection(gridRegion());
    if (!dirty.isEmpty())
        requestRepaintStrip(dirty);
}

} // namespace synth::ui
