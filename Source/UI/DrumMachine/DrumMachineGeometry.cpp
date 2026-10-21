// DrumMachineComponent: where things are. The beat-to-x mapping is the grid's own (view_), the rows scroll
// vertically by scrollY_, and a cell is the rectangle of one step in one row.

#include "DrumMachineComponent.h"

#include "UI/Timeline/ScrollPolicy.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr double kZoomWheelSensitivity = 2.0;
constexpr double kScrollPixelsPerWheelUnit = 200.0;
constexpr double kRowScrollPixelsPerWheelUnit = 80.0;
constexpr int kCellInset = 2;
} // namespace

void DrumMachineComponent::setRulerBandHeight(int heightPx) {
    rulerBandHeight_ = std::max(0, heightPx);
    layoutRows();
    layoutToolbar();
}

juce::Rectangle<int> DrumMachineComponent::gridRegion() const noexcept {
    return {kLabelColumnWidth, canvasTop(), std::max(0, getWidth() - kLabelColumnWidth),
            std::max(0, getHeight() - canvasTop())};
}

double DrumMachineComponent::beatToX(double absBeat) const noexcept {
    return static_cast<double>(kLabelColumnWidth) + view_.beatToX(absBeat);
}

double DrumMachineComponent::xToBeat(double x) const noexcept {
    return view_.xToBeat(x - static_cast<double>(kLabelColumnWidth));
}

void DrumMachineComponent::notifyViewChanged() {
    repaint();
    if (onHorizontalViewChanged)
        onHorizontalViewChanged();
}

void DrumMachineComponent::setHorizontalView(double pixelsPerBeat, double firstVisibleBeat) {
    if (!std::isfinite(pixelsPerBeat) || !std::isfinite(firstVisibleBeat))
        return;
    view_.pixelsPerBeat =
        std::clamp(pixelsPerBeat, TimelineViewState::kMinPixelsPerBeat, TimelineViewState::kMaxPixelsPerBeat);
    view_.firstVisibleBeat = std::max(0.0, firstVisibleBeat);
    notifyViewChanged();
}

//==============================================================================
int DrumMachineComponent::rowHeightOf(const LaneSlot& slot) const noexcept {
    // Under Reduce Motion a row fades at its full height; otherwise its height follows the reveal.
    const float factor = prefersReducedMotion() ? (slot.reveal > 0.0f ? 1.0f : 0.0f) : slot.reveal;
    return static_cast<int>(std::lround(static_cast<double>(kRowHeight) * static_cast<double>(factor)));
}

int DrumMachineComponent::slotIndexForRow(int row) const noexcept {
    return row >= 0 && row < static_cast<int>(slots_.size()) ? row : -1;
}

int DrumMachineComponent::rowTop(int slotIndex) const noexcept {
    return slotIndex >= 0 && slotIndex < static_cast<int>(rowTops_.size()) ? rowTops_[(size_t)slotIndex] : canvasTop();
}

double DrumMachineComponent::maxScrollY() const noexcept {
    int total = 0;
    for (const auto& slot : slots_)
        total += rowHeightOf(slot);
    return std::max(0.0, static_cast<double>(total - std::max(0, getHeight() - canvasTop())));
}

bool DrumMachineComponent::setScrollY(double y) {
    const double clamped = juce::jlimit(0.0, maxScrollY(), y);
    if (clamped == scrollY_)
        return false;
    scrollY_ = clamped;
    layoutRows();
    repaint();
    return true;
}

int DrumMachineComponent::stepAtX(double x) const noexcept {
    const auto* c = clip();
    if (c == nullptr)
        return -1;
    const double stepLen = getStepBeats();
    const double rel = xToBeat(x) - c->startBeat;
    if (rel < 0.0)
        return -1;
    const int step = drumgrid::cellOf(rel, stepLen);
    return step < getStepCount() ? step : -1;
}

juce::Rectangle<int> DrumMachineComponent::cellBounds(int row, int step) const {
    const auto* c = clip();
    if (c == nullptr || slotIndexForRow(row) < 0 || step < 0 || step >= getStepCount())
        return {};
    const double stepLen = getStepBeats();
    const int x0 = static_cast<int>(std::lround(beatToX(c->startBeat + step * stepLen)));
    const int x1 = static_cast<int>(std::lround(beatToX(c->startBeat + (step + 1) * stepLen)));
    const int height = rowHeightOf(slots_[(size_t)row]);
    const int inset = std::min(kCellInset, std::max(0, (x1 - x0 - 1) / 2));
    const int vInset = std::min(kCellInset, height / 4);
    return {x0 + inset, rowTop(row) + vInset, std::max(1, x1 - x0 - 2 * inset), std::max(0, height - 2 * vInset)};
}

std::optional<DrumMachineComponent::Cell> DrumMachineComponent::cellAt(juce::Point<int> pos) const {
    if (!gridRegion().contains(pos))
        return std::nullopt;
    const int step = stepAtX(pos.x);
    if (step < 0)
        return std::nullopt;
    for (int row = 0; row < static_cast<int>(slots_.size()); ++row) {
        const int top = rowTop(row);
        if (pos.y >= top && pos.y < top + rowHeightOf(slots_[(size_t)row]))
            return Cell{row, step};
    }
    return std::nullopt;
}

juce::Rectangle<int> DrumMachineComponent::columnStrip(int step) const {
    const auto* c = clip();
    if (c == nullptr || step < 0)
        return {};
    const double stepLen = getStepBeats();
    const int x0 = static_cast<int>(std::floor(beatToX(c->startBeat + step * stepLen)));
    const int x1 = static_cast<int>(std::ceil(beatToX(c->startBeat + (step + 1) * stepLen)));
    return juce::Rectangle<int>(x0, canvasTop(), std::max(1, x1 - x0), gridRegion().getHeight())
        .getIntersection(gridRegion());
}

bool DrumMachineComponent::rowSilenced(int pitch) const {
    const auto* c = clip();
    if (c == nullptr)
        return false;
    if (std::binary_search(c->mutedPitches.begin(), c->mutedPitches.end(), pitch))
        return true;
    return !c->soloPitches.empty() && !std::binary_search(c->soloPitches.begin(), c->soloPitches.end(), pitch);
}

// Scrolls just far enough that the cell is whole inside the grid, on both axes.
void DrumMachineComponent::ensureCellVisible(Cell cell) {
    const auto* c = clip();
    if (c == nullptr || slotIndexForRow(cell.row) < 0)
        return;
    const auto grid = gridRegion();
    const double stepLen = getStepBeats();
    const double startBeat = c->startBeat + cell.step * stepLen;
    const double endBeat = startBeat + stepLen;
    const double visibleBeats = static_cast<double>(grid.getWidth()) / view_.pixelsPerBeat;
    if (startBeat < view_.firstVisibleBeat)
        setHorizontalView(view_.pixelsPerBeat, startBeat);
    else if (endBeat > view_.firstVisibleBeat + visibleBeats)
        setHorizontalView(view_.pixelsPerBeat, std::max(0.0, endBeat - visibleBeats));

    const int top = rowTop(cell.row);
    const int height = rowHeightOf(slots_[(size_t)cell.row]);
    if (top < grid.getY())
        setScrollY(scrollY_ - (grid.getY() - top));
    else if (top + height > grid.getBottom())
        setScrollY(scrollY_ + (top + height - grid.getBottom()));
}

//==============================================================================
void DrumMachineComponent::zoomHorizontalAroundX(double factor, double anchorGridX) {
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    view_.zoomAroundX(factor, anchorGridX);
    notifyViewChanged();
}

void DrumMachineComponent::zoomHorizontal(double factor) {
    zoomHorizontalAroundX(factor, static_cast<double>(gridRegion().getWidth()) * 0.5);
}

// The rows have one fixed height, so there is nothing to zoom vertically.
void DrumMachineComponent::zoomVertical(double factor) { juce::ignoreUnused(factor); }

void DrumMachineComponent::mouseMagnify(const juce::MouseEvent& e, float scaleFactor) {
    if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
        return;
    zoomHorizontalAroundX(static_cast<double>(scaleFactor),
                          std::max(0.0, static_cast<double>(e.getPosition().x - kLabelColumnWidth)));
}

// Cmd+wheel zooms around the pointer; Shift+wheel (or a sideways trackpad swipe) scrolls the beats; a plain wheel
// scrolls the rows. A mouse-wheel notch eases (ScrollTweenRunner); a trackpad moves directly.
void DrumMachineComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (e.mods.isCommandDown()) {
        wheelTween_.stop();
        const bool zoomIn = wheelGestureIsUpward(wheel) != zoomScrollInverted_;
        const double magnitude = std::abs(dominantWheelDelta(wheel)) * kZoomWheelSensitivity;
        zoomHorizontalAroundX(std::exp(zoomIn ? magnitude : -magnitude),
                              std::max(0.0, static_cast<double>(e.getPosition().x - kLabelColumnWidth)));
        return;
    }
    const bool eased = !wheel.isSmooth && !wheel.isInertial;
    const bool horizontal = e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    const auto read = [this](int axis) { return axis == 0 ? view_.firstVisibleBeat : scrollY_; };
    const auto scroll = [this](int axis, double delta) {
        if (axis == 0) {
            view_.scrollBeats(delta);
            notifyViewChanged();
        } else {
            setScrollY(scrollY_ + delta);
        }
    };
    int axis = 1;
    double amount = 0.0;
    if (horizontal) {
        const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        axis = 0;
        amount =
            static_cast<double>(scrollAmount(delta, scrollInverted_)) * kScrollPixelsPerWheelUnit / view_.pixelsPerBeat;
    } else if (wheel.deltaY != 0.0f) {
        amount = static_cast<double>(scrollAmount(dominantWheelDelta(wheel), scrollInverted_)) *
                 kRowScrollPixelsPerWheelUnit;
    }
    if (amount == 0.0)
        return;
    if (eased && wheelTween_.push(*this, axis, amount, read, scroll))
        return;
    wheelTween_.stop();
    scroll(axis, amount);
}

} // namespace synth::ui
