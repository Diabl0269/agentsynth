// ModuleComponentPaint.cpp -- the Sequencer step-column layout helper, the card's paint() (ports,
// activity LED, output-card identity treatment, modulation rings), the macro-port docked widget's
// own paint, port-geometry/hit-testing (getPortCenter/getPortForPoint and friends), and resized()'s
// per-module-type dispatch. ModuleComponent is declared in ModuleComponent.h; the rest of its
// implementation lives in the sibling ModuleComponent*.cpp units next to this one (FRO65 split of
// the former single ModuleComponent.cpp).
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/MacroControlModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/SequencerModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace detail;

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

    // Multi-select state (issue #156). The theme already owns the full selected treatment
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

    int numIns = module->getTotalNumInputChannels();
    int numOuts = module->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        numIns = mb->getVisibleInputPortCount();
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

    // Inputs
    for (int i = 0; i < numIns; ++i) {
        auto p = getPortCenter(i, true);
        g.setColour(jackAccentColour);
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);

        juce::String label = "In " + juce::String(i);
        if (auto* mb = dynamic_cast<ModuleBase*>(module))
            label = mb->getInputPortLabel(i);
        else if (dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(module))
            label = (i == 0) ? "Left" : (i == 1) ? "Right" : "In " + juce::String(i);

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

    // Pending modulation drop target: ring the knob a released cable would land on, so the drop
    // is aimed rather than guessed at.
    if (mod != nullptr && modDropTargetChannel >= 0) {
        for (const auto& t : mod->getModulationTargets()) {
            if (t.channelIndex != modDropTargetChannel)
                continue;
            const int si = getModRingSliderIndex(t.name);
            if (si < 0)
                break;
            const auto b = sliders[si]->getBounds().toFloat();
            const float radius = std::min(b.getWidth(), b.getHeight()) / 2.0f - 6.0f;
            g.setColour(jackAccentColour);
            g.drawEllipse(b.getCentreX() - radius, b.getCentreY() - 10.0f - radius, radius * 2.0f, radius * 2.0f, 2.0f);
            break;
        }
    }

    // Serum-style modulation rings on knobs
    if (mod != nullptr) {
        auto targets = mod->getModulationTargets();
        const auto& modInfo = owner.getCachedModDisplayInfo();

        for (const auto& info : modInfo) {
            if (info.destNodeID != nodeId || info.isBypassed)
                continue;

            juce::String targetParamName;
            for (const auto& t : targets) {
                if (t.channelIndex == info.destChannelIndex) {
                    targetParamName = t.name;
                    break;
                }
            }
            if (targetParamName.isEmpty())
                continue;

            const int si = getModRingSliderIndex(targetParamName);
            if (si >= 0) {
                auto sliderBounds = sliders[si]->getBounds().toFloat();
                float cx = sliderBounds.getCentreX();
                float cy = sliderBounds.getCentreY() - 10.0f;
                float radius = std::min(sliderBounds.getWidth(), sliderBounds.getHeight()) / 2.0f - 11.0f;

                float baseNorm = 0.5f;
                for (auto* param : module->getParameters()) {
                    if (param->getName(100) == targetParamName) {
                        baseNorm = param->getValue();
                        break;
                    }
                }

                float modNorm = juce::jlimit(0.0f, 1.0f, baseNorm + info.modSignalValue);

                // Serum-style mod ring now drawn by the themed LnF (270° sweep + theme tokens).
                // Guarded: headless tests without our LnF simply skip the ring.
                if (lf != nullptr)
                    lf->drawModulationRing(g, {cx, cy}, radius, baseNorm, modNorm, info.modSignalValue >= 0.0f);
            }
        }
    }
}

// Compact docked port widget (P8-15 founder-review fix F2, docs/macros/ports.md#how-a-port-is-drawn): a small
// row tinted with the owning macro's colour, showing the port's own NAME (resolved live through
// GraphEditor — the name lives on synth::MacroPort, not this node, so a rename in the Configure
// I/O dialog is reflected the next time this repaints, with nothing to cache or invalidate) and
// its jack(s). Both directions of the pass-through are drawn (the port's own boundary-facing
// side, matching MacroPort::isInput — an external cable's landing point — AND the interior side
// that feeds/is-fed-by a specific member, §5.4's "a manual cable drawn after expanding the
// macro"): getPortForPoint/getPortCenter are otherwise UNCHANGED for these types (just compacted,
// see the getPortCenter branch above), so drag/drop keeps working exactly as it does for every
// other module. Only the interior jack goes unlabelled — the resolved name sits next to the
// boundary one, mirroring the collapsed card's own left/right convention (item 4).
//
// Founder-review fix G4 ("too large" — see kMacroPortWidgetWidth's own comment): the drawn jack
// shrank from a full module card's 10px dot to 7px, the corner radius from 6 to 4 and the name
// font from 10.5f to 9.5f, all sized down together with the widget's own width/height so the chip
// reads as a boundary jack rather than a miniature module. NONE of that touches the actual HIT
// target: getPortForPoint's `< 10` distance check (unchanged, general to every module) still
// grabs a click several px off the now-smaller dot — a shrunk drawn jack and a shrunk hit radius
// are two different knobs, and only the first one turned here
// (MacroPortWidgetTests.cpp's `HitTestStaysGenerousAroundTheShrunkJackDot`).
void ModuleComponent::paintMacroPortWidget(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    static const synth::theme::Colors fallbackColors{};
    const auto& themeColors = lf != nullptr ? lf->getTheme().colors : fallbackColors;

    const auto ownership = owner.macroPortOwnerFor(nodeId);
    const juce::Colour tint = ownership.macro != nullptr ? ownership.macro->colour : themeColors.accent;
    const juce::String name =
        (ownership.port != nullptr && ownership.port->name.isNotEmpty()) ? ownership.port->name : cardTitle();
    const bool boundaryIsInput = ownership.port != nullptr ? ownership.port->isInput : true;

    auto bounds = getLocalBounds().toFloat();
    g.setColour(tint.withAlpha(0.22f));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(tint.withAlpha(0.85f));
    g.drawRoundedRectangle(bounds.reduced(0.75f), 4.0f, 1.0f);

    const juce::Colour audioJackColour = themeColors.audioWire;
    const juce::Colour jackAccentColour = themeColors.accent;
    constexpr float kJackRadius = 3.5f; // 7px dot, down from a full card's 10px (fix G4)

    // T162: a port with a user colour (MacroPort::colour, set from the Configure I/O modal's swatch)
    // paints its dot in THAT colour; otherwise the kind tint (audioWire for MIDI, accent for
    // AudioCV) — the same fallback the collapsed card's MacroCardComponent::paint uses, so an
    // expanded docked widget and a collapsed card read a port's jack identically.
    const juce::Colour midiJackColour = resolveMacroPortJackColour(ownership.port, audioJackColour);
    const juce::Colour cvJackColour = resolveMacroPortJackColour(ownership.port, jackAccentColour);

    if (module->acceptsMidi() || module->producesMidi()) {
        if (module->acceptsMidi()) {
            auto p = getPortCenter(0, true);
            g.setColour(midiJackColour);
            g.fillEllipse((float)p.x - kJackRadius, (float)p.y - kJackRadius, kJackRadius * 2.0f, kJackRadius * 2.0f);
        }
        if (module->producesMidi()) {
            auto p = getPortCenter(0, false);
            g.setColour(midiJackColour);
            g.fillEllipse((float)p.x - kJackRadius, (float)p.y - kJackRadius, kJackRadius * 2.0f, kJackRadius * 2.0f);
        }
    } else {
        int numIns = 0, numOuts = 0;
        if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
            numIns = mb->getVisibleInputPortCount();
            numOuts = mb->getVisibleOutputPortCount();
        }
        for (int i = 0; i < numIns; ++i) {
            auto p = getPortCenter(i, true);
            g.setColour(cvJackColour);
            g.fillEllipse((float)p.x - kJackRadius, (float)p.y - kJackRadius, kJackRadius * 2.0f, kJackRadius * 2.0f);
        }
        for (int i = 0; i < numOuts; ++i) {
            auto p = getPortCenter(i, false);
            g.setColour(cvJackColour);
            g.fillEllipse((float)p.x - kJackRadius, (float)p.y - kJackRadius, kJackRadius * 2.0f, kJackRadius * 2.0f);
        }
    }

    g.setColour(themeColors.textPrimary);
    g.setFont(juce::Font(juce::FontOptions(9.5f)));
    // Inset 16, not G4's original 12 (P8-15 founder-review polish fix): a jack dot is drawn at
    // x=10/width-10 with a 3.5px radius, i.e. its outer edge sits at 13.5px from the widget's own
    // edge, so a 12px text inset put the name's own text area INSIDE the dot — on a Mono widget
    // showing a realistic name ("Delay 1 Audio") the left dot visibly overlapped the "D". 16 clears
    // the dot's outer edge (13.5) by 2.5px on both sides without moving the dot itself or widening
    // the widget — see MacroPortWidgetTests.cpp's `RealisticPortNameFitsWithinTheWidgetAtFullUnscaledSize`
    // for the width budget this leaves for the name.
    auto textArea = getLocalBounds().reduced(16, 2);
    // drawFittedText never draws outside textArea: it compresses the glyph run horizontally (and,
    // failing that, ellipsises) rather than clip mid-glyph — the "elide gracefully" requirement —
    // but at this widget's tuned width a realistic name draws at its natural, unscaled size (see
    // the test named above), so in practice this is a safety net, not the common case.
    g.drawFittedText(name, textArea,
                     boundaryIsInput ? juce::Justification::centredLeft : juce::Justification::centredRight, 1);
}

juce::Colour ModuleComponent::resolveMacroPortJackColour(const synth::MacroPort* port, juce::Colour kindTint) {
    // A port user colour wins when set; unset (the default, and every pre-T152 save) falls back to
    // the kind tint — a null port (a docked widget whose port entry has drifted away, which by
    // construction shouldn't happen) is exactly the same "unset", i.e. the kind tint too.
    return (port != nullptr) ? port->colour.value_or(kindTint) : kindTint;
}

std::optional<ModuleComponent::Port> ModuleComponent::getModTargetPortForPoint(juce::Point<int> localPoint) const {
    auto* mod = dynamic_cast<ModuleBase*>(module);
    if (mod == nullptr)
        return std::nullopt;

    const auto targets = mod->getModulationTargets();

    for (int si = 0; si < sliders.size(); ++si) {
        auto* slider = sliders[si];
        // A knob on an inactive tab page keeps its last bounds, so it must not swallow a drop.
        if (!slider->isVisible() || !slider->getBounds().contains(localPoint))
            continue;

        // Only rotaries are modulation targets; the ADSR's vertical sliders are not addressed
        // this way and neither is anything without a matching CV jack.
        if (slider->getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag)
            continue;

        for (const auto& t : targets) {
            if (t.name != slider->getComponentID())
                continue;
            return Port{slider->getBounds(), t.channelIndex, /*isInput*/ true, /*isMidi*/ false};
        }
    }

    if (thresholdControl != nullptr && thresholdControl->getSlider() != nullptr &&
        thresholdControl->getBounds().contains(localPoint)) {
        for (const auto& t : targets) {
            if (t.name == thresholdControl->getParamName())
                return Port{thresholdControl->getBounds(), t.channelIndex, /*isInput*/ true, /*isMidi*/ false};
        }
    }
    return std::nullopt;
}

bool ModuleComponent::setModDropTargetChannel(int channelIndex) {
    if (modDropTargetChannel == channelIndex)
        return false;
    modDropTargetChannel = channelIndex;
    repaint();
    return true;
}

int ModuleComponent::getModRingSliderIndex(const juce::String& paramName) const {
    for (int si = 0; si < sliders.size(); ++si) {
        if (sliders[si]->getComponentID() != paramName)
            continue;
        if (sliders[si]->getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag)
            continue;
        // A knob on an inactive tab page keeps the bounds it had when its page was last laid
        // out, so drawing from them paints a ring over empty card (issue #180 tab strip).
        if (!sliders[si]->isVisible())
            return -1;
        return si;
    }
    return -1;
}

juce::Point<int> ModuleComponent::getPortCenter(int index, bool isInput) {
    if (module == nullptr)
        return {0, 0};

    if (getType(module) == ModuleType::Attenuverter) {
        return {getWidth() / 2, getHeight() / 2};
    }

    // Macro-port widget (P8-15 fix F2): same left-input/right-output convention every other card
    // uses (x=10 / x=width-10, the same inset macroCardPortLayout's own collapsed-card jacks use —
    // "a macro's boundary jacks read like any other module's"), just compacted to the widget's own
    // small header offset/row step instead of a real card's 38/20. A MIDI port's single jack sits
    // fixed at the header row, mirroring the generic MIDI-jack convention below.
    if (isMacroPortType(getType(module))) {
        if (module->acceptsMidi() || module->producesMidi())
            return {isInput ? 10 : getWidth() - 10, kMacroPortWidgetHeaderY};
        int visible = 0;
        if (auto* mb = dynamic_cast<ModuleBase*>(module))
            visible = isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount();
        const int clamped = (visible > 0) ? juce::jlimit(0, visible - 1, index) : 0;
        return {isInput ? 10 : getWidth() - 10, kMacroPortWidgetHeaderY + clamped * kMacroPortWidgetRowStep};
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
        // Multi-column gutter: a 16-jack stack in one column costs ~390px of card height before a
        // single control is placed. Both columns stay on the LEFT: inputs-left / outputs-right is
        // the convention that makes signal flow read left to right, and splitting inputs across
        // both edges costs more in comprehension than the height saves. The interior column being
        // partly covered by its own module while you drag a cable at it is solved by dropping
        // straight onto the destination knob instead (see GraphEditor's mod-drop).
        const int columns = getInputPortColumns();
        if (columns > 1 && visible > 0) {
            const int rows = (visible + columns - 1) / columns;
            const int col = clamped / rows;
            const int row = clamped % rows;
            return {10 + col * kPortColumnStride, headerHeight + portOffset + row * yStep + 20};
        }
        return {10, headerHeight + portOffset + clamped * yStep + 20}; // Left side, apply offset
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

    int numIns = module->getTotalNumInputChannels();
    int numOuts = module->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        numIns = mb->getVisibleInputPortCount();
        numOuts = mb->getVisibleOutputPortCount();
    }

    // Check for MIDI Output at fixed top-right position
    if (module->producesMidi()) {
        auto p = getMidiPortCenter(true); // Matches paint()
        if (localPoint.getDistanceFrom(p) < 10) {
            return Port{{p.x - 5, p.y - 5, 10, 10},
                        juce::AudioProcessorGraph::midiChannelIndex,
                        false,
                        true}; // Index 0, Output, IsMidi
        }
    }

    // MIDI Input detection (Top Left)
    if (module->acceptsMidi()) {
        auto p = getMidiPortCenter(false); // Top left near header
        if (localPoint.getDistanceFrom(p) < 10) {
            return Port{
                {p.x - 5, p.y - 5, 10, 10}, juce::AudioProcessorGraph::midiChannelIndex, true, true}; // MIDI Input
        }
    }

    // Inputs
    for (int i = 0; i < numIns; ++i) {
        auto p = getPortCenter(i, true);
        if (localPoint.getDistanceFrom(p) < 10) {
            return Port{{p.x - 5, p.y - 5, 10, 10}, i, true, false};
        }
    }

    // Outputs (audio outputs)
    for (int i = 0; i < numOuts; ++i) {
        auto p = getPortCenter(i, false);
        if (localPoint.getDistanceFrom(p) < 10) {
            return Port{{p.x - 5, p.y - 5, 10, 10}, i, false, false};
        }
    }

    return std::nullopt;
}

void ModuleComponent::resized() {
    if (module == nullptr)
        return;

    // The compact macro-port widget (P8-15 fix F2) creates no header buttons and no body controls
    // (see the constructor's isMacroPortType guard and layoutMacroPortWidget) — nothing here needs
    // positioning.
    if (isMacroPortType(getType(module)))
        return;

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

    // FRO112: ADSR now falls through to the generic default layout below (see updateLayout()'s
    // comment) — no bespoke apply-pass branch needed here any more.

    // --- MIDI Keyboard Layout ---
    if (getType(module) == ModuleType::MidiKeyboard) {
        setSize(synth::LayoutUtil::kDoubleWidth, 150); // Appropriate size for a keyboard
        if (keyboardComponent) {
            keyboardComponent->setBounds(10, 50, getWidth() - 20, getHeight() - 60);
        }
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
    // this) and tracks the visible glyph ink far more closely than the full ascent+descent box
    // drawText centres text within: an all-caps, all-punctuation-free title (the title is upper-
    // cased + letter-spaced in drawModulePanel) never touches the descender clearance that box
    // reserves, so centring on THAT box reads slightly low against the glyphs actually on screen.
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
