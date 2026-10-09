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
#include "UI/Graph/PortPanel/PortConnectionsPanel.h"
#include "UI/Graph/PortPanel/PortPanelController.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;

float MacroCardComponent::portNameAlpha() const {
    // The parent is the canvas content component, whose transform is the zoom (scale + pan).
    const float zoom = getParentComponent() != nullptr ? getParentComponent()->getTransform().getScaleFactor() : 1.0f;
    return macroPortNameAlphaAtZoom(zoom);
}

bool MacroCardComponent::portNameIsTruncated(const juce::String& name) const {
    const juce::Font font{juce::FontOptions(kMacroPortNameFontSize)};
    return font.getStringWidthFloat(name) > (float)kMacroPortNameRoom;
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
        const juce::Colour kindTint =
            port.kind == synth::MacroPortKind::Midi ? themeColors.audioWire : themeColors.accent;
        g.setColour(resolvePortJackColour(port.nodeUuid, port.colour, kindTint));
        g.fillEllipse((float)port.jackPos.x - 5.0f, (float)port.jackPos.y - 5.0f, 10.0f, 10.0f);

        // Names fade in with zoom; the strip keeps its width and shows dots alone when they are hidden.
        if (nameAlpha > 0.0f && port.name.isNotEmpty()) {
            g.setColour(themeColors.textPrimary.withMultipliedAlpha(nameAlpha));
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

// One menu for the card's '+' and the open macro's hull '+': MacroGroupController::buildAddPortMenu.
juce::PopupMenu MacroCardComponent::buildAddPortMenu(bool isInput) {
    return owner.getMacroController().buildAddPortMenu(macroId, isInput);
}
