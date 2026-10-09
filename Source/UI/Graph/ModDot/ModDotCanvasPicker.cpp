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

// The jack-picking mode: a layer over the canvas that picks a jack or a knob, not a module.
ModDotCanvasPicker::ModDotCanvasPicker(GraphEditor& editor, std::function<bool(const JackHit&)> isEligible)
    : editor_(editor)
    , isEligibleJack_(std::move(isEligible)) {
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    setTitle("Pick a jack to connect");
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

std::optional<ModDotCanvasPicker::JackHit> ModDotCanvasPicker::eligibleJackAt(juce::Point<int> screenPoint) const {
    std::optional<JackHit> found;
    if (!isEligibleJack_)
        return found;
    for (auto* card : editor_.getModuleComponents()) {
        if (card == nullptr || !card->isVisible() || !card->getScreenBounds().contains(screenPoint))
            continue;
        const auto local = card->getLocalPoint(nullptr, screenPoint);
        std::optional<JackHit> hit;
        if (const auto port = card->getPortForPoint(local))
            hit = JackHit{card->getNodeId(), port->area, port->index, port->isInput, port->isMidi, false};
        else if (const auto knob = card->getModTargetPortForPoint(local))
            hit = JackHit{card->getNodeId(), knob->area, knob->index, true, false, true};
        if (hit.has_value() && isEligibleJack_(*hit))
            found = hit;
    }
    return found;
}

void ModDotCanvasPicker::setHoveredJack(std::optional<JackHit> hit) {
    const auto same = [](const std::optional<JackHit>& a, const std::optional<JackHit>& b) {
        return a.has_value() == b.has_value() &&
               (!a.has_value() || (a->node == b->node && a->index == b->index && a->isInput == b->isInput &&
                                   a->isMidi == b->isMidi && a->knob == b->knob));
    };
    if (same(hit, hoveredJack_))
        return;
    hoveredJack_ = hit;
    repaint();
}

void ModDotCanvasPicker::mouseMove(const juce::MouseEvent& e) {
    if (isEligibleJack_)
        setHoveredJack(eligibleJackAt(e.getScreenPosition()));
    else
        setHovered(eligibleNodeAt(e.getScreenPosition()));
}
void ModDotCanvasPicker::mouseExit(const juce::MouseEvent&) {
    setHovered({});
    setHoveredJack(std::nullopt);
}

void ModDotCanvasPicker::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (!e.mods.isLeftButtonDown())
        return;
    if (isEligibleJack_) {
        if (const auto hit = eligibleJackAt(e.getScreenPosition()); hit.has_value() && onPickedJack)
            onPickedJack(*hit);
        return;
    }
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
    if (hoveredJack_.has_value()) {
        const auto p = modDotPaletteFor(*this);
        for (auto* card : editor_.getModuleComponents()) {
            if (card == nullptr || card->getNodeId() != hoveredJack_->node)
                continue;
            auto area = getLocalArea(card, hoveredJack_->area).toFloat();
            area = hoveredJack_->knob ? area.expanded(2.0f) : area.withSizeKeepingCentre(22.0f, 22.0f);
            g.setColour(p.accent.withAlpha(0.16f));
            g.fillRoundedRectangle(area, hoveredJack_->knob ? 8.0f : 11.0f);
            g.setColour(p.accent);
            g.drawRoundedRectangle(area.reduced(1.0f), hoveredJack_->knob ? 8.0f : 10.0f, 2.0f);
        }
        return;
    }
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
