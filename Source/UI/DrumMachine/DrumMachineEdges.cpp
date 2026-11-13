// DrumMachineComponent: the end edge of a drum's pattern. A drum with its own length draws an orange edge after its
// last step with the step count; the row's end (its own, else the clip's) carries a handle that shows on hover with a
// resize cursor, and dragging it sets the length in steps: previewed live (the ghost repeats follow), committed once on
// release with exactly the previewed length. docs/timeline/drum-machine.md#per-drum-length-and-swing.

#include "DrumMachineComponent.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kMaxHandleHalfWidth = 3;
constexpr float kEdgeLineWidth = 2.0f;
constexpr float kHandleWidth = 4.0f;
constexpr float kHandleHeight = 14.0f;
constexpr float kCountFontHeight = 9.0f;
constexpr int kCountWidth = 30;
} // namespace

juce::Rectangle<int> DrumMachineComponent::edgeHandleBounds(int row) const {
    const auto* c = clip();
    const int pitch = getLanePitch(row);
    if (c == nullptr || pitch < 0)
        return {};
    const int height = rowHeightOf(slots_[(size_t)row]);
    const double stepLen = getStepBeats();
    const int x = static_cast<int>(std::lround(beatToX(c->startBeat + displayedEdge(pitch) * stepLen)));
    const int stepPx = std::max(1, static_cast<int>(std::lround(stepLen * view_.pixelsPerBeat)));
    const int half = juce::jlimit(1, kMaxHandleHalfWidth, stepPx / 3);
    return juce::Rectangle<int>(x - half, rowTop(row), 2 * half + 1, height).getIntersection(gridRegion());
}

int DrumMachineComponent::edgeHandleRowAt(juce::Point<int> pos) const {
    if (!gridRegion().contains(pos))
        return -1;
    for (int row = 0; row < static_cast<int>(slots_.size()); ++row)
        if (edgeHandleBounds(row).contains(pos))
            return row;
    return -1;
}

void DrumMachineComponent::updateEdgeHover(juce::Point<int> pos) {
    const int row = gesture_.kind == Gesture::Kind::EdgeDrag ? gesture_.cell.row
                    : clipId_.isValid()                      ? edgeHandleRowAt(pos)
                                                             : -1;
    if (row == hoverEdgeRow_)
        return;
    hoverEdgeRow_ = row;
    setMouseCursor(row >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    repaint(gridRegion());
}

void DrumMachineComponent::mouseMove(const juce::MouseEvent& e) { updateEdgeHover(e.getPosition()); }

void DrumMachineComponent::mouseExit(const juce::MouseEvent&) {
    if (gesture_.kind == Gesture::Kind::EdgeDrag || hoverEdgeRow_ < 0)
        return;
    hoverEdgeRow_ = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint(gridRegion());
}

// The handle's tooltip while the pointer is on it, naming the two step actions' keys; the grid's own otherwise.
juce::String DrumMachineComponent::getTooltip() {
    if (hoverEdgeRow_ < 0)
        return SettableTooltipClient::getTooltip();
    const auto shorter = shortcutHintFor(shortcuts_, "drumMachineRowShorter",
                                         juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0));
    const auto longer = shortcutHintFor(shortcuts_, "drumMachineRowLonger",
                                        juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0));
    return formatShortcutHint("Drag to change how long this drum's pattern is",
                              shorter.isNotEmpty() && longer.isNotEmpty() ? shorter + " / " + longer : juce::String());
}

//==============================================================================
void DrumMachineComponent::beginEdgeDrag(int row) {
    const int pitch = getLanePitch(row);
    gesture_ = {};
    gesture_.kind = Gesture::Kind::EdgeDrag;
    gesture_.cell = {row, focus_.step};
    gesture_.pitch = pitch;
    gesture_.startLength = getRowLength(pitch);
    gesture_.previewLength = gesture_.startLength;
    focus_.row = row;
    hoverEdgeRow_ = row;
}

// The pointer's nearest step boundary is the new length (at least 1). At or past the clip's end the length clears; a
// clip longer than the model's cap stops at the cap.
void DrumMachineComponent::dragEdgeTo(double x) {
    const auto* c = clip();
    if (c == nullptr)
        return;
    const double steps = (xToBeat(x) - c->startBeat) / getStepBeats();
    const int boundary = static_cast<int>(std::lround(std::clamp(steps, 0.0, 1.0e6)));
    const int length = boundary >= getStepCount() ? 0 : juce::jlimit(1, synth::Clip::kMaxRowLengthSteps, boundary);
    if (length == gesture_.previewLength)
        return;
    gesture_.previewLength = length;
    setRowPreview(gesture_.pitch, length);
}

void DrumMachineComponent::setRowPreview(int pitch, std::optional<int> steps) {
    if (steps)
        rowPreview_[pitch] = *steps;
    else
        rowPreview_.erase(pitch);
    syncRowLengths(true);
}

//==============================================================================
// The edges of the drums that have a length, and the handle under the pointer.
void DrumMachineComponent::paintRowEdges(juce::Graphics& g) {
    const auto* c = clip();
    if (c == nullptr)
        return;
    const auto& colors = synth::theme::themeOf(*this).colors;
    const double stepLen = getStepBeats();
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), kCountFontHeight, juce::Font::plain));
    for (int row = 0; row < static_cast<int>(slots_.size()); ++row) {
        const int pitch = slots_[(size_t)row].lane.pitch;
        const int length = getRowLength(pitch);
        const bool handle = row == hoverEdgeRow_;
        const int height = rowHeightOf(slots_[(size_t)row]);
        if (height < 8 || (length <= 0 && slides_.count(pitch) == 0 && !handle))
            continue;
        const float alpha = slots_[(size_t)row].reveal;
        const float x = static_cast<float>(beatToX(c->startBeat + displayedEdge(pitch) * stepLen));
        const float top = static_cast<float>(rowTop(row));
        if (length > 0 || slides_.count(pitch) > 0) {
            g.setColour(colors.warning.withMultipliedAlpha(alpha));
            g.fillRect(x - kEdgeLineWidth * 0.5f, top + 1.0f, kEdgeLineWidth, static_cast<float>(height) - 2.0f);
        }
        if (length > 0)
            g.drawText(
                juce::String(length),
                juce::Rectangle<float>(x + 3.0f, top, static_cast<float>(kCountWidth), static_cast<float>(height)),
                juce::Justification::centredLeft, false);
        if (handle) {
            const float handleHeight = std::min(kHandleHeight, static_cast<float>(height) - 4.0f);
            g.setColour(colors.warning.withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(x - kHandleWidth * 0.5f, top + (static_cast<float>(height) - handleHeight) * 0.5f,
                                   kHandleWidth, handleHeight, kHandleWidth * 0.5f);
        }
    }
}

} // namespace synth::ui
