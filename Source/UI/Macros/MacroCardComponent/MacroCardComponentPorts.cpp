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
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;

namespace {
constexpr float kFooterButtonSize = 8.0f;
constexpr float kFooterButtonFromBottom = 12.0f;
constexpr float kAddButtonInset = 4.0f;     // '+' x from its strip's outer edge
constexpr float kRemoveButtonInset = 16.0f; // '-' x from its strip's outer edge

// The glyph shared by '+' and '-': a ring, and either one or two strokes across it.
void paintFooterButton(juce::Graphics& g, juce::Rectangle<float> b, juce::Colour colour, bool withVerticalStroke) {
    g.setColour(colour);
    g.drawEllipse(b, 1.2f);
    const auto cross = b.reduced(b.getWidth() * 0.28f);
    if (withVerticalStroke)
        g.drawLine(cross.getCentreX(), cross.getY(), cross.getCentreX(), cross.getBottom(), 1.4f);
    g.drawLine(cross.getX(), cross.getCentreY(), cross.getRight(), cross.getCentreY(), 1.4f);
}
} // namespace

bool MacroCardComponent::portNamesVisible() const {
    // The parent is the canvas content component, whose transform is the zoom (scale + pan).
    const float zoom = getParentComponent() != nullptr ? getParentComponent()->getTransform().getScaleFactor() : 1.0f;
    return macroPortNamesVisibleAtZoom(zoom);
}

juce::Rectangle<float> MacroCardComponent::getAddPortButtonBounds(bool isInput) const {
    // At the strip's foot: kMacroPortStripFooter below the last row a card of this height holds, so
    // the button can never sit on a jack row. The card's height follows its port count
    // (macroCardHeightFor), so the foot follows the card's own bottom edge.
    const float y = (float)getHeight() - kFooterButtonFromBottom;
    const float x = isInput ? kAddButtonInset : (float)getWidth() - kAddButtonInset - kFooterButtonSize;
    return {x, y, kFooterButtonSize, kFooterButtonSize};
}

juce::Rectangle<float> MacroCardComponent::getRemovePortButtonBounds(bool isInput) const {
    if (!portNamesVisible())
        return {};
    bool hasPort = false;
    for (const auto& port : owner.getMacroController().macroCardPortLayout(macroId))
        hasPort = hasPort || port.isInput == isInput;
    if (!hasPort)
        return {};
    const float y = (float)getHeight() - kFooterButtonFromBottom;
    const float x = isInput ? kRemoveButtonInset : (float)getWidth() - kRemoveButtonInset - kFooterButtonSize;
    return {x, y, kFooterButtonSize, kFooterButtonSize};
}

void MacroCardComponent::removeBottomPort(bool isInput) {
    const MacroGroupController::MacroCardPort* bottom = nullptr;
    const auto layout = owner.getMacroController().macroCardPortLayout(macroId);
    for (const auto& port : layout)
        if (port.isInput == isInput && (bottom == nullptr || port.row > bottom->row))
            bottom = &port;
    if (bottom == nullptr)
        return;
    const juce::String uuid = bottom->nodeUuid; // the layout dies with the delete's rebuild
    hoveredPortUuid_.reset();
    owner.getMacroController().deleteMacroPortManually(macroId, uuid);
}

void MacroCardComponent::paintPortStrips(juce::Graphics& g, const synth::Macro& macro,
                                         const synth::theme::Colors& themeColors) {
    auto& controller = owner.getMacroController();
    const auto [inW, outW] = controller.macroCardStripWidths(macro.id);
    const bool names = portNamesVisible();
    const auto layout = controller.macroCardPortLayout(macro.id);
    const float w = (float)getWidth();
    const float h = (float)getHeight();
    constexpr float kRadius = 8.0f; // the card's own corner radius, followed on the outer corners

    // Strips: a tint of surface-hi so the macro colour still shows through. Both are always drawn
    // (a side with no ports still holds its '+').
    juce::Path left, right;
    left.addRoundedRectangle(0.0f, 0.0f, (float)inW, h, kRadius, kRadius, true, false, true, false);
    right.addRoundedRectangle(w - (float)outW, 0.0f, (float)outW, h, kRadius, kRadius, false, true, false, true);
    g.setColour(themeColors.surfaceHi.withAlpha(0.55f));
    g.fillPath(left);
    g.fillPath(right);
    g.setColour(themeColors.border);
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

        // Names are painted only at working zoom; below it the strip keeps its width and shows dots.
        if (names && port.name.isNotEmpty()) {
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.drawFittedText(port.name, port.labelArea,
                             port.isInput ? juce::Justification::centredLeft : juce::Justification::centredRight, 1);
        }
    }

    // '+' always; '-' only when its side has a port and names are visible.
    const auto glyphColour = themeColors.textMuted.withAlpha(0.75f);
    for (const bool isInput : {true, false}) {
        paintFooterButton(g, getAddPortButtonBounds(isInput), glyphColour, true);
        const auto minus = getRemovePortButtonBounds(isInput);
        if (!minus.isEmpty())
            paintFooterButton(g, minus, glyphColour, false);
    }

    // Hovered jack: a cross over the dot (hoveredPortUuid_ is kept fresh by mouseMove/mouseExit;
    // this Component has no child Button of its own to hover). mouseDown re-checks the same hit-test
    // before deleting.
    if (hoveredPortUuid_.has_value()) {
        for (const auto& port : layout) {
            if (port.nodeUuid != *hoveredPortUuid_)
                continue;
            const juce::Rectangle<float> dot((float)port.jackPos.x - 5.0f, (float)port.jackPos.y - 5.0f, 10.0f, 10.0f);
            g.setColour(themeColors.surface.withAlpha(0.9f));
            g.fillEllipse(dot);
            g.setColour(themeColors.error);
            const auto cross = dot.reduced(dot.getWidth() * 0.22f);
            g.drawLine(cross.getX(), cross.getY(), cross.getRight(), cross.getBottom(), 1.6f);
            g.drawLine(cross.getX(), cross.getBottom(), cross.getRight(), cross.getY(), 1.6f);
            break;
        }
    }
}

juce::PopupMenu MacroCardComponent::buildAddPortMenu(bool isInput) {
    // The SAME kind/shape choice list synth::ui::MacroPortConfigDialog's own "Add a port" panel
    // offers (Audio/CV picks Mono/Stereo/Poly-N, MIDI has no shape) and, on a choice, the SAME
    // MacroGroupController::addMacroPort() Configure I/O's Add button calls, so the created port and
    // its one-undo-step transaction are identical either way. `isInput` is fixed by which side's '+'
    // was clicked; an empty name falls back to addMacroPort's own defaultMacroPortName(). The Poly-N
    // voice count matches the dialog's own default ("4").
    juce::PopupMenu menu;
    juce::Component::SafePointer<MacroCardComponent> safeThis(this);
    auto addItem = [&menu, safeThis, isInput](const juce::String& text, synth::MacroPortKind kind, MacroPortShape shape,
                                              int voices) {
        menu.addItem(text, [safeThis, isInput, kind, shape, voices] {
            if (safeThis == nullptr)
                return;
            safeThis->owner.getMacroController().addMacroPort(safeThis->macroId, isInput, kind, shape, voices, {});
        });
    };
    addItem("Audio/CV - Mono", synth::MacroPortKind::AudioCV, MacroPortShape::Mono, 1);
    addItem("Audio/CV - Stereo", synth::MacroPortKind::AudioCV, MacroPortShape::Stereo, 1);
    addItem("Audio/CV - Poly-N", synth::MacroPortKind::AudioCV, MacroPortShape::Poly, 4);
    addItem("MIDI", synth::MacroPortKind::Midi, MacroPortShape::Mono, 1);
    return menu;
}
