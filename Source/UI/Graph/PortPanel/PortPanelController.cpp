// PortPanelController.cpp -- click policy, opening and closing the port connections panel, and the cable highlight.
// docs/layout/cables.md#port-connections-panel.

#include "PortPanelController.h"

#include "AudioEngine/AudioEngine.h"
#include "PortConnectionsPanel.h"
#include "PortConnector.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModDot/ModDotPanelFrame.h"
#include "UI/Graph/ModDot/ModDotPanelLaunch.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"

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
    jackReleased(card, port.area, refFor(card, port), e);
}

void PortPanelController::jackReleased(juce::Component& anchor, juce::Rectangle<int> jackArea, const PortRef& ref,
                                       const juce::MouseEvent& e) {
    const bool plainLeft = !e.mods.isPopupMenu() && !e.mods.isMiddleButtonDown() && !e.mods.isCommandDown() &&
                           !e.mods.isShiftDown() && !e.mods.isAltDown();
    if (!plainLeft || e.getNumberOfClicks() != 1 || e.getDistanceFromDragStart() >= (int)kClickThresholdPx)
        return;
    cancelPendingOpen();
    if (isOpenFor(ref)) {
        close();
        return;
    }
    // One cable and a double-click that disconnects it: wait out the double-click before showing anything.
    if (editor_.getDoubleClickPortDisconnectEnabled() && listPortConnections(editor_, ref).size() == 1) {
        pending_.action = [this, safe = juce::Component::SafePointer<juce::Component>(&anchor), jackArea, ref] {
            if (safe != nullptr && safe->isVisible())
                open(*safe, jackArea, ref);
        };
        pending_.startTimer(juce::MouseEvent::getDoubleClickTimeout());
        return;
    }
    open(anchor, jackArea, ref);
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
    open(card, port.area, refFor(card, port));
}

void PortPanelController::open(juce::Component& anchor, juce::Rectangle<int> jackArea, const PortRef& port) {
    cancelPendingOpen();
    // A jack clicked again while a panel is still fading out: the fade is cut short, the new panel takes the spot.
    if (auto* old = dynamic_cast<ModDotPanelFrame*>(frame_.getComponent()); old != nullptr && old->isClosing())
        old->finishClosingNow();
    frame_ = nullptr;
    close();
    launch(anchor, jackArea, std::make_unique<PortConnectionsPanel>(editor_, *this, port));
}

void PortPanelController::launch(juce::Component& anchor, juce::Rectangle<int> jackArea,
                                 std::unique_ptr<PortConnectionsPanel> panel) {
    auto* raw = panel.get();
    panel_ = raw;
    dismissed_ = false;
    const auto jack = anchor.localAreaToGlobal(jackArea);
    if (panelLauncher) {
        raw->onDismiss = [this] {
            dismissed_ = true;
            clearHighlight();
            clearPreview();
        };
        panelLauncher(std::move(panel), anchor, jack);
        return;
    }
    ModDotPanelLaunch options;
    options.appProperties = editor_.getModDot().host.appProperties;
    options.anchorIsRegion = true;
    options.setMaxHeight = [safe = juce::Component::SafePointer<PortConnectionsPanel>(raw)](int height) {
        if (safe != nullptr)
            safe->setMaxHeight(height);
    };
    options.keepOpenOnOutsideClick = [safe = juce::Component::SafePointer<PortConnectionsPanel>(raw)] {
        return safe != nullptr && safe->isPicking(); // a press on the canvas is the pick, not a click away
    };
    options.bindDismiss =
        [this, safe = juce::Component::SafePointer<PortConnectionsPanel>(raw)](std::function<void()> closeFrame) {
            if (safe != nullptr)
                safe->onDismiss = [this, closeFrame = std::move(closeFrame)] {
                    dismissed_ = true;
                    clearHighlight();
                    clearPreview();
                    closeFrame();
                };
        };
    frame_ = launchInFrame(std::move(panel), anchor, jack, options);
}

void PortPanelController::close() {
    if (auto* open = getPanel())
        open->dismiss();
    clearHighlight();
    clearPreview();
}

void PortPanelController::tick() {
    if (auto* open = getPanel())
        open->sync();
}

void PortPanelController::panelClosed(PortConnectionsPanel* panel) {
    if (panel_.getComponent() == static_cast<juce::Component*>(panel)) {
        panel_ = nullptr;
        clearHighlight();
        clearPreview();
    }
}

void PortPanelController::disconnectAll(const PortRef& port) {
    if (auto* card = cardFor(port.node))
        editor_.disconnectPort(card, port.jack, port.isInput, port.isMidi);
}

// ---- adding a connection ----

bool PortPanelController::connect(const PortRef& from, const PortTarget& target) {
    clearPreview();
    switch (target.kind) {
    case PortTarget::Kind::Jack:
        return PortConnector::connectToJack(editor_, from, target.jack).connected;
    case PortTarget::Kind::Knob:
        return PortConnector::connectToKnob(editor_, from, target.node, target.knobChannel);
    case PortTarget::Kind::NewModule:
        return PortConnector::addModuleAndConnect(editor_, from, target.newType).uid != 0;
    }
    return false;
}

namespace {
// Where a jack (or, with a knob channel, a knob) of `card` is drawn, in canvas coordinates; null when it has none.
std::optional<juce::Point<float>> canvasAnchor(ModuleComponent& card, const PortRef& jack, int knobChannel) {
    const auto origin = card.getBounds().getPosition().toFloat();
    if (knobChannel >= 0) {
        if (const auto knob = card.getModTargetKnobAnchor(knobChannel))
            return origin + *knob;
        return std::nullopt;
    }
    const auto centre =
        jack.isMidi ? card.getMidiPortCenter(!jack.isInput) : card.getPortCenter(jack.jack, jack.isInput);
    return origin + centre.toFloat();
}

// A macro port of a COLLAPSED macro is drawn as a dot on the macro's card; its own (hidden) module card is elsewhere.
std::optional<juce::Point<float>> collapsedMacroPortAnchor(GraphEditor& editor, const PortRef& jack) {
    auto& macros = editor.getMacroController();
    const auto owner = macros.macroPortOwnerFor(jack.node);
    if (owner.macro == nullptr || owner.port == nullptr || !owner.macro->collapsed)
        return std::nullopt;
    auto* macroCard = macros.getMacroCard(owner.macro->id);
    if (macroCard == nullptr || !macroCard->isVisible())
        return std::nullopt;
    for (const auto& dot : macros.macroCardPortLayout(owner.macro->id))
        if (dot.nodeUuid == owner.port->nodeUuid && (dot.visibleJack < 0 || dot.visibleJack == jack.jack) &&
            dot.isInput == jack.isInput)
            return macroCard->getBounds().getPosition().toFloat() + dot.jackPos.toFloat();
    return std::nullopt;
}
} // namespace

void PortPanelController::setPreview(const PortRef& from, const PortTarget* target) {
    const bool had = preview_.has_value();
    preview_.reset();
    auto* fromCard = cardFor(from.node);
    auto* toCard = target != nullptr && !target->isNew() && !target->connected ? cardFor(target->node) : nullptr;
    const auto macroDot = collapsedMacroPortAnchor(editor_, from);
    if ((fromCard != nullptr || macroDot.has_value()) && toCard != nullptr) {
        const auto here = macroDot.has_value() ? macroDot : canvasAnchor(*fromCard, from, -1);
        const auto there =
            canvasAnchor(*toCard, target->jack, target->kind == PortTarget::Kind::Knob ? target->knobChannel : -1);
        if (here.has_value() && there.has_value()) {
            GraphEditor::VisibleCable look;
            look.signal = from.isMidi                              ? CableSignal::Midi
                          : target->kind == PortTarget::Kind::Knob ? CableSignal::ModCV
                                                                   : CableSignal::Audio;
            Preview preview;
            preview.p1 = from.isInput ? *there : *here; // the cable runs from the output to the input
            preview.p2 = from.isInput ? *here : *there;
            preview.colour = editor_.colourForCable(look);
            preview_ = preview;
        }
    }
    if (had || preview_.has_value())
        editor_.notifyModuleContentChanged();
}

void PortPanelController::clearPreview() { setPreview({}, nullptr); }

// A dashed wire in the cable's colour over the canvas, to the jack or knob a row in the panel would connect to.
void PortPanelController::paintPreview(juce::Graphics& g) const {
    if (!preview_.has_value())
        return;
    const auto curve = GraphEditor::buildCablePath(preview_->p1, preview_->p2);
    const float dashes[] = {6.0f, 5.0f};
    juce::Path dashed;
    juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt)
        .createDashedStroke(dashed, curve, dashes, juce::numElementsInArray(dashes));
    g.setColour(preview_->colour.withAlpha(0.9f));
    g.fillPath(dashed);
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
