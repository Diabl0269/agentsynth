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
constexpr int kToolbarPad = 4;
constexpr float kRestingAlpha = 0.55f;
constexpr float kSilencedAlpha = 0.35f;
constexpr float kMutedHitAlpha = 0.4f;
constexpr float kPlayColumnAlpha = 0.16f;
constexpr float kFlashAlpha = 0.55f;
constexpr float kAccentTickHeight = 2.0f;
constexpr int kDenseStepPx = 3;
constexpr float kPlayheadLineWidth = TimelinePlayheadOverlay::kLineWidth;
constexpr int kPlayheadStripHalfWidth = TimelinePlayheadOverlay::kStripHalfWidth;
constexpr double kMaxLineXMagnitude = 1.0e6;
} // namespace

static_assert(DrumMachineComponent::kToolbarHeight == 20, "the toolbar matches the piano roll's header height");

//==============================================================================
void DrumMachineComponent::layoutToolbar() {
    auto header = getLocalBounds().removeFromTop(kToolbarHeight).reduced(kToolbarPad, 1);
    const int nameWidth = std::max(0, header.getWidth() - kSwitchWidth - kPlayClipSlotWidth - kShowAllWidth -
                                          kStepsComboWidth - kStepsCaptionWidth - 4 * kToolbarPad);
    nameBounds_ = header.removeFromLeft(std::min(nameWidth, 220));
    header.removeFromLeft(kToolbarPad);
    viewSwitch_.setBounds(header.removeFromLeft(kSwitchWidth));
    // The slot after the switch is kept free for a "Play clip" control to come.
    header.removeFromLeft(kPlayClipSlotWidth + kToolbarPad);
    showAllButton_.setBounds(header.removeFromLeft(kShowAllWidth));
    header.removeFromLeft(kToolbarPad);
    stepsCaptionBounds_ = header.removeFromLeft(kStepsCaptionWidth);
    stepsCombo_.setBounds(header.removeFromLeft(kStepsComboWidth));
}

void DrumMachineComponent::resized() {
    layoutToolbar();
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
}

//==============================================================================
void DrumMachineComponent::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    g.fillAll(theme.colors.bg1);
    paintToolbar(g);
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

void DrumMachineComponent::paintCell(juce::Graphics& g, const LaneSlot& slot, int row, int step,
                                     juce::Rectangle<int> rect, bool dense) {
    if (rect.isEmpty())
        return;
    const auto& theme = synth::theme::themeOf(*this);
    const auto& colors = theme.colors;
    const int pitch = slot.lane.pitch;
    const auto key = drumgrid::cellKey(pitch, step);
    const float rowAlpha = slot.reveal * (rowSilenced(pitch) ? kSilencedAlpha : 1.0f);
    const float radius = dense ? 0.0f : theme.metrics.cornerRadiusSmall;
    const auto area = rect.toFloat();

    // Alternating beat groups keep the pulse readable; steps per beat is a whole number for every choice.
    const double stepsPerBeat = static_cast<double>(stepsPerBar()) / std::max(1.0, beatsPerBar());
    const int group = static_cast<int>(std::floor(static_cast<double>(step) / std::max(1.0, stepsPerBeat)));
    g.setColour((group % 2 == 0 ? colors.surface : colors.surfaceHi).withMultipliedAlpha(rowAlpha));
    g.fillRoundedRectangle(area, radius);

    if (playing_ && step == playStep_) {
        g.setColour(colors.accent.withAlpha(kPlayColumnAlpha * rowAlpha));
        g.fillRoundedRectangle(area, radius);
    }

    // A leaving hit is drawn from the velocity its motion remembers, since the cell no longer holds it.
    int velocity = getCellVelocity(row, step);
    const auto motion = hitMotions_.find(key);
    if (velocity == 0 && motion != hitMotions_.end() && !motion->second.growing)
        velocity = motion->second.velocity;
    if (velocity > 0) {
        float motionAlpha = 1.0f;
        const float scale = hitScaleAt(key, motionAlpha);
        const auto cell = cells_.find(key);
        const bool noteMuted = cell != cells_.end() && cell->second.muted;
        const float alpha = rowAlpha * motionAlpha * (noteMuted ? kMutedHitAlpha : 1.0f);
        const float fillHeight = std::max(1.0f, area.getHeight() * static_cast<float>(velocity) / 127.0f);
        auto fill = area.withTop(area.getBottom() - fillHeight);
        const auto centre = area.getCentre();
        const auto scaled = juce::AffineTransform::scale(scale, scale, centre.x, centre.y);
        juce::Path body;
        body.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), radius);
        body.applyTransform(scaled);
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(body.getBounds().getSmallestIntegerContainer());
        g.setColour(slot.lane.colour.withMultipliedAlpha(alpha));
        juce::Path fillPath;
        fillPath.addRoundedRectangle(fill.getX(), fill.getY(), fill.getWidth(), fill.getHeight(), radius);
        fillPath.applyTransform(scaled);
        g.fillPath(fillPath);
        if (drumgrid::isAccent(velocity) && !dense) {
            const auto tick = juce::Rectangle<float>(area.getX() + 2.0f, area.getY() + 1.0f, area.getWidth() - 4.0f,
                                                     kAccentTickHeight);
            g.setColour(juce::Colours::white.withMultipliedAlpha(alpha));
            g.fillRect(tick.transformedBy(scaled).getIntersection(area));
        }
        const float flash = flashAmount(key);
        if (flash > 0.0f) {
            g.setColour(juce::Colours::white.withAlpha(kFlashAlpha * flash * alpha));
            g.fillPath(fillPath);
        }
    } else {
        g.setColour(colors.border.withMultipliedAlpha(rowAlpha * kRestingAlpha));
        if (!dense)
            g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);
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
    if (playing_ == playing)
        return;
    playing_ = playing;
    if (playStep_ >= 0)
        requestRepaintStrip(columnStrip(playStep_));
}

void DrumMachineComponent::setPlayheadBeat(double absoluteBeat) {
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
        if (playing_) {
            if (previous >= 0)
                requestRepaintStrip(columnStrip(previous));
            if (step >= 0) {
                requestRepaintStrip(columnStrip(step));
                for (const auto& slot : slots_) {
                    const auto key = drumgrid::cellKey(slot.lane.pitch, step);
                    if (cells_.count(key) > 0 && !rowSilenced(slot.lane.pitch))
                        beginFlash(key);
                }
            }
        }
    }

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
