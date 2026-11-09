// MacroCardComponentPorts.cpp
//
// The collapsed card's port sidebars: the two strips, jack dots, names, the '+'/'-' pair at each
// strip's foot, the add-port menu and the bottom-port removal. Everything here reads
// MacroGroupController::macroCardPortLayout / macroCardStripWidths, the one layout that paint,
// hit-testing and boundary-cable anchoring share (docs/macros/ports.md#how-a-port-is-drawn).
// MacroCardComponent is declared in MacroCardComponent.h; MacroCardComponent.cpp holds the rest.

#include "MacroCardComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Graph/PortPanel/PortConnectionList.h"
#include "UI/Graph/PortPanel/PortConnectionsPanel.h"
#include "UI/Graph/PortPanel/PortPanelController.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;

float MacroCardComponent::portNameAlpha() const {
    // The parent is the canvas content component, whose transform is the zoom (scale + pan).
    const float zoom = getParentComponent() != nullptr ? getParentComponent()->getTransform().getScaleFactor() : 1.0f;
    return macroPortNameAlphaAtZoom(zoom);
}

bool MacroCardComponent::portNameIsTruncated(const juce::String& name) const {
    const juce::Font font{juce::FontOptions(kMacroPortNameFontSize)};
    return juce::GlyphArrangement::getStringWidth(font, name) > (float)kMacroPortNameRoom;
}

juce::Rectangle<float> MacroCardComponent::getAddPortButtonBounds(bool isInput) const {
    // At the strip's foot: kMacroPortStripFooter below the last row a card of this height holds, so
    // the button can never sit on a jack row. The card's height follows its port count
    // (macroCardHeightFor), so the foot follows the card's own bottom edge.
    const float y = (float)getHeight() - kMacroPortFooterButtonFromBottom;
    const float x =
        isInput ? kMacroPortAddButtonInset : (float)getWidth() - kMacroPortAddButtonInset - kMacroPortFooterButtonSize;
    return {x, y, kMacroPortFooterButtonSize, kMacroPortFooterButtonSize};
}

juce::Rectangle<float> MacroCardComponent::getRemovePortButtonBounds(bool isInput) const {
    if (portNameAlpha() < 0.5f)
        return {};
    return removePortButtonSlot(isInput);
}

juce::Rectangle<float> MacroCardComponent::removePortButtonSlot(bool isInput) const {
    bool hasPort = false;
    for (const auto& port : owner.getMacroController().macroCardPortLayout(macroId))
        hasPort = hasPort || port.isInput == isInput;
    if (!hasPort)
        return {};
    const float y = (float)getHeight() - kMacroPortFooterButtonFromBottom;
    const float x = isInput ? kMacroPortRemoveButtonInset
                            : (float)getWidth() - kMacroPortRemoveButtonInset - kMacroPortFooterButtonSize;
    return {x, y, kMacroPortFooterButtonSize, kMacroPortFooterButtonSize};
}

void MacroCardComponent::removeBottomPort(bool isInput) {
    hoveredPortUuid_.reset();
    owner.getMacroController().deleteBottomMacroPort(macroId, isInput);
}

void MacroCardComponent::portReleased(const juce::MouseEvent& e) {
    auto& controller = owner.getMacroController();
    const auto hit = controller.macroCardPortForPoint(macroId, e.getMouseDownPosition());
    if (!hit.has_value())
        return;
    const auto node = controller.resolveMemberNodeId(hit->nodeUuid);
    if (node.uid == 0)
        return;
    // The cables drawn to the card land on the port node's OUTSIDE jack: an inlet's input, an outlet's output.
    const bool midi = hit->kind == synth::MacroPortKind::Midi;
    const synth::ui::PortRef ref{
        node, midi ? juce::AudioProcessorGraph::midiChannelIndex : juce::jmax(0, hit->visibleJack), hit->isInput, midi};
    constexpr int kDot = 10; // the painted dot (see the hover ring in paintPortStrips)
    const auto dot = juce::Rectangle<int>(kDot, kDot).withCentre(hit->jackPos);
    owner.getPortPanel().jackReleased(*this, dot, ref, e);
}

// The same rule as a module card's jack: one cable goes, none or several leave the double-click to the card.
bool MacroCardComponent::disconnectOnlyCableAt(const juce::MouseEvent& e) {
    if (!owner.getDoubleClickPortDisconnectEnabled())
        return false;
    auto& controller = owner.getMacroController();
    const auto hit = controller.macroCardPortForPoint(macroId, e.getPosition());
    if (!hit.has_value())
        return false;
    const auto node = controller.resolveMemberNodeId(hit->nodeUuid);
    if (node.uid == 0)
        return false;
    const bool midi = hit->kind == synth::MacroPortKind::Midi;
    const synth::ui::PortRef ref{
        node, midi ? juce::AudioProcessorGraph::midiChannelIndex : juce::jmax(0, hit->visibleJack), hit->isInput, midi};
    if (synth::ui::listPortConnections(owner, ref).size() != 1)
        return false;
    owner.getPortPanel().disconnectAll(ref);
    return true;
}

void MacroCardComponent::closeOwnPortPanel() {
    auto& portPanel = owner.getPortPanel();
    portPanel.cancelPendingOpen();
    if (auto* open = portPanel.getPanel()) {
        const auto portOwner = owner.getMacroController().macroPortOwnerFor(open->port().node);
        if (portOwner.macro != nullptr && portOwner.macro->id == macroId)
            portPanel.close();
    }
}

void MacroCardComponent::paintPortStrips(juce::Graphics& g, const synth::Macro& macro,
                                         const synth::theme::Colors& themeColors) {
    auto& controller = owner.getMacroController();
    const auto [inW, outW] = controller.macroCardStripWidths(macro.id);
    const float nameAlpha = portNameAlpha();
    const auto layout = controller.macroCardPortLayout(macro.id);
    const float w = (float)getWidth();
    const float h = (float)getHeight();
    constexpr float kRadius = 8.0f; // the card's own corner radius, followed on the outer corners

    // Strips: a tint of surface-hi so the macro colour still shows through. Both are always drawn
    // (a side with no ports still holds its '+').
    juce::Path left, right;
    left.addRoundedRectangle(0.0f, 0.0f, (float)inW, h, kRadius, kRadius, true, false, true, false);
    right.addRoundedRectangle(w - (float)outW, 0.0f, (float)outW, h, kRadius, kRadius, false, true, false, true);
    g.setColour(themeColors.surfaceHi.withAlpha(macroStripFillAlpha(nameAlpha)));
    g.fillPath(left);
    g.fillPath(right);
    g.setColour(themeColors.border.withMultipliedAlpha(nameAlpha)); // the inner dividers fade with the names
    g.drawLine((float)inW - 0.5f, 1.0f, (float)inW - 0.5f, h - 1.0f, 1.0f);
    g.drawLine(w - (float)outW + 0.5f, 1.0f, w - (float)outW + 0.5f, h - 1.0f, 1.0f);

    // Jack dots. Colour follows ModuleComponent::paint's own jack convention (audioWire for a MIDI
    // port, accent for Audio/CV); a JACK dot is not a cable, so this is not the *Wire-at-a-paint-site
    // the cable-colour invariant forbids. A port's user colour, or an armed picker preview, wins.
    g.setFont(juce::Font(juce::FontOptions(kMacroPortNameFontSize)));
    for (const auto& port : layout) {
        // A row growInPort is fading in: its dot grows from nothing as it fades (a plain fade under Reduce Motion).
        const float shown = getPortShown(port.nodeUuid);
        const float radius = 5.0f * (synth::ui::prefersReducedMotion() ? 1.0f : shown);
        const juce::Colour kindTint =
            port.kind == synth::MacroPortKind::Midi ? themeColors.audioWire : themeColors.accent;
        g.setColour(resolvePortJackColour(port.nodeUuid, port.colour, kindTint).withMultipliedAlpha(shown));
        g.fillEllipse((float)port.jackPos.x - radius, (float)port.jackPos.y - radius, radius * 2.0f, radius * 2.0f);

        // Names fade in with zoom; the strip keeps its width and shows dots alone when they are hidden.
        if (nameAlpha > 0.0f && port.name.isNotEmpty()) {
            g.setColour(themeColors.textPrimary.withMultipliedAlpha(nameAlpha * shown));
            g.drawFittedText(port.name, port.labelArea,
                             port.isInput ? juce::Justification::centredLeft : juce::Justification::centredRight, 1,
                             1.0f);
        }
    }

    // '+' always; '-' only when its side has a port, fading with the names.
    const auto glyphColour = themeColors.textMuted.withAlpha(0.75f);
    for (const bool isInput : {true, false}) {
        paintMacroPortFooterButton(g, getAddPortButtonBounds(isInput), glyphColour, true);
        const auto minus = removePortButtonSlot(isInput);
        if (nameAlpha > 0.0f && !minus.isEmpty())
            paintMacroPortFooterButton(g, minus, glyphColour.withMultipliedAlpha(nameAlpha), false);
    }

    // Hovered jack: a ring round the dot (hoveredPortUuid_ is kept fresh by mouseMove/mouseExit; this Component has no
    // child Button of its own to hover). A click on it opens the port connections panel.
    if (hoveredPortUuid_.has_value()) {
        for (const auto& port : layout) {
            if (port.nodeUuid != *hoveredPortUuid_ || port.visibleJack != hoveredPortJack_)
                continue;
            const juce::Rectangle<float> dot((float)port.jackPos.x - 5.0f, (float)port.jackPos.y - 5.0f, 10.0f, 10.0f);
            g.setColour(themeColors.textPrimary.withAlpha(0.85f));
            g.drawEllipse(dot.expanded(2.5f), 1.5f);
            break;
        }
    }
}

// The drag tick sets this from MacroGroupController::cardCableDropAt, the answer the drop commits through, so the hint
// can never promise a landing the release does not make. It fades in like a hover (120 ms easeOutCubic) and out in
// 90 ms easeInCubic; ShownFade makes that a plain 80 ms fade under Reduce Motion and instant under Animations Off or
// off screen. While it fades out the last hint stays drawn. A drop that lands clears it `instant`, so a glide's picture
// of the card never carries a half-faded outline.
void MacroCardComponent::setCableDropHint(std::optional<CableDropHint> hint, bool instant) {
    if (hint == cableDropHint_ && !instant)
        return;
    cableDropHint_ = hint;
    if (hint.has_value())
        paintedHint_ = hint;
    if (hintFade_ == nullptr) {
        hintFade_ = std::make_unique<synth::ui::ShownFade>(*this);
        hintFade_->onFrame = [this] {
            if (!cableDropHint_.has_value() && hintFade_->alpha() <= 0.0f)
                paintedHint_.reset();
            repaint();
        };
    }
    const float target = hint.has_value() ? 1.0f : 0.0f;
    if (instant)
        hintFade_->jumpTo(target);
    else if (hint.has_value())
        hintFade_->fadeTo(target, 120.0, synth::ui::easeOutCubic);
    else
        hintFade_->fadeTo(target, 90.0, synth::ui::easeInCubic);
}

float MacroCardComponent::getCableDropHintAlphaForTest() const {
    return hintFade_ != nullptr ? hintFade_->alpha() : 0.0f;
}

// A port a dropped cable just made grows in instead of appearing: 170 ms easeOutCubic, landing a little before the
// cable's 220 ms slide reaches its jack. ShownFade makes it a plain 80 ms fade under Reduce Motion (paint then keeps
// the dot full size) and lands it at once under Animations Off or off screen. One row at a time: a second call moves
// the fade to the newer port and shows the older one fully.
void MacroCardComponent::growInPort(const juce::String& portUuid) {
    if (portEnterFade_ == nullptr) {
        portEnterFade_ = std::make_unique<synth::ui::ShownFade>(*this);
        portEnterFade_->onFrame = [this] {
            if (portEnterFade_->alpha() >= 1.0f)
                enteringPortUuid_.clear();
            repaint();
        };
    }
    enteringPortUuid_ = portUuid;
    portEnterFade_->jumpTo(0.0f);
    portEnterFade_->fadeTo(1.0f, 170.0, synth::ui::easeOutCubic);
}

float MacroCardComponent::getPortShown(const juce::String& portUuid) const {
    if (portEnterFade_ == nullptr || enteringPortUuid_.isEmpty() || portUuid != enteringPortUuid_)
        return 1.0f;
    return portEnterFade_->alpha();
}

void MacroCardComponent::stepPortEnterForTest(double ms) {
    if (portEnterFade_ != nullptr)
        portEnterFade_->stepForTest(ms);
}

// The card outlined in the accent colour with a soft glow, and its port row marked: a ring round the jack of the port
// that takes the cable, or a hollow dot in the next free row of the side a new port is added to (the '+' of that side
// when the row would fall on it). Positions come from macroCardPortLayout and the '+' bounds, the same layout paint
// and the drop's hit test read.
void MacroCardComponent::paintCableDropHint(juce::Graphics& g, const synth::Macro& macro,
                                            const synth::theme::Colors& themeColors) {
    const float alpha = hintFade_ != nullptr ? hintFade_->alpha() : 0.0f;
    if (!paintedHint_.has_value() || alpha <= 0.0f)
        return;
    const auto& hint = *paintedHint_;
    const auto accent = themeColors.accent;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(accent.withMultipliedAlpha(0.25f * alpha));
    g.drawRoundedRectangle(bounds.reduced(2.0f), 8.0f, 4.0f);
    g.setColour(accent.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f, 2.0f);

    constexpr float kDot = 10.0f;
    const auto layout = owner.getMacroController().macroCardPortLayout(macro.id);
    if (!hint.newPort) {
        for (const auto& port : layout) {
            if (port.nodeUuid != hint.portUuid || (hint.visibleJack >= 0 && port.visibleJack != hint.visibleJack))
                continue;
            const auto dot = juce::Rectangle<float>(kDot, kDot).withCentre(port.jackPos.toFloat());
            g.drawEllipse(dot.expanded(3.0f), 2.0f); // a whole two-row Stereo port rings both rows
        }
        return;
    }
    int rows = 0;
    for (const auto& port : layout)
        rows += port.isInput == hint.isInput ? 1 : 0;
    const float x = hint.isInput ? 10.0f : (float)getWidth() - 10.0f;
    const float y = (float)(kMacroPortRowsTop + rows * kMacroPortRowHeight + kMacroPortRowHeight / 2);
    const auto plus = getAddPortButtonBounds(hint.isInput);
    const auto slot = juce::Rectangle<float>(kDot, kDot).withCentre({x, y});
    if (slot.getBottom() + 2.0f <= plus.getY())
        g.drawEllipse(slot.reduced(0.75f), 1.5f);
    else
        g.drawEllipse(plus.expanded(3.0f), 2.0f);
}

// One menu for the card's '+' and the open macro's hull '+': MacroGroupController::buildAddPortMenu.
juce::PopupMenu MacroCardComponent::buildAddPortMenu(bool isInput) {
    return owner.getMacroController().buildAddPortMenu(macroId, isInput);
}
