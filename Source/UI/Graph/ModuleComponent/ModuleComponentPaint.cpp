// ModuleComponentPaint.cpp -- the Sequencer step-column layout helper, the card's paint() (ports,
// activity LED, output-card identity treatment, modulation rings), the macro-port docked widget's
// own paint, port-geometry/hit-testing (getPortCenter/getPortForPoint and friends), and resized()'s
// per-module-type dispatch. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units next to this one.
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/MacroControlModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/SequencerModule.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;

namespace {

// The controls a modulation target lands on: every rotary knob and a card fader. Any other linear
// slider (a sequencer gate, the Threshold view's) is not addressed this way.
bool showsModulation(const juce::Slider& slider) {
    return slider.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag ||
           dynamic_cast<const synth::ui::CardFader*>(&slider) != nullptr;
}

} // namespace

void ModuleComponent::layoutSequencerStepColumn(int step, int colX, int startY) {
    // Gate slider (row 0)
    juce::String gateId = "Gate " + juce::String(step);
    for (int i = 0; i < sliders.size(); ++i) {
        if (sliders[i]->getComponentID().equalsIgnoreCase(gateId)) {
            sliderLabels[i]->setBounds(colX, startY, 55, 20);
            sliders[i]->setBounds(colX, startY + 20, 55, 50);
        }
    }

    // Pitch / Root slider (row 1) — Sequencer uses "Pitch N", PolySequencer uses "Step N Root"
    juce::String pitchId = "Pitch " + juce::String(step);
    juce::String rootId = "Step " + juce::String(step) + " Root";
    for (int i = 0; i < sliders.size(); ++i) {
        if (sliders[i]->getComponentID().equalsIgnoreCase(pitchId) ||
            sliders[i]->getComponentID().equalsIgnoreCase(rootId)) {
            sliderLabels[i]->setBounds(colX, startY + 80, 55, 20);
            sliders[i]->setBounds(colX, startY + 100, 55, 50);
        }
    }

    // F.Env / Chord combo (row 2) — Sequencer uses "F.Env N" slider; PolySequencer uses "Step N Chord" combo
    juce::String fEnvId = "F.Env " + juce::String(step);
    for (int i = 0; i < sliders.size(); ++i) {
        if (sliders[i]->getComponentID().equalsIgnoreCase(fEnvId)) {
            sliderLabels[i]->setBounds(colX, startY + 160, 55, 20);
            sliders[i]->setBounds(colX, startY + 180, 55, 50);
        }
    }

    juce::String chordId = "Step " + juce::String(step) + " Chord";
    for (int i = 0; i < comboBoxes.size(); ++i) {
        if (comboBoxes[i]->getName().equalsIgnoreCase(chordId)) {
            if (i < comboLabels.size())
                comboLabels[i]->setBounds(colX, startY + 160, 55, 20);
            comboBoxes[i]->setBounds(colX, startY + 180, 55, 24);
        }
    }
}

void ModuleComponent::paint(juce::Graphics& g) {
    if (module == nullptr)
        return;

    if (getType(module) == ModuleType::Attenuverter) {
        return; // Transparent background, no ports, no header
    }

    if (isMacroPortType(getType(module))) {
        paintMacroPortWidget(g);
        return; // compact docked widget — no header, no generic port loop, no body
    }

    auto* mod = dynamic_cast<ModuleBase*>(module);
    bool isBypassed = mod && mod->isBypassed();

    // Guarded LnF cast: headless tests construct this component WITHOUT installing our LnF.
    // When the cast is null we fall back to a plain themed-ish fill so those tests don't crash.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    // Multi-select state. The theme already owns the full selected treatment
    // (accent border + glow); this just supplies the flag it was always waiting for.
    const bool isSelected = owner.isNodeSelected(nodeId);

    if (lf != nullptr) {
        // Single owner of card treatment: background, drop shadow, body fill, border, and the
        // header band (filled + title drawn) all come from the active theme.
        lf->drawModulePanel(g, getLocalBounds().toFloat(), kHeaderHeight, cardTitle(), isSelected, isBypassed);
    } else {
        // Fallback path (no themed LnF): plain fill + simple header so tests render without crashing.
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        g.setColour(isSelected ? juce::Colours::aqua : juce::Colours::black);
        g.drawRect(getLocalBounds(), 2);
        g.setColour(juce::Colours::darkgrey);
        g.fillRect(0, 0, getWidth(), kHeaderHeight);
        g.setColour(juce::Colours::white);
        g.drawText(cardTitle(), 0, 0, getWidth(), kHeaderHeight, juce::Justification::centred, true);
    }

    // Drop-target highlight while an audio file hovers over a Sampler.
    if (fileDragHighlight) {
        auto dropColour = (lf != nullptr) ? lf->getTheme().colors.accent : juce::Colours::yellow;
        g.setColour(dropColour.withAlpha(0.12f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 24.0f);
        g.setColour(dropColour);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 24.0f, 2.0f);
    }

    // Highlight Active Step (Sequencer only) — recolored from theme accent when available.
    if (getType(module) == ModuleType::Sequencer) {
        if (auto* seq = dynamic_cast<SequencerModule*>(module)) {
            int activeStep = seq->currentActiveStep.load();
            // Coordinates match resized()
            int startX = 10;
            int stepWidth = 60;
            int x = startX + activeStep * stepWidth;

            auto stepColour =
                (lf != nullptr) ? lf->getTheme().colors.accent.withAlpha(0.3f) : juce::Colours::yellow.withAlpha(0.3f);
            g.setColour(stepColour);
            g.fillRect(x, 110, stepWidth - 5, 220); // Cover Gate+Pitch+F.Env area
        }
    }

    // Activity LED in header — recolored to the theme's success token when available.
    if (cachedRMS > 0.01f && !isBypassed) {
        float ledAlpha = juce::jlimit(0.3f, 1.0f, cachedRMS * 2.0f);
        auto ledColour = (lf != nullptr) ? lf->getTheme().colors.success : juce::Colours::limegreen;
        g.setColour(ledColour.withAlpha(ledAlpha));
        g.fillEllipse(6.0f, 8.0f, 8.0f, 8.0f);
    }

    // Output-card identity treatment (user request: "a better way to represent the output module
    // and its destinations"). Audio Output is a bare juce::AudioGraphIOProcessor and otherwise
    // renders through the exact same generic path as every card above — this is the one additive
    // block that gives it its own identity. The glyph reuses the activity LED's slot: Audio Output
    // is never a ModuleBase, so it never has a VisualBuffer and cachedRMS above stays 0.0f forever
    // for this card, leaving that slot permanently dark otherwise. The destination line is pushed
    // in by GraphEditor::refreshOutputDeviceInfo (MainComponent -> GraphEditor -> here) whenever
    // AudioEngine's device state changes; an empty string (no device open yet, headless build, or
    // Hosted mode with nothing to report) means "draw no line" rather than an empty one.
    if (isAudioOutputIONode(module)) {
        if (lf != nullptr) {
            if (auto ioIcon = lf->getIcon(synth::theme::Icon::CatIO)) {
                // Owned clone (not peekIcon's shared view): retinted below to the TITLE's colour,
                // not the library sidebar's fixed textMuted, so glyph + title read as one lockup —
                // this must never touch the shared IconLibrary cache other cards/rows also read.
                const auto& c = lf->getTheme().colors;
                const auto titleColour = isSelected ? c.accent : (isBypassed ? c.textDisabled : c.textPrimary);
                ioIcon->replaceColour(c.textMuted, titleColour); // c.textMuted is retintIcons()'s baked-in tint
                ioIcon->drawWithin(g, outputCardIconBoundsForTest(*lf), juce::RectanglePlacement::centred, 1.0f);
            }
        }
        if (outputDeviceInfoText.isNotEmpty()) {
            auto mutedColour = (lf != nullptr) ? lf->getTheme().colors.textMuted : juce::Colours::grey;
            g.setColour(mutedColour);
            g.setFont(juce::Font(juce::FontOptions((lf != nullptr) ? lf->getTheme().type.micro : 9.0f)));
            g.drawFittedText(outputDeviceInfoText, kContentMargin, 27, getWidth() - kContentMargin * 2, 14,
                             juce::Justification::centredLeft, 1);
        }
    }

    // --- PORTS ---
    // Theme-derived jack/label colors (guarded: headless tests have no themed LnF).
    // Audio-signal jacks (MIDI in/out) -> audioWire; mod-capable input/output jacks -> accent;
    // port labels -> textMuted. Geometry is unchanged.
    juce::Colour jackAccentColour = (lf != nullptr) ? lf->getTheme().colors.accent : juce::Colours::yellow;
    juce::Colour audioJackColour = (lf != nullptr) ? lf->getTheme().colors.audioWire : juce::Colours::white;
    juce::Colour labelColour = (lf != nullptr) ? lf->getTheme().colors.textMuted : juce::Colours::white;

    int numOuts = module->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        numOuts = mb->getVisibleOutputPortCount();
    }
    bool midiOutDrawn = false; // Reintroduced
                               // MIDI Output (Top Right if produces midi)
    if (module->producesMidi()) {
        g.setColour(audioJackColour);
        auto p = getMidiPortCenter(true);
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);
        g.setColour(labelColour);
        g.drawText("Midi Out", p.x - 65, p.y - 5, 60, 10, juce::Justification::right, false);
    }
    // MIDI Input (Top Left if accepts midi components)
    if (module->acceptsMidi()) {
        g.setColour(audioJackColour);
        auto p = getMidiPortCenter(false);
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);
        g.setColour(labelColour);
        g.drawText("Midi In", p.x + 10, p.y - 5, 60, 10, juce::Justification::left, false);
    }

    // Inputs -- a knob-bound jack (isInputJackKnobBound) draws no gutter dot/label at all;
    // its cable lands on the knob's own ring instead (see the landing-dot paint in
    // GraphEditorCables.cpp's paintOverChildren). drawnInputJackIndices() is the single list of
    // which raw indices remain.
    for (int i : drawnInputJackIndices()) {
        auto p = getPortCenter(i, true);
        g.setColour(jackAccentColour);
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);

        juce::String label = "In " + juce::String(i);
        if (auto* mb = dynamic_cast<ModuleBase*>(module))
            label = mb->getInputPortLabel(i);
        else if (dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(module)) {
            // Audio Output's Left is labelled "L / Mono" -- it is the jack that normals,
            // borrowing Left onto Right at render time for as long as Right stays unpatched (see
            // ModuleComponent::getTooltip() for the jack-hover explanation). Audio Input has no
            // normalling of its own, so its own Left/Right keep the plain labels.
            const bool isNormallingLeft = i == 0 && isAudioOutputIONode(module);
            label = isNormallingLeft ? "L / Mono" : (i == 0) ? "Left" : (i == 1) ? "Right" : "In " + juce::String(i);
        }

        g.setColour(labelColour);
        g.drawText(label, p.x + 10, p.y - 10, 60, 20, juce::Justification::left, false);
    }

    // Outputs
    // Only draw audio outputs if MIDI out hasn't been drawn in the same general area (to prevent overlap)
    // For now, we assume MIDI out takes the "first" audio output slot.
    // A more robust solution would involve explicit port mapping.
    int audioOutStartIndex = midiOutDrawn ? 1 : 0;
    for (int i = audioOutStartIndex; i < numOuts + audioOutStartIndex;
         ++i) { // Adjust index for display if midi out is present
        auto p = getPortCenter(i, false);
        g.setColour(jackAccentColour);
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);

        juce::String label = "Out " + juce::String(i);
        if (auto* mb = dynamic_cast<ModuleBase*>(module))
            label = mb->getOutputPortLabel(i);
        else if (dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(module))
            label = (i == 0) ? "Left" : (i == 1) ? "Right" : "Out " + juce::String(i);
        g.setColour(labelColour);
        g.drawText(label, p.x - 70, p.y - 10, 60, 20, juce::Justification::right, false);
    }

    paintModulationRings(g, mod, jackAccentColour);

    // MIDI-mapped badges + the armed-control breathing outline, drawn last so they sit on
    // top of every knob/toggle/combo/header button (ModuleComponentMidiLearn.cpp).
    paintMidiLearnOverlays(g);
}

// Compact docked port widget (docs/macros/ports.md#how-a-port-is-drawn): a small
// row tinted with the owning macro's colour, showing the port's own NAME (resolved live through
// GraphEditor — the name lives on synth::MacroPort, not this node, so a rename in the Configure
// I/O dialog is reflected the next time this repaints, with nothing to cache or invalidate) and
// its jack(s). Both directions of the pass-through are drawn (the port's own boundary-facing
// side, matching MacroPort::isInput — an external cable's landing point — AND the interior side
// that feeds/is-fed-by a specific member, docs/macros/ports.md#cable-rendering-across-the-boundary's "a manual cable
// drawn after expanding the macro"): getPortForPoint/getPortCenter are otherwise UNCHANGED for these types (just
// compacted, see the getPortCenter branch above), so drag/drop keeps working exactly as it does for every other module.
// Only the interior jack goes unlabelled — the resolved name sits next to the boundary one, mirroring the collapsed
// card's own left/right convention.
//
// The strip behind the widget is painted by the canvas (paintMacroPortStrips), so the widget draws only its jacks and
// its name. The boundary jack (the one on the hull border, where cables from outside land) is a full 10px dot like the
// collapsed card's; the interior jack (on the strip's inner edge, where cables to members start) is a 7px dot. NONE of
// that touches the HIT target: getPortForPoint's `< 10` distance check still grabs a click several px off the smaller
// dot (MacroPortWidgetTests.cpp's `HitTestStaysGenerousAroundTheShrunkJackDot`). The name is painted only at working
// zoom; below it the strip shows dots and the tooltip carries the name.
void ModuleComponent::paintMacroPortWidget(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    static const synth::theme::Colors fallbackColors{};
    const auto& themeColors = lf != nullptr ? lf->getTheme().colors : fallbackColors;

    const auto ownership = owner.getMacroController().macroPortOwnerFor(nodeId);
    const juce::String name =
        (ownership.port != nullptr && ownership.port->name.isNotEmpty()) ? ownership.port->name : cardTitle();
    const bool boundaryIsInput = ownership.port != nullptr ? ownership.port->isInput : true;

    // A port with a user colour (MacroPort::colour, set from the Configure I/O modal's swatch)
    // paints its dot in THAT colour; otherwise the kind tint (audioWire for MIDI, accent for
    // AudioCV) — the same fallback the collapsed card's MacroCardComponent uses, so an expanded
    // docked widget and a collapsed card read a port's jack identically.
    const juce::Colour midiJackColour = effectiveMacroPortJackColour(ownership.port, themeColors.audioWire);
    const juce::Colour cvJackColour = effectiveMacroPortJackColour(ownership.port, themeColors.accent);

    auto dot = [&](int index, bool isInput, juce::Colour colour) {
        const bool isBoundary = isInput == boundaryIsInput;
        const float radius = isBoundary ? 5.0f : 3.5f;
        const auto p = getPortCenter(index, isInput);
        const float alpha = isBoundary ? 1.0f : macroPortNameAlpha(); // the interior dot melts into the boundary one
        if (alpha <= 0.0f)
            return;
        g.setColour(colour.withMultipliedAlpha(alpha));
        g.fillEllipse((float)p.x - radius, (float)p.y - radius, radius * 2.0f, radius * 2.0f);
    };

    if (module->acceptsMidi() || module->producesMidi()) {
        if (module->acceptsMidi())
            dot(0, true, midiJackColour);
        if (module->producesMidi())
            dot(0, false, midiJackColour);
    } else {
        int numIns = 0, numOuts = 0;
        if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
            numIns = mb->getVisibleInputPortCount();
            numOuts = mb->getVisibleOutputPortCount();
        }
        for (int i = 0; i < numIns; ++i)
            dot(i, true, cvJackColour);
        for (int i = 0; i < numOuts; ++i)
            dot(i, false, cvJackColour);
    }

    const float nameAlpha = macroPortNameAlpha();
    if (nameAlpha <= 0.0f)
        return;
    g.setColour(themeColors.textPrimary.withMultipliedAlpha(nameAlpha));
    g.setFont(juce::Font(juce::FontOptions(kMacroPortNameFontSize)));
    // The name starts kMacroPortStripInset from the boundary edge and stops short of the interior
    // jack; a name that does not fit is shortened with an ellipsis, never squeezed. First row only.
    g.drawFittedText(name, macroPortNameArea(boundaryIsInput),
                     boundaryIsInput ? juce::Justification::centredLeft : juce::Justification::centredRight, 1, 1.0f);
}

float ModuleComponent::macroPortNameAlpha() const {
    // The parent is the canvas content component, whose transform is the zoom (scale + pan).
    const float zoom = getParentComponent() != nullptr ? getParentComponent()->getTransform().getScaleFactor() : 1.0f;
    return macroPortNameAlphaAtZoom(zoom);
}

bool ModuleComponent::macroPortBoundaryIsOutput() const {
    const auto ownership = owner.getMacroController().macroPortOwnerFor(nodeId);
    return ownership.port != nullptr && !ownership.port->isInput;
}

int ModuleComponent::macroPortJackX(bool isInput) const {
    const int x = isInput ? kMacroPortWidgetJackInset : getWidth() - kMacroPortWidgetJackInset;
    const auto ownership = owner.getMacroController().macroPortOwnerFor(nodeId);
    if (ownership.port == nullptr || isInput == ownership.port->isInput)
        return x; // the boundary jack, or a widget with no owning port yet
    const int boundaryX = ownership.port->isInput ? kMacroPortWidgetJackInset : getWidth() - kMacroPortWidgetJackInset;
    return juce::roundToInt(macroPortInteriorJackX((float)boundaryX, (float)x, macroPortNameAlpha()));
}

juce::Rectangle<int> ModuleComponent::macroPortNameArea(bool boundaryIsInput) const {
    constexpr int kInteriorClearance = kMacroPortWidgetJackInset + 8;
    auto textArea = juce::Rectangle<int>(0, 0, getWidth(), kMacroPortWidgetRowStep);
    if (boundaryIsInput)
        return textArea.withTrimmedLeft(kMacroPortStripInset).withTrimmedRight(kInteriorClearance);
    return textArea.withTrimmedLeft(kInteriorClearance).withTrimmedRight(kMacroPortStripInset);
}

bool ModuleComponent::macroPortNameIsTruncated(const juce::String& name, bool boundaryIsInput) const {
    const juce::Font font{juce::FontOptions(kMacroPortNameFontSize)};
    return font.getStringWidthFloat(name) > (float)macroPortNameArea(boundaryIsInput).getWidth();
}

juce::Colour ModuleComponent::effectiveMacroPortJackColour(const synth::MacroPort* port, juce::Colour kindTint) const {
    // The armed preview wins (one picked colour drives both a MIDI and a CV jack); else the stored
    // colour, else the kind tint -- exactly what resolveMacroPortJackColour yields.
    if (portColourPreview_.has_value())
        return *portColourPreview_;
    return resolveMacroPortJackColour(port, kindTint);
}

juce::Colour ModuleComponent::resolveMacroPortJackColour(const synth::MacroPort* port, juce::Colour kindTint) {
    // A port user colour wins when set; unset (the default, and every older save) falls back to
    // the kind tint — a null port (a docked widget whose port entry has drifted away, which by
    // construction shouldn't happen) is exactly the same "unset", i.e. the kind tint too.
    return (port != nullptr) ? port->colour.value_or(kindTint) : kindTint;
}

std::optional<ModuleComponent::Port> ModuleComponent::getModTargetPortForPoint(juce::Point<int> localPoint) const {
    auto* mod = dynamic_cast<ModuleBase*>(module);
    if (mod == nullptr)
        return std::nullopt;

    const auto targets = mod->getModulationTargets();

    // Resolve each target to ITS knob (bound parameter first, jack label as the fallback) rather
    // than scanning knobs for a label match: only the bound lookup finds "Rate (Hz)" for "Rate".
    // Only knobs and card faders are modulation targets (showsModulation); other sliders are not addressed this way
    // and neither is anything without a matching CV jack. A knob on an unselected tab keeps its
    // last bounds, so a hidden one (sliderIndexForModTarget says -1) must not swallow a drop.
    for (const auto& t : targets) {
        const int si = sliderIndexForModTarget(t);
        if (si >= 0 && sliders[si]->getBounds().contains(localPoint))
            return Port{sliders[si]->getBounds(), t.channelIndex, /*isInput*/ true, /*isMidi*/ false};
    }

    if (thresholdControl != nullptr && thresholdControl->getSlider() != nullptr &&
        thresholdControl->getBounds().contains(localPoint)) {
        for (const auto& t : targets) {
            if (knobNameForModTarget(mod, t) == thresholdControl->getParamName())
                return Port{thresholdControl->getBounds(), t.channelIndex, /*isInput*/ true, /*isMidi*/ false};
        }
    }
    return std::nullopt;
}

// The componentID the card gave the knob a target drives: its bound parameter's display name
// (ModuleComponent::createControls sets every slider's componentID to param->getName(100)), or
// the jack label itself when the target binds to no parameter — the pre-paramId behaviour, kept
// so a module that never sets paramId and whose labels ARE its knob names keeps working unchanged.
juce::String ModuleComponent::knobNameForModTarget(const ModuleBase* mod, const ModulationTarget& target) {
    if (mod != nullptr)
        if (const auto* param = mod->parameterForModTarget(target))
            return param->getName(100);
    return target.name;
}

// Resolves the knob by the target's BOUND parameter (ModuleBase::parameterForModTarget) -- the widget
// bound to that paramId, never a display-name match -- so a jack labelled "Rate" finds the "Rate (Hz)"
// knob. A target with no bound parameter falls back to the knob named like the jack. The visibility rule
// is getModRingSliderIndex's either way.
int ModuleComponent::sliderIndexForModTarget(const ModulationTarget& target) const {
    const auto* mb = dynamic_cast<const ModuleBase*>(module);
    if (const auto* param = mb != nullptr ? mb->parameterForModTarget(target) : nullptr) {
        const int si = sliderParams.indexOf(const_cast<juce::RangedAudioParameter*>(param));
        return si >= 0 ? shownRingSliderIndex(si) : -1;
    }
    return getModRingSliderIndex(target.name);
}

// See the doc comment on the declaration (ModuleComponent.h) -- this is the ONE place a
// cable's landing point is computed, shared by GraphEditor's cable re-anchor pass
// (GraphEditorModHover.cpp) so a click near the drawn ring always hits the cable that lands there.
// The anchor sits just OUTSIDE the ring's own arc rather than on it -- at the same
// rotary-start angle (norm 0.0f, lower-left), radius pushed out by half the ring's stroke width
// (clearing the drawn arc itself) + half the landing dot's own diameter (so the dot's edge, not
// its centre, clears the arc) + a 2px gap, so the dot and the arc never visually overlap. Falls
// back to the default theme's metrics when there is no themed LnF (headless tests) -- the same
// guarded-cast pattern paint() uses.
// half the ring's own stroke width (clears the drawn arc itself) + half the landing dot's
// own diameter (so the dot's EDGE, not its centre, clears the arc) + a 2px gap -- the amount the
// landing radius is pushed OUTWARD from the ring's plain radius. A free function of the LnF alone
// (never the knob's bounds), so both a CARD-local caller (getModTargetKnobAnchor) and a
// KNOB-local one (wantsCablePickupGestureFor's hit-test, which compares against a MouseEvent
// delivered to the knob itself, not the card) can add it to whichever radius they already have in
// their own frame, rather than only being expressible in one specific frame.
float ModuleComponent::knobLandingRadiusOffset() const {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    static const synth::theme::Metrics fallbackMetrics{};
    const float ringStroke = (lf != nullptr) ? lf->getTheme().metrics.knobRingWidth : fallbackMetrics.knobRingWidth;
    constexpr float kLandingGap = 2.0f;
    return ringStroke * 0.5f + kKnobLandingDotDiameter * 0.5f + kLandingGap;
}

std::optional<juce::Point<float>> ModuleComponent::getModTargetKnobAnchor(int destChannel) const {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    if (mb == nullptr)
        return std::nullopt;
    for (const auto& target : mb->getModulationTargets()) {
        if (target.channelIndex != destChannel)
            continue;
        const int si = sliderIndexForModTarget(target);
        // A knob in a tab section never takes its jack's cable: its tab can be switched away, and
        // a jack that moved between the gutter and the knob on every tab click would move the
        // card's jack layout with it. Its jack stays in the gutter; a drop on the knob still lands.
        if (si < 0 || (cardBody_ != nullptr && cardBody_->isTabbed(*sliders[si])))
            return std::nullopt;
        // CARD-local: sliders[si]->getBounds() is relative to this card (its parent), matching
        // this method's own CARD-local contract (see the declaration's comment).
        if (auto* fader = dynamic_cast<synth::ui::CardFader*>(sliders[si]))
            return fader->getPosition().toFloat() + fader->landingPoint(kKnobLandingDotDiameter);
        const auto sliderBounds = sliders[si]->getBounds().toFloat();
        const auto centre = modRingCentreFor(*sliders[si], sliderBounds);
        const float landingRadius = modRingRadiusFor(sliderBounds) + knobLandingRadiusOffset();
        return modRingPointForNorm(centre, landingRadius, 0.0f);
    }
    return std::nullopt;
}

// True when visible input jack `index` is a ModulationTarget whose knob resolves on this
// card RIGHT NOW -- recomputed live off getModulationTargets()/mapInputChannel()/
// sliderIndexForModTarget() every call (poly toggle and Dual I/O change what resolves; a knob in
// a tab section never binds, getModTargetKnobAnchor), never cached across a layout. A module with no ModulationTarget
// mapping to `index` (an audio/pitch/gate/MIDI jack, or a CV jack with no bound knob, e.g. Oscillator's Pitch CV) is
// never knob-bound, matching the plain gutter behaviour exactly.
bool ModuleComponent::isInputJackKnobBound(int index) const { return knobAnchorForVisibleInputJack(index).has_value(); }

// Shared by isInputJackKnobBound (the "is it hidden" question) and getPortCenter's redirect
// branch (the "where does it land" question) so the two can never resolve a different target for
// the same visible index -- see the declaration's comment (ModuleComponent.h).
std::optional<juce::Point<float>> ModuleComponent::knobAnchorForVisibleInputJack(int index) const {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    if (mb == nullptr)
        return std::nullopt;
    for (const auto& target : mb->getModulationTargets()) {
        if (mb->mapInputChannel(target.channelIndex).visibleJackIndex != index)
            continue;
        return getModTargetKnobAnchor(target.channelIndex);
    }
    return std::nullopt;
}

// paint()'s input loop, getPortForPoint()'s input loop, and getInputPortColumns() all read
// THIS list rather than re-deriving "which jacks are hidden" each their own way, so they can never
// disagree about what's actually on screen. Never cached across a call -- isInputJackKnobBound
// recomputes live off current slider visibility every time (poly toggle, Dual I/O, the More row).
std::vector<int> ModuleComponent::drawnInputJackIndices() const {
    std::vector<int> drawn;
    if (module == nullptr)
        return drawn;
    int visible = 0;
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        visible = mb->getVisibleInputPortCount();
    else
        visible = module->getTotalNumInputChannels(); // no ModuleBase (e.g. Audio Input/Output) -- never knob-bound
    drawn.reserve(visible);
    for (int i = 0; i < visible; ++i)
        if (!isInputJackKnobBound(i))
            drawn.push_back(i);
    return drawn;
}

bool ModuleComponent::setModDropTargetChannel(int channelIndex) {
    if (modDropTargetChannel == channelIndex)
        return false;
    modDropTargetChannel = channelIndex;
    repaint();
    return true;
}

int ModuleComponent::getModRingSliderIndex(const juce::String& paramName) const {
    for (int si = 0; si < sliders.size(); ++si)
        if (sliders[si]->getComponentID() == paramName && showsModulation(*sliders[si]))
            return shownRingSliderIndex(si);
    return -1;
}

// A knob hidden on an unselected tab (or in a folded More row) keeps the bounds it had when last
// laid out, so drawing from them paints a ring over empty card -- UNLESS a layout's swap group shows a
// sibling in its cell (CardBody::isSwappedOut, the ADSR's stage time swapped for its tempo division
// included): that keeps the SAME cell a jack still legitimately lands on, so the jack never falls back to
// the gutter and the card never grows on a swap.
int ModuleComponent::shownRingSliderIndex(int si) const {
    if (!showsModulation(*sliders[si]))
        return -1;
    if (sliders[si]->isVisible())
        return si;
    return cardBody_ != nullptr && cardBody_->isSwappedOut(*sliders[si]) ? si : -1;
}

juce::Point<int> ModuleComponent::getPortCenter(int index, bool isInput) {
    if (module == nullptr)
        return {0, 0};

    if (getType(module) == ModuleType::Attenuverter) {
        return {getWidth() / 2, getHeight() / 2};
    }

    // Macro-port widget: same left-input/right-output convention every other card uses, but the jacks
    // sit kMacroPortWidgetJackInset in from the widget's own edge (the boundary jack lands on the
    // hull border) and on 16px rows. A MIDI port's single jack sits fixed at the first row.
    if (isMacroPortType(getType(module))) {
        const int x = macroPortJackX(isInput);
        if (module->acceptsMidi() || module->producesMidi())
            return {x, kMacroPortWidgetHeaderY};
        int visible = 0;
        if (auto* mb = dynamic_cast<ModuleBase*>(module))
            visible = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
        const int clamped = (visible > 0) ? juce::jlimit(0, visible - 1, index) : 0;
        return {x, kMacroPortWidgetHeaderY + clamped * kMacroPortWidgetRowStep};
    }

    // Macro bank: jacks sit on their macro's row so knob N and jack N line up horizontally.
    if (auto* macro = dynamic_cast<MacroControlModule*>(module)) {
        if (!isInput) {
            const int visible = macro->getVisibleOutputPortCount();
            const int clamped = (visible > 0) ? juce::jlimit(0, visible - 1, index) : 0;
            return {getWidth() - 10, synth::LayoutUtil::macroRowCentreY(clamped)};
        }
    }

    int yStep = 20;
    int headerHeight = kPortGutterHeaderHeight;

    int portOffset = 0;
    if (module->producesMidi()) {
        portOffset = 20; // Additional offset for all ports if MIDI out is present, to avoid collision with MIDI Out at
                         // (getWidth() - 10, 38)
    }

    // Clamp index to visible jack range so wires never terminate at a phantom y
    // below the module. In-bounds indices are unchanged (clamped == index).
    int visible = 0;
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        visible = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
    } else {
        visible = isInput ? module->getTotalNumInputChannels() : module->getTotalNumOutputChannels();
    }
    int clamped = (visible > 0) ? juce::jlimit(0, visible - 1, index) : 0;

    if (isInput) {
        // A knob-bound jack (its ModulationTarget resolves to a visible knob on this card)
        // draws no gutter dot at all -- a cable/routing that names it by this same visible index
        // (portPos in GraphEditorCables.cpp calls this exact function) lands on the knob's own
        // ring-landing anchor instead. This is the ONE place that redirect happens, so every
        // caller -- paint(), getPortForPoint(), and every cable kind's endpoint resolution -- gets
        // it for free without knowing knob-hiding exists.
        if (auto anchor = knobAnchorForVisibleInputJack(clamped))
            return anchor->roundToInt();

        // Not knob-bound: its drawn ROW is its rank among the other non-knob-bound jacks, not its
        // raw visible index -- a knob-bound jack ahead of it in the index order takes no row, so
        // the column packs with no gap where that jack would have sat.
        const auto drawn = drawnInputJackIndices();
        int packedIndex = 0;
        for (int d : drawn) {
            if (d == clamped)
                break;
            ++packedIndex;
        }
        const int drawnCount = (int)drawn.size();

        // Multi-column gutter: a 16-jack stack in one column costs ~390px of card height before a
        // single control is placed. Both columns stay on the LEFT: inputs-left / outputs-right is
        // the convention that makes signal flow read left to right, and splitting inputs across
        // both edges costs more in comprehension than the height saves. The interior column being
        // partly covered by its own module while you drag a cable at it is solved by dropping
        // straight onto the destination knob instead (see GraphEditor's mod-drop).
        const int columns = getInputPortColumns();
        if (columns > 1 && drawnCount > 0) {
            const int rows = (drawnCount + columns - 1) / columns;
            const int col = packedIndex / rows;
            const int row = packedIndex % rows;
            return {10 + col * kPortColumnStride, headerHeight + portOffset + row * yStep + 20};
        }
        return {10, headerHeight + portOffset + packedIndex * yStep + 20}; // Left side, apply offset
    } else {
        // No additional midiOffset for outputs here, as MIDI out is now fixed.
        return {getWidth() - 10, headerHeight + portOffset + clamped * yStep + 20}; // Right side, apply offset
    }
}

std::optional<ModuleComponent::Port> ModuleComponent::getPortForPoint(juce::Point<int> localPoint) {
    if (module == nullptr)
        return std::nullopt;

    if (getType(module) == ModuleType::Attenuverter) {
        return std::nullopt; // Users cannot manually drag connections from the smart wire knob
    }

    int numOuts = module->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        numOuts = mb->getVisibleOutputPortCount();
    }

    auto hitOutputs = [&]() -> std::optional<Port> {
        if (module->producesMidi()) {
            auto p = getMidiPortCenter(true); // Matches paint()
            if (localPoint.getDistanceFrom(p) < 10)
                return Port{{p.x - 5, p.y - 5, 10, 10}, juce::AudioProcessorGraph::midiChannelIndex, false, true};
        }
        for (int i = 0; i < numOuts; ++i) {
            auto p = getPortCenter(i, false);
            if (localPoint.getDistanceFrom(p) < 10)
                return Port{{p.x - 5, p.y - 5, 10, 10}, i, false, false};
        }
        return std::nullopt;
    };
    // Inputs -- a knob-bound jack is never hit-tested here at all (it draws no gutter dot
    // to click); the knob claims that click via CardKnobSlider's own gesture wiring instead
    // (wireCardKnobModAmountGesture / wantsCablePickupGestureFor, ModuleComponent.cpp).
    auto hitInputs = [&]() -> std::optional<Port> {
        if (module->acceptsMidi()) {
            auto p = getMidiPortCenter(false); // Top left near header
            if (localPoint.getDistanceFrom(p) < 10)
                return Port{{p.x - 5, p.y - 5, 10, 10}, juce::AudioProcessorGraph::midiChannelIndex, true, true};
        }
        for (int i : drawnInputJackIndices()) {
            auto p = getPortCenter(i, true);
            if (localPoint.getDistanceFrom(p) < 10)
                return Port{{p.x - 5, p.y - 5, 10, 10}, i, true, false};
        }
        return std::nullopt;
    };
    // A docked macro-port widget zoomed out draws its two jacks as one dot: the press picks the boundary side.
    const bool outputsFirst = isMacroPortType(getType(module)) && macroPortBoundaryIsOutput();
    if (auto hit = outputsFirst ? hitOutputs() : hitInputs())
        return hit;
    if (auto hit = outputsFirst ? hitInputs() : hitOutputs())
        return hit;

    return std::nullopt;
}

// Audio Output's "L / Mono" jack (and its Right sibling) are the only jacks with a
// hover explanation today, so this stays a small special case against isAudioOutputIONode rather
// than a general per-jack tooltip table -- getPortForPoint() already gives the exact same hit-test
// paint()'s jack dots use, so the tooltip always agrees with what is drawn. Empty for every other
// jack/module, so a control's own setTooltip() (juce::SettableTooltipClient) is unaffected;
// juce::TooltipWindow only calls this while the mouse is actually over this component.
juce::String ModuleComponent::getTooltip() {
    // A docked port widget's name fades in with zoom; while it is faded, or when it is ellipsised, the tooltip
    // carries the full name.
    if (module != nullptr && isMacroPortType(getType(module))) {
        const auto ownership = owner.getMacroController().macroPortOwnerFor(nodeId);
        if (ownership.port == nullptr)
            return {};
        if (macroPortNameAlpha() >= 1.0f && !macroPortNameIsTruncated(ownership.port->name, ownership.port->isInput))
            return {};
        return ownership.port->name;
    }
    if (module == nullptr || !isAudioOutputIONode(module))
        return {};
    const auto port = getPortForPoint(getMouseXYRelative());
    if (!port.has_value() || !port->isInput || port->isMidi)
        return {};
    if (port->index == 0)
        return "L / Mono - normals to Right while Right is unpatched";
    if (port->index == 1)
        return "Borrows Left while unpatched";
    return {};
}

void ModuleComponent::resized() {
    if (module == nullptr)
        return;

    // The compact macro-port widget creates no header buttons and no body controls
    // (see the constructor's isMacroPortType guard and layoutMacroPortWidget) — nothing here needs
    // positioning.
    if (isMacroPortType(getType(module)))
        return;
    syncPortAccessibility();

    // Header icon buttons: delete (rightmost) → bypass → mute → Dual I/O (when present).
    // Attenuverter path: all four are null → no-op.
    if (deleteButton)
        deleteButton->setBounds(getWidth() - 26, 2, 22, 20);

    if (bypassButton)
        bypassButton->setBounds(getWidth() - 50, 2, 22, 20);

    if (muteButton)
        muteButton->setBounds(getWidth() - 74, 2, 22, 20);

    if (dualIOButton)
        dualIOButton->setBounds(getWidth() - 98, 2, 22, 20);

    if (auto* macro = dynamic_cast<MacroControlModule*>(module)) {
        layoutMacroBank(macro->getMacroCount());
        return;
    }

    if (getType(module) == ModuleType::ParametricEQ) {
        layoutParametricEQ();
        return;
    }

    if (getType(module) == ModuleType::Sequencer) {
        // --- Sequencer Specific Layout ---
        int x = 10;
        int y = 30;

        // Top Row: Run and BPM
        for (auto* toggle : toggles) {
            if (toggle->getComponentID().equalsIgnoreCase("run")) {
                toggle->setBounds(x + 30, y, 60, 24); // Add margin
                x += 70;
            }
        }

        for (int i = 0; i < sliders.size(); ++i) {
            if (sliders[i]->getComponentID().equalsIgnoreCase("bpm")) {
                sliderLabels[i]->setBounds(x + 20, y, 60, 20); // Add margin
                sliders[i]->setBounds(x + 20, y + 20, 60, 50);
                x += 70;
            }
        }

        // Steps Row: 8 columns starting at (10, 110), each 60px wide
        const int startX = 10;
        const int startY = 110;
        const int stepWidth = 60;

        for (int step = 1; step <= 8; ++step) {
            int colX = startX + (step - 1) * stepWidth;
            layoutSequencerStepColumn(step, colX, startY);
        }

        // Sync to Transport toggle: a 26px row appended below the step grid.
        for (auto* toggle : toggles) {
            if (toggle->getComponentID().equalsIgnoreCase("Sync to Transport"))
                toggle->setBounds(startX, 380, 200, 24);
        }

        return;
    }

    if (getType(module) == ModuleType::PolySequencer) {
        // --- PolySequencer Specific Layout ---
        // Run toggle + BPM in header row (y=30), then 8 step columns at y=110.
        // Step N params: "Gate N" (slider), "Step N Root" (slider), "Step N Chord" (combo).
        int x = 10;
        int y = 30;

        for (auto* toggle : toggles) {
            if (toggle->getComponentID().equalsIgnoreCase("run")) {
                toggle->setBounds(x + 30, y, 60, 24);
                x += 70;
            }
        }

        for (int i = 0; i < sliders.size(); ++i) {
            if (sliders[i]->getComponentID().equalsIgnoreCase("bpm")) {
                sliderLabels[i]->setBounds(x + 20, y, 60, 20);
                sliders[i]->setBounds(x + 20, y + 20, 60, 50);
                x += 70;
            }
        }

        // Steps Row: 8 columns starting at (10, 110), each 60px wide
        const int startX = 10;
        const int startY = 110;
        const int stepWidth = 60;

        for (int step = 1; step <= 8; ++step) {
            int colX = startX + (step - 1) * stepWidth;
            layoutSequencerStepColumn(step, colX, startY);
        }

        // Sync to Transport toggle: a 26px row appended below the step grid.
        for (auto* toggle : toggles) {
            if (toggle->getComponentID().equalsIgnoreCase("Sync to Transport"))
                toggle->setBounds(startX, 380, 200, 24);
        }

        return;
    }

    // ADSR now falls through to the generic default layout below (see updateLayout()'s
    // comment) — no bespoke apply-pass branch needed here any more.

    // --- MIDI Keyboard Layout ---
    if (getType(module) == ModuleType::MidiKeyboard) {
        layoutMidiKeyboardCard(); // the Octave row and the keys; ModuleComponentMidiKeyboardCard.cpp
        return;
    }

    // --- Default Layout ---
    layoutDefaultContent(/*apply*/ true);
}

void ModuleComponent::refreshPortLayout() {
    if (module == nullptr)
        return;

    updateLayout();
    owner.handleModuleResized(this);
    repaint();
}

void ModuleComponent::setOutputDeviceInfoText(const juce::String& text) {
    // A no-op on every module except Audio Output: the card layout never changes (this only
    // affects a paint-time text draw), so unlike refreshPortLayout above there is nothing to
    // re-measure — just invalidate the cached image if the text actually changed.
    if (module == nullptr || !isAudioOutputIONode(module) || outputDeviceInfoText == text)
        return;

    outputDeviceInfoText = text;
    repaint();
}

juce::Rectangle<float> ModuleComponent::outputCardIconBoundsForTest(const synth::theme::AppLookAndFeel& lf) {
    // Mirrors the title's own font — theme.type.h2, bold — set in AppLookAndFeel::drawModulePanel.
    const juce::Font titleFont(juce::FontOptions(lf.getTheme().type.h2, juce::Font::bold));

    // JUCE exposes no direct cap-height accessor. 0.72x ascent is the standard sans-serif
    // approximation (Inter — embedded for every UI face here, see docs/layout/theming.md — sits close
    // this) and tracks the visible capital/x-height glyph ink far more closely than the full
    // ascent+descent box drawText centres text within: most of a title sits above the descender
    // clearance that box reserves, so centring on THAT box reads slightly low against the glyphs
    // actually on screen.
    const float capHeight = titleFont.getAscent() * 0.72f;

    // Header band geometry, copied from AppLookAndFeel::drawModulePanel (body = bounds.reduced(2);
    // header = body.withHeight(24), passed down from paint() below as literal 24): the same
    // vertically-centred text-box math drawText itself uses, so this converges on the same
    // baseline drawText would place the title on.
    constexpr float kHeaderTop = 2.0f;
    constexpr float kHeaderHeight = 24.0f;
    const float textBoxTop = kHeaderTop + (kHeaderHeight - titleFont.getHeight()) * 0.5f;
    const float baseline = textBoxTop + titleFont.getAscent();
    const float capCentreY = baseline - capHeight * 0.5f;

    // Sized to the title's cap-height (not the full header band) and right-aligned to the activity
    // LED's own right edge — fillEllipse(6, 8, 8, 8) a few lines below, right edge at x=14 — so the
    // gap to the title's left inset (22, see drawModulePanel) stays the same 8px the LED's absence
    // already reserves, whatever the icon's resulting width turns out to be.
    constexpr float kIconRightEdge = 14.0f;
    return juce::Rectangle<float>(kIconRightEdge - capHeight, capCentreY - capHeight * 0.5f, capHeight, capHeight);
}
