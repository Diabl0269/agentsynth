// The pick-on-canvas layer: hit-testing module cards by screen bounds, the hover outline, and handing wheel
// scrolling on to the canvas underneath so the user can reach a module that is out of view.

#include "ModDotCanvasPicker.h"

#include "ModDotPalette.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth::ui {

ModDotCanvasPicker::ModDotCanvasPicker(GraphEditor& editor,
                                       std::function<bool(juce::AudioProcessorGraph::NodeID)> isEligible)
    : editor_(editor)
    , isEligible_(std::move(isEligible)) {
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true); // a press here takes the keys from the panel's window, so Esc still stops the mode
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    setTitle("Pick a source module");
    setComponentID("modDotCanvasPicker");
}

ModDotCanvasPicker::~ModDotCanvasPicker() = default;

void ModDotCanvasPicker::begin() {
    editor_.addAndMakeVisible(*this);
    setBounds(editor_.getLocalBounds());
    toFront(false);
}

void ModDotCanvasPicker::parentSizeChanged() {
    if (auto* parent = getParentComponent())
        setBounds(parent->getLocalBounds());
}

juce::AudioProcessorGraph::NodeID ModDotCanvasPicker::eligibleNodeAt(juce::Point<int> screenPoint) const {
    juce::AudioProcessorGraph::NodeID found;
    for (auto* card : editor_.getModuleComponents())
        if (card != nullptr && card->isVisible() && card->getScreenBounds().contains(screenPoint) && isEligible_ &&
            isEligible_(card->getNodeId()))
            found = card->getNodeId();
    return found;
}

void ModDotCanvasPicker::setHovered(juce::AudioProcessorGraph::NodeID node) {
    if (node == hovered_)
        return;
    hovered_ = node;
    repaint();
}

void ModDotCanvasPicker::mouseMove(const juce::MouseEvent& e) { setHovered(eligibleNodeAt(e.getScreenPosition())); }
void ModDotCanvasPicker::mouseExit(const juce::MouseEvent&) { setHovered({}); }

void ModDotCanvasPicker::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (!e.mods.isLeftButtonDown())
        return;
    const auto node = eligibleNodeAt(e.getScreenPosition());
    if (node.uid != 0 && onPicked)
        onPicked(node);
}

void ModDotCanvasPicker::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (auto* parent = getParentComponent())
        parent->mouseWheelMove(e.getEventRelativeTo(parent), wheel);
}

bool ModDotCanvasPicker::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (onEscape)
        onEscape();
    return true;
}

void ModDotCanvasPicker::paint(juce::Graphics& g) {
    if (hovered_.uid == 0)
        return;
    const auto p = modDotPaletteFor(*this);
    for (auto* card : editor_.getModuleComponents()) {
        if (card == nullptr || card->getNodeId() != hovered_)
            continue;
        const auto bounds = getLocalArea(nullptr, card->getScreenBounds()).toFloat();
        g.setColour(p.accent.withAlpha(0.14f));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(p.accent);
        g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f, 2.0f);
    }
}

} // namespace synth::ui
