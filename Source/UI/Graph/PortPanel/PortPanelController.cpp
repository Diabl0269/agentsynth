// PortPanelController.cpp -- click policy, opening and closing the port connections panel, and the cable highlight.
// docs/layout/cables.md#port-connections-panel.

#include "PortPanelController.h"

#include "AudioEngine/AudioEngine.h"
#include "PortConnectionsPanel.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModDot/ModDotPanelFrame.h"
#include "UI/Graph/ModDot/ModDotPanelLaunch.h"

namespace synth::ui {

PortPanelController::PortPanelController(GraphEditor& editor)
    : editor_(editor) {}

PortPanelController::~PortPanelController() {
    pending_.stopTimer();
    if (auto* open = getPanel()) {
        open->orphan(); // its window is deleted a turn later, by when this is gone
        open->dismiss();
    }
}

ModuleComponent* PortPanelController::cardFor(juce::AudioProcessorGraph::NodeID node) const {
    for (auto* card : editor_.getModuleComponents())
        if (card != nullptr && card->getNodeId() == node)
            return card;
    return nullptr;
}

PortRef PortPanelController::refFor(const ModuleComponent& card, const ModuleComponent::Port& port) const {
    return {card.getNodeId(), port.index, port.isInput, port.isMidi};
}

// ---- the click ----

void PortPanelController::jackReleased(ModuleComponent& card, const ModuleComponent::Port& port,
                                       const juce::MouseEvent& e) {
    const bool plainLeft = !e.mods.isPopupMenu() && !e.mods.isMiddleButtonDown() && !e.mods.isCommandDown() &&
                           !e.mods.isShiftDown() && !e.mods.isAltDown();
    if (!plainLeft || e.getNumberOfClicks() != 1 || e.getDistanceFromDragStart() >= (int)kClickThresholdPx)
        return;
    cancelPendingOpen();
    const auto ref = refFor(card, port);
    if (isOpenFor(ref)) {
        close();
        return;
    }
    // One cable and a double-click that disconnects it: wait out the double-click before showing anything.
    if (editor_.getDoubleClickPortDisconnectEnabled() && listPortConnections(editor_, ref).size() == 1) {
        pending_.action = [this, safe = juce::Component::SafePointer<ModuleComponent>(&card), port] {
            if (safe != nullptr)
                open(*safe, port);
        };
        pending_.startTimer(juce::MouseEvent::getDoubleClickTimeout());
        return;
    }
    open(card, port);
}

bool PortPanelController::keepPanelForDoubleClick(ModuleComponent& card, const ModuleComponent::Port& port) {
    cancelPendingOpen();
    const auto ref = refFor(card, port);
    if (listPortConnections(editor_, ref).size() < 2) {
        close();
        return false;
    }
    if (!isOpenFor(ref))
        open(card, port);
    return true;
}

void PortPanelController::cancelPendingOpen() {
    pending_.stopTimer();
    pending_.action = nullptr;
}

// ---- the panel ----

PortConnectionsPanel* PortPanelController::getPanel() const {
    return dismissed_ ? nullptr : static_cast<PortConnectionsPanel*>(panel_.getComponent());
}

bool PortPanelController::isOpenFor(const PortRef& port) const {
    auto* open = getPanel();
    return open != nullptr && open->port() == port;
}

void PortPanelController::open(ModuleComponent& card, const ModuleComponent::Port& port) {
    cancelPendingOpen();
    // A jack clicked again while a panel is still fading out: the fade is cut short, the new panel takes the spot.
    if (auto* old = dynamic_cast<ModDotPanelFrame*>(frame_.getComponent()); old != nullptr && old->isClosing())
        old->finishClosingNow();
    frame_ = nullptr;
    close();
    launch(card, port, std::make_unique<PortConnectionsPanel>(editor_, *this, refFor(card, port)));
}

void PortPanelController::launch(ModuleComponent& card, const ModuleComponent::Port& port,
                                 std::unique_ptr<PortConnectionsPanel> panel) {
    auto* raw = panel.get();
    panel_ = raw;
    dismissed_ = false;
    const auto jack = card.localAreaToGlobal(port.area);
    if (panelLauncher) {
        raw->onDismiss = [this] {
            dismissed_ = true;
            clearHighlight();
        };
        panelLauncher(std::move(panel), card, jack);
        return;
    }
    ModDotPanelLaunch options;
    options.appProperties = editor_.getModDot().host.appProperties;
    options.anchorIsRegion = true;
    options.setMaxHeight = [safe = juce::Component::SafePointer<PortConnectionsPanel>(raw)](int height) {
        if (safe != nullptr)
            safe->setMaxHeight(height);
    };
    options.bindDismiss =
        [this, safe = juce::Component::SafePointer<PortConnectionsPanel>(raw)](std::function<void()> closeFrame) {
            if (safe != nullptr)
                safe->onDismiss = [this, closeFrame = std::move(closeFrame)] {
                    dismissed_ = true;
                    clearHighlight();
                    closeFrame();
                };
        };
    frame_ = launchInFrame(std::move(panel), card, jack, options);
}

void PortPanelController::close() {
    if (auto* open = getPanel())
        open->dismiss();
    clearHighlight();
}

void PortPanelController::tick() {
    if (auto* open = getPanel())
        open->sync();
}

void PortPanelController::panelClosed(PortConnectionsPanel* panel) {
    if (panel_.getComponent() == static_cast<juce::Component*>(panel)) {
        panel_ = nullptr;
        clearHighlight();
    }
}

void PortPanelController::disconnectAll(const PortRef& port) {
    if (auto* card = cardFor(port.node))
        editor_.disconnectPort(card, port.jack, port.isInput, port.isMidi);
}

// ---- cable highlight ----

void PortPanelController::setHighlightedCable(const GraphEditor::CableId& id) {
    if (highlighted_ == id)
        return;
    highlighted_ = id;
    editor_.notifyModuleContentChanged(); // repaints the canvas: the cables are drawn there
}

void PortPanelController::clearHighlightIf(const GraphEditor::CableId& id) {
    if (highlighted_ == id)
        clearHighlight();
}

void PortPanelController::clearHighlight() {
    if (!highlighted_.has_value())
        return;
    highlighted_.reset();
    editor_.notifyModuleContentChanged();
}

} // namespace synth::ui
