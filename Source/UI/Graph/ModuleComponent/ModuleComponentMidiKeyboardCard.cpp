// ModuleComponentMidiKeyboardCard.cpp -- the MIDI Keyboard card's bespoke body: the on-screen keys and the
// Octave stepper row above them (the module's `octave` parameter, drawn as the card widgets' stepper:
// "-" value "+", keyboard reachable, with an accessible title and a tooltip). The card builds no CardBody,
// so the row is card code. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units next to this one.
// docs/layout/module-card.md#faders-switches-and-steppers.
#include "ModuleComponent.h"
#include "Modules/MidiKeyboardModule.h"
#include "UI/Graph/CardWidgets/CardStepper.h"
#include "UI/Layout/LayoutUtil.h"

namespace {

constexpr int kKeyboardCardMargin = 10;
constexpr int kOctaveRowY = 32;      // under the header, clear of the Midi output jack at the right edge
constexpr int kOctaveRowHeight = 24; // a stepper is one row tall
constexpr int kOctaveCaptionWidth = 52;
constexpr int kOctaveStepperWidth = 132;
constexpr int kKeysGap = 4; // between the row and the keys
constexpr int kKeysHeight = 90;
// 160: GraphEditorDragDrop.cpp's bespokeCardSizeTable pins the same number for the size estimate.
constexpr int kCardHeight = kOctaveRowY + kOctaveRowHeight + kKeysGap + kKeysHeight + kKeyboardCardMargin;

} // namespace

// The Octave stepper, bound like every card stepper: a pick is one complete gesture (one undo step), and a
// value from automation or undo moves the display without notifying back. The card listens to the stepper
// and its buttons, so a right click opens the control menu (MIDI Learn included).
void ModuleComponent::createMidiKeyboardOctaveRow() {
    auto* param =
        module != nullptr ? dynamic_cast<juce::AudioParameterInt*>(findParameterByID(module, "octave")) : nullptr;
    if (param == nullptr)
        return;
    auto* caption = adoptBespokeWidget(new juce::Label("Octave", "Octave"));
    caption->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(caption);
    octaveCaption_ = caption;

    auto* stepper = adoptBespokeWidget(new synth::ui::CardStepper(param->getName(100)));
    stepper->setComponentID(param->getName(100));
    stepper->setTooltip(param->getName(100));
    addAndMakeVisible(stepper);
    stepper->addMouseListener(this, true);
    registerMidiLearnable(*stepper, param);
    octaveAttachment_ = std::make_unique<juce::ParameterAttachment>(
        *param, [stepper, param](float) { stepper->setValueText(param->getCurrentValueAsText()); });
    stepper->onStep = [this, param](int delta) {
        const auto range = param->getRange();
        const int next = juce::jlimit(range.getStart(), range.getEnd(), param->get() + delta);
        if (next != param->get())
            octaveAttachment_->setValueAsCompleteGesture((float)next);
    };
    octaveAttachment_->sendInitialUpdate();
    octaveStepper_ = stepper;
}

void ModuleComponent::layoutMidiKeyboardCard() {
    setSize(synth::LayoutUtil::kDoubleWidth, kCardHeight);
    if (octaveCaption_ != nullptr)
        octaveCaption_->setBounds(kKeyboardCardMargin, kOctaveRowY, kOctaveCaptionWidth, kOctaveRowHeight);
    if (octaveStepper_ != nullptr)
        octaveStepper_->setBounds(kKeyboardCardMargin + kOctaveCaptionWidth, kOctaveRowY, kOctaveStepperWidth,
                                  kOctaveRowHeight);
    if (keyboardComponent != nullptr)
        keyboardComponent->setBounds(kKeyboardCardMargin, kOctaveRowY + kOctaveRowHeight + kKeysGap,
                                     getWidth() - 2 * kKeyboardCardMargin, kKeysHeight);
}
