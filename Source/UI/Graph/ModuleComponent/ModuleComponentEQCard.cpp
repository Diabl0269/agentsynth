// ModuleComponentEQCard.cpp -- the Parametric EQ card: its layout geometry (namespace eqCard),
// knob placement, the pop-out EQ window, and the undo-gesture wiring for its curve editor.
// ModuleComponent is declared in ModuleComponent.h; the rest of its implementation lives in the
// sibling ModuleComponent*.cpp units next to this one (FRO65 split of the former single
// ModuleComponent.cpp).
#include "ModuleComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace eqCard {
constexpr int kMargin = 12;
constexpr int kCurveHeight = 150;
constexpr int kButtonRow = 28;
constexpr int kToggleRow = 26;
constexpr int kNumKnobRows = 3; // Freq / Gain / Q
constexpr int kNumVisibleInputs = 6;
constexpr int kScopeHeight = 100;

// Knob geometry is deliberately identical to the generic auto-UI layout, so an EQ knob is the
// same size as an Oscillator or Filter knob. The columns are wider than a knob, so each one is
// centred in its column rather than stretched to fill it.
constexpr int kKnobLabelHeight = 20;
constexpr int kSliderWidth = 70;
constexpr int kSliderHeight = 60;
constexpr int kTextBoxWidth = 50;
constexpr int kTextBoxHeight = 20;
constexpr int kKnobRow = kKnobLabelHeight + kSliderHeight;

// getPortCenter puts input i at y = 38 (header) + 20 (MIDI-out offset) + i*20 + 20, and each
// label is drawn 10px above its jack with height 20 — so the last one ends 10px below its centre.
constexpr int kPortFirstY = 78;
constexpr int kPortStep = 20;
constexpr int kPortLabelBottom = kPortFirstY + (kNumVisibleInputs - 1) * kPortStep + 16;

// The port labels only occupy a narrow gutter down each edge (inputs are drawn at x+10 with
// width 60 from a jack at x=10; outputs mirror that), so the curve sits BESIDE them starting
// just under the header rather than below the whole stack. On a six-input module that reclaims
// ~125px of otherwise dead space at the top of the card.
constexpr int kCurveTop = 60;
constexpr int kPortGutter = 88;
constexpr int kCurveBottom = kCurveTop + kCurveHeight;
constexpr int kContentTop = (kCurveBottom > kPortLabelBottom ? kCurveBottom : kPortLabelBottom) + 8;

constexpr const char* kKnobSuffix[kNumKnobRows] = {"Freq", "Gain", "Q"};
} // namespace eqCard

juce::ToggleButton* ModuleComponent::findToggleByName(const juce::String& name) const {
    for (auto* toggle : toggles)
        if (toggle->getComponentID().equalsIgnoreCase(name))
            return toggle;
    return nullptr;
}

void ModuleComponent::layoutNamedKnob(const juce::String& name, int x, int y, int w, int h) {
    juce::ignoreUnused(h);
    for (int i = 0; i < sliders.size(); ++i) {
        if (!sliders[i]->getComponentID().equalsIgnoreCase(name))
            continue;
        // Fixed knob box centred in the (wider) column, so EQ knobs render at exactly the same
        // size as every other module's rather than stretching to the column width.
        const int knobX = x + (w - eqCard::kSliderWidth) / 2;
        sliderLabels[i]->setBounds(x, y, w, eqCard::kKnobLabelHeight);
        sliders[i]->setTextBoxStyle(juce::Slider::TextBoxBelow, false, eqCard::kTextBoxWidth, eqCard::kTextBoxHeight);
        sliders[i]->setBounds(knobX, y + eqCard::kKnobLabelHeight, eqCard::kSliderWidth, eqCard::kSliderHeight);
        return;
    }
}

int ModuleComponent::parametricEQHeight() const {
    using namespace eqCard;
    // kContentTop already accounts for the curve, which is laid out beside the port labels.
    int h = kContentTop;
    h += kKnobRow + 6;   // Show Spectrum + Open EQ Window, with the Output trim sharing the row
    h += kToggleRow + 2; // per-band on/off row
    h += kNumKnobRows * kKnobRow + 4;
    if (scopeToggle)
        h += kButtonRow + 4;
    if (scopeComponent && scopeComponent->isVisible())
        h += kScopeHeight + 8;
    return h + 16; // bottom margin
}

void ModuleComponent::layoutParametricEQ() {
    using namespace eqCard;

    const int contentW = getWidth() - kMargin * 2;
    if (contentW <= 0)
        return;

    // The curve occupies the space between the input and output port-label gutters, level with
    // the jacks rather than below them.
    if (eqCurveComponent) {
        const int curveW = getWidth() - kPortGutter * 2;
        if (curveW > 0)
            eqCurveComponent->setBounds(kPortGutter, kCurveTop, curveW, kCurveHeight);
    }

    int y = kContentTop;

    // Buttons on the left, Output trim on the right of the same row — a full-width row for one
    // lone knob would leave three empty columns and make the card taller for nothing. The
    // buttons are nudged down so they sit against the knob's centre rather than its label.
    const int buttonY = y + (kKnobRow - kButtonRow) / 2;
    if (spectrumToggle)
        spectrumToggle->setBounds(kMargin, buttonY, 140, kButtonRow);
    if (eqPopOutButton)
        eqPopOutButton->setBounds(kMargin + 150, buttonY, 150, kButtonRow);
    layoutNamedKnob("Output", getWidth() - kMargin - kSliderWidth, y, kSliderWidth, kKnobRow);
    y += kKnobRow + 6;

    // One column per band. The on/off toggle's text is the band's type ("1 Low Shelf"), so the
    // column header and its enable control are the same widget.
    const int colW = contentW / ParametricEQModule::kNumBands;
    for (int b = 0; b < ParametricEQModule::kNumBands; ++b) {
        if (auto* toggle = findToggleByName(ParametricEQModule::rowLabelFor(b)))
            toggle->setBounds(kMargin + b * colW, y, colW - 4, kToggleRow);
    }
    y += kToggleRow + 2;

    for (int row = 0; row < kNumKnobRows; ++row) {
        for (int b = 0; b < ParametricEQModule::kNumBands; ++b) {
            const juce::String name = "B" + juce::String(b + 1) + " " + kKnobSuffix[row];
            layoutNamedKnob(name, kMargin + b * colW, y + row * kKnobRow, colW - 4, kKnobRow);
        }
    }
    y += kNumKnobRows * kKnobRow + 4;

    if (scopeToggle) {
        scopeToggle->setBounds(kMargin, y, contentW, kButtonRow);
        y += kButtonRow + 4;
    }
    if (scopeComponent && scopeComponent->isVisible())
        scopeComponent->setBounds(kMargin, y, contentW, kScopeHeight);
}

void ModuleComponent::wireEqGestureCallbacks(EQCurveComponent& curve) {
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    auto capture = [safeThis](bool isStart) {
        if (safeThis == nullptr || safeThis->undoManager == nullptr || safeThis->module == nullptr)
            return;
        auto& graph = safeThis->owner.getAudioEngine().getGraph();
        if (isStart)
            safeThis->undoManager->captureBeforeState(graph);
        else
            safeThis->undoManager->pushSnapshotFromCapture(graph);
    };
    curve.onGestureStart = [capture] { capture(true); };
    curve.onGestureEnd = [capture] { capture(false); };
}

void ModuleComponent::openEqWindow() {
    if (eqWindow != nullptr) {
        eqWindow->toFront(true);
        return;
    }

    auto* eqMod = dynamic_cast<ParametricEQModule*>(module);
    if (eqMod == nullptr)
        return;

    auto content = std::make_unique<EQWindow>(*eqMod);
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    content->setGestureCallbacks(
        [safeThis] {
            if (safeThis != nullptr && safeThis->undoManager != nullptr && safeThis->module != nullptr)
                safeThis->undoManager->captureBeforeState(safeThis->owner.getAudioEngine().getGraph());
        },
        [safeThis] {
            if (safeThis != nullptr && safeThis->undoManager != nullptr && safeThis->module != nullptr)
                safeThis->undoManager->pushSnapshotFromCapture(safeThis->owner.getAudioEngine().getGraph());
        });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content.release());
    options.dialogTitle = module->getName();
    options.componentToCentreAround = getTopLevelComponent();
    options.useNativeTitleBar = true;
    options.resizable = true;
    eqWindow = options.launchAsync();
}
