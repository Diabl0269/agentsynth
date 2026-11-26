#pragma once

// Private to the ModuleComponent.cpp / ModuleComponent*.cpp translation units. Holds the file-local constants and free
// helpers used by more than one of those units — everything used by only one unit stays in that unit's own file
// instead. Not part of the public API: nothing outside the ModuleComponent units should include this header. Assumes
// ModuleComponent.h is included first (for ModuleComponent::kMacroPortWidgetHeaderY, and the JUCE module headers these
// declarations depend on).

#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace detail {

/** The header's activity LED (an 8 px dot at 6,8) with room for its soft edge: all the RMS tick repaints. */
inline juce::Rectangle<int> activityLedArea() noexcept { return {4, 6, 12, 12}; }

// ---- Modulation-ring geometry --------------------------------------------------------
// The ONE place the ring/band centre and radius are computed from a knob's bounds -- shared by
// paintModulationRings (ModuleComponentPaint.cpp), the drop-target ring, getModTargetKnobAnchor and
// CardKnobSlider's annulus hit-tests (wantsModAmountGesture, wired up in
// ModuleComponent::createControls), so a drag gesture's "am I on the ring" test can never drift
// from what actually gets painted there.
// The dial is not centred in the slider's bounds: the value text box takes a strip on one side
// (below for most knobs, ABOVE for ADSR -- see CardBody.cpp's setFloatKnobStyle), so the centre is shifted
// by the slider's own layout (getSliderLayout().sliderBounds, the rect drawRotarySlider paints in)
// relative to its local bounds. `boundsInCallerSpace` is the slider's bounds in whatever frame the
// caller works in (card-local getBounds(), or the knob's own getLocalBounds()); only its size and
// position are used, the layout offset is frame-independent. Below-text-box knobs get exactly the
// old centreY - 10.
inline juce::Point<float> modRingCentreFor(const juce::Slider& slider, juce::Rectangle<float> boundsInCallerSpace) {
    const auto layoutCentre =
        slider.getLookAndFeel().getSliderLayout(const_cast<juce::Slider&>(slider)).sliderBounds.toFloat().getCentre();
    const auto offset = layoutCentre - slider.getLocalBounds().toFloat().getCentre();
    return boundsInCallerSpace.getCentre() + offset;
}
inline float modRingRadiusFor(juce::Rectangle<float> sliderBounds) {
    return std::min(sliderBounds.getWidth(), sliderBounds.getHeight()) / 2.0f - 11.0f;
}

// A point on the ring's own circle for a given 0..1 norm, in the SAME coordinate space as
// `centre` -- shared with AppLookAndFeel::drawModulationRing via modRingAngleForNorm (the ONE
// angle-mapping helper) so a cable re-anchored onto a knob (ModuleComponent::getModTargetKnobAnchor,
// GraphEditorModHover.cpp) always lands exactly on the drawn ring, never a hand-rolled
// approximation. JUCE's addCentredArc angle convention is clockwise from 12 o'clock (angle 0 is
// straight up), hence (sin, -cos) rather than the usual (cos, sin).
inline juce::Point<float> modRingPointForNorm(juce::Point<float> centre, float radius, float norm) {
    const float angle = synth::theme::AppLookAndFeel::modRingAngleForNorm(norm);
    return {centre.x + radius * std::sin(angle), centre.y - radius * std::cos(angle)};
}

// ---- Default body-layout metrics (see layoutDefaultContent) ----------------------------------
// Defined once beside the card body's run layouts (UI/Graph/CardBody/CardBodyGeometry.h), which the
// static size estimate measures with too.
using synth::cardbody::kBottomPadding;
using synth::cardbody::kContentMargin;
using synth::cardbody::kKnobColumns;
using synth::cardbody::kKnobHeight;
using synth::cardbody::kLabelHeight;
using synth::cardbody::kNarrowContentWidth;
using synth::cardbody::kPortLabelClearance;
using synth::cardbody::kRowHeight;
using synth::cardbody::kWaveformHeight;
// Horizontal step between input-jack columns on a multi-column gutter. A jack sits at x, its
// label runs from x+10 for 60px, so 100 leaves a 30px gap before the next column's jack.
inline constexpr int kPortColumnStride = 100;

inline ModuleType getType(juce::AudioProcessor* module) {
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        return mb->getModuleType();
    return ModuleType::Oscillator;
}

/** The four macro-boundary node types (Macro In/Out, Macro MIDI In/Out — docs/macros/ports.md#node-types),
 *  which render as the compact docked port widget (docs/macros/ports.md#how-a-port-is-drawn) rather than an
 *  ordinary module card: no header chrome, no body, a small tinted row docked to their macro's
 *  hull edge instead of freely placed. See layoutMacroPortWidget()/paintMacroPortWidget(). */
inline bool isMacroPortType(ModuleType t) {
    return t == ModuleType::MacroInlet || t == ModuleType::MacroOutlet || t == ModuleType::MacroMidiInlet ||
           t == ModuleType::MacroMidiOutlet;
}

/** The MIDI jack's fixed y — the generic layout's own "38, below the header" convention, compacted
 *  for the macro-port widget (which has no header). Shared by paint()'s MIDI dot and
 *  getPortForPoint()'s MIDI hit-test so the two can never disagree, mirroring every other
 *  paint/hit-test pairing in this file. */
inline int midiJackY(juce::AudioProcessor* module) {
    return isMacroPortType(getType(module)) ? ModuleComponent::kMacroPortWidgetHeaderY : 38;
}

/** Where the "Midi Out" label is drawn, left of the MIDI output dot at `dot`: its right edge stops 10 px short of the
 * dot's centre, the same clearance an audio output's label keeps from its dot, so the text never touches the 5 px dot.
 */
inline juce::Rectangle<int> midiOutLabelBounds(juce::Point<int> dot) { return {dot.x - 70, dot.y - 5, 60, 10}; }

/** True for the graph's terminal audio sink (Audio Output). Mirrors GraphEditor.cpp's
 *  isTerminalAudioSink — detected by TYPE, not by name, so a ModuleBase happening to be titled
 *  "Audio Output" cannot impersonate it. A bare juce::AudioGraphIOProcessor is never a ModuleBase,
 *  so getType() above always falls back to Oscillator for it; this is the one place in this file
 *  that actually cares which IO node it is (the output-card identity treatment in paint()). */
inline bool isAudioOutputIONode(juce::AudioProcessor* module) {
    using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
    auto* io = dynamic_cast<IOProcessor*>(module);
    return io != nullptr && io->getType() == IOProcessor::audioOutputNode;
}

/** The label beside Audio Output input jack `jack`; `rightFollowsLeft` is the engine's normalling. */
inline juce::String audioOutputJackLabel(int jack, bool rightFollowsLeft) {
    if (jack == 0)
        return "L / Mono";
    if (jack == 1)
        return rightFollowsLeft ? "Right (follows L)" : "Right";
    return "In " + juce::String(jack);
}

/** The label beside input jack `jack` of `module`; `outputRightFollowsLeft` is the engine's Audio Output normalling. */
inline juce::String inputJackLabel(juce::AudioProcessor* module, int jack, bool outputRightFollowsLeft) {
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        return mb->getInputPortLabel(jack);
    // Audio Output's Left is "L / Mono": it normals onto Right while Right is unpatched (getTooltip() says so on
    // hover), and Right says it follows Left meanwhile. Audio Input has no normalling, so plain Left/Right.
    if (isAudioOutputIONode(module))
        return audioOutputJackLabel(jack, jack == 1 && outputRightFollowsLeft);
    if (dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(module) != nullptr)
        return (jack == 0) ? "Left" : (jack == 1) ? "Right" : "In " + juce::String(jack);
    return "In " + juce::String(jack);
}

// RightClickSafeButton lives in Source/UI/MidiRemote/MidiLearnMenu.h -- the mixer (Mute button) and
// the transport bar (the GlyphButtons) need the exact same right-click guard, and that header
// (unlike this one) is meant to be included outside the ModuleComponent units.
using MidiLearnableToggleButton = synth::ui::midilearn::RightClickSafeButton<juce::ToggleButton>;
using MidiLearnableDrawableButton = synth::ui::midilearn::RightClickSafeButton<juce::DrawableButton>;

/** The header icons' inset from their button's edge: the focus ring is 2.5 px on a light theme and sits 1 px in, so the
 *  icon stays clear of it. */
constexpr int kHeaderIconEdgeIndent = 4;

/** Insets the icons of the header buttons that exist (a null entry is skipped). */
inline void insetHeaderIcons(std::initializer_list<juce::DrawableButton*> buttons) {
    for (auto* button : buttons)
        if (button != nullptr)
            button->setEdgeIndent(kHeaderIconEdgeIndent);
}

/** The header's toggling Mute button, unparented; a card adds it only when its module has a mute parameter. */
inline std::unique_ptr<juce::DrawableButton> makeMuteButton() {
    auto button = std::make_unique<MidiLearnableDrawableButton>("Mute", juce::DrawableButton::ImageFitted);
    button->setClickingTogglesState(true);
    button->setTooltip("Mute");
    return button;
}

} // namespace detail
