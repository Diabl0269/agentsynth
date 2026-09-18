// ModuleComponentLayout.cpp -- the generic auto-layout pass: per-module-type sizing dispatch
// (updateLayout), the macro-port docked widget's size, the content-top-Y/input-column helpers,
// and the default body-content layout shared by most module cards. ModuleComponent is declared
// in ModuleComponent.h; the rest of its implementation lives in the sibling ModuleComponent*.cpp
// units next to this one (FRO65 split of the former single ModuleComponent.cpp).
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/MacroControlModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Layout/LayoutUtil.h"

using namespace detail;

void ModuleComponent::updateLayout() {
    if (isMacroPortType(getType(module))) {
        layoutMacroPortWidget();
        return;
    }

    if (getType(module) == ModuleType::Attenuverter) {
        setSize(40, 40);
        if (sliders.size() > 0) {
            sliders[0]->setBounds(0, 0, 40, 40);
            sliders[0]->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            sliders[0]->setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::yellow);
            if (sliderLabels.size() > 0)
                sliderLabels[0]->setVisible(false);
        }
        return;
    }

    if (auto* macro = dynamic_cast<MacroControlModule*>(module)) {
        // The bank is the only module whose footprint tracks a parameter: the "Knobs" count
        // decides how many macro rows (knob + output jack) are shown. It grows downward from an
        // unchanged top-left, so the module the user is turning never jumps under the cursor.
        setSize(synth::LayoutUtil::kSingleWidth, synth::LayoutUtil::macroBankHeight(macro->getMacroCount()));
        resized();
        return;
    }

    if (getType(module) == ModuleType::Sequencer || getType(module) == ModuleType::PolySequencer) {
        // +26 for the Sync to Transport toggle row — see the ADSR comment above for the pattern.
        setSize(synth::LayoutUtil::kDoubleWidth, 406);
        return;
    }

    // FRO112: ADSR no longer measures itself — its five remaining knobs (attack/hold/decay/
    // sustain/release; the three curve params moved onto the envelope graph's bend handles) flow
    // through the generic 3-per-row knob grid below exactly like every other module's, wrapping
    // into two rows (3+2) at the shared 280px width. The envelope graph section and its BPM|MS
    // row are generic-layout blocks too (see layoutDefaultContent, mirroring the scope/frequency-
    // response toggle+component pattern) rather than a bespoke branch here.

    // Parametric EQ is double-width with a bespoke band grid, so it measures itself.
    if (getType(module) == ModuleType::ParametricEQ) {
        setSize(synth::LayoutUtil::kDoubleWidth, parametricEQHeight());
        resized();
        return;
    }

    // The Wavetable card carries 15 knobs, 7 combos and 16 input jacks after issue #180, so it
    // goes double-width and uses the default body layout's wide-card branches (6 knob columns,
    // paired combos). At single width the same content would run past 1150px tall.
    const int cardWidth =
        (getType(module) == ModuleType::Wavetable) ? synth::LayoutUtil::kDoubleWidth : synth::LayoutUtil::kSingleWidth;

    // Width must be final before measuring: the slider grid wraps on it.
    if (getWidth() != cardWidth)
        setSize(cardWidth, juce::jmax(getHeight(), 100));

    const int bodyHeight = layoutDefaultContent(/*apply*/ false);
    setSize(cardWidth, std::max(100, bodyHeight));
    resized();
}

// Compact docked widget for the four macro-port types (P8-15 founder-review fix F2,
// docs/macros/ports.md#how-a-port-is-drawn): a small, fixed-shape row — no header chrome, no body, no 100px
// floor a real module card carries. Sized purely from the module's own visible jack count, which
// for a Mono/Poly-N port (or a MIDI port, no shape at all) is one row on each side (getVisible*
// PortCount()==1) and for Stereo is two (==2) — MacroInletModule/MacroOutletModule's
// declare-max/vary-visible mechanism (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed's
// implementation note) already keeps that in sync with the port's shape, so this needs no shape-aware branching of its
// own.
void ModuleComponent::layoutMacroPortWidget() {
    int rows = 1;
    if (!(module->acceptsMidi() || module->producesMidi())) {
        int numIns = 0, numOuts = 0;
        if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
            numIns = mb->getVisibleInputPortCount();
            numOuts = mb->getVisibleOutputPortCount();
        }
        rows = juce::jmax(1, numIns, numOuts);
    }
    const int height = kMacroPortWidgetHeaderY + (rows - 1) * kMacroPortWidgetRowStep + kMacroPortWidgetBottomPad;
    setSize(kMacroPortWidgetWidth, height);
}

int ModuleComponent::getContentTopY() {
    int y = 38; // below the title bar (header hairline at 24 + 8px breathing room + jack radius)
    if (module->acceptsMidi())
        y += 30; // the "Midi In"/"Midi Out" row

    int numIns = module->getTotalNumInputChannels();
    int numOuts = module->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        numIns = mb->getVisibleInputPortCount();
        numOuts = mb->getVisibleOutputPortCount();
    }

    // Ask for the real jack positions instead of recomputing them: a port label box spans
    // centre ± 10, so clear the lowest jack by a little more than that. The LAST input is not
    // necessarily the lowest once the gutter has more than one column (an odd jack count leaves
    // the second column a row short), so take the maximum over all of them.
    for (int i = 0; i < numIns; ++i)
        y = std::max(y, getPortCenter(i, true).y + kPortLabelClearance);
    if (numOuts > 0)
        y = std::max(y, getPortCenter(numOuts - 1, false).y + kPortLabelClearance);

    return y;
}

int ModuleComponent::getInputPortColumns() const {
    // Only the Wavetable card needs this today: 16 CV jacks in one column would set a ~390px
    // floor on the card height before any control is placed. Keyed off the jack count rather
    // than the type so a future high-jack module gets the same treatment for free.
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        if (mb->getVisibleInputPortCount() > 10 && getWidth() >= synth::LayoutUtil::kDoubleWidth)
            return 2;
    return 1;
}

int ModuleComponent::layoutDefaultContent(bool apply) {
    const int width = getWidth();

    // Everything here sits BELOW the last jack (getContentTopY), so the narrow gutters that used to
    // keep content clear of the port labels are unnecessary — the body gets nearly the full card
    // width, which is what makes three knobs per row fit.
    const int contentX = kContentMargin;
    const int contentW = std::max(60, width - kContentMargin * 2);

    // Single-column widgets (combos, toggles, the load row) look stretched at full width, so they
    // stay centred in a narrower band.
    const int narrowW = std::min(contentW, kNarrowContentWidth);
    const int narrowX = contentX + (contentW - narrowW) / 2;

    int y = getContentTopY();

    // --- Hosted Plugin chrome: the "Open Editor" button, the card's only body content ---
    if (openPluginEditorButton) {
        if (apply)
            openPluginEditorButton->setBounds(narrowX, y, narrowW, kRowHeight);
        y += kRowHeight + 2;
    }

    // --- Sampler chrome: waveform overview, then the load button + file-name row ---
    if (sampleWaveform) {
        if (apply)
            sampleWaveform->setBounds(contentX, y, contentW, kWaveformHeight);
        y += kWaveformHeight + 8;

        if (apply) {
            const int buttonWidth = juce::jmax(96, narrowW / 2);
            loadSampleButton->setBounds(narrowX, y, buttonWidth, kRowHeight);
            sampleNameLabel->setBounds(narrowX + buttonWidth + 6, y, narrowW - buttonWidth - 6, kRowHeight);
        }
        y += kRowHeight + 8;
    }

    // --- Wavetable chrome: the scanned frame view, the load row, then the folder browser ---
    if (wavetableDisplay != nullptr && loadWavetableButton != nullptr) {
        // The port labels only occupy a narrow gutter down each edge, so on a double-width card
        // this chrome sits BESIDE the 16-jack stack, starting just under the header, instead of
        // below all of it — the same reclaim the Parametric EQ card makes for its response
        // curve. It takes ~130px off a card that would otherwise clear 1000px tall.
        constexpr int kPortGutterWidth = 88;
        constexpr int kChromeTopY = 60;

        const bool besidePorts = width >= synth::LayoutUtil::kDoubleWidth;
        // Every input column is on the left, so the chrome has to clear all of them.
        const int inputGutter = kPortGutterWidth + (getInputPortColumns() - 1) * kPortColumnStride;
        const int chromeX = besidePorts ? (contentX + inputGutter) : contentX;
        const int chromeW = besidePorts ? std::max(120, contentW - inputGutter - kPortGutterWidth) : contentW;
        const int chromeNarrowW = std::min(chromeW, kNarrowContentWidth);
        const int chromeNarrowX = chromeX + (chromeW - chromeNarrowW) / 2;

        int chromeY = besidePorts ? kChromeTopY : y;

        if (apply)
            wavetableDisplay->setBounds(chromeX, chromeY, chromeW, kWaveformHeight);
        chromeY += kWaveformHeight + 8;

        // The Table selector belongs with the display it drives, not buried on a tab page.
        for (int i = 0; i < comboBoxes.size(); ++i) {
            if (i >= comboTabIndex.size() || comboTabIndex[i] != kTabChrome)
                continue;
            if (apply) {
                comboLabels[i]->setBounds(chromeX, chromeY, chromeW, kLabelHeight);
                comboBoxes[i]->setBounds(chromeX, chromeY + kLabelHeight, chromeW, kRowHeight);
            }
            chromeY += kLabelHeight + kRowHeight + 6;
        }

        // One button row, not two: [Load...] [Folder...] [<] [>], with the file caption on its
        // own line under them so a long wavetable name is readable instead of ellipsised.
        if (apply && wavetableFolderButton != nullptr) {
            constexpr int kStepButtonW = 28;
            constexpr int kGap = 4;
            const int stepped = (kStepButtonW + kGap) * 2;
            const int remaining = std::max(80, chromeW - stepped);
            const int loadW = remaining / 2 - kGap;
            const int folderW = remaining - loadW - kGap;

            int x = chromeX;
            loadWavetableButton->setBounds(x, chromeY, loadW, kRowHeight);
            x += loadW + kGap;
            wavetableFolderButton->setBounds(x, chromeY, folderW, kRowHeight);
            x += folderW + kGap;
            wavetablePrevButton->setBounds(x, chromeY, kStepButtonW, kRowHeight);
            x += kStepButtonW + kGap;
            wavetableNextButton->setBounds(x, chromeY, kStepButtonW, kRowHeight);
        }
        chromeY += kRowHeight + 4;

        if (apply)
            wavetableNameLabel->setBounds(chromeX, chromeY, chromeW, kLabelHeight);
        chromeY += kLabelHeight + 8;

        // Beside the ports the body still cannot start above the last jack; below them the
        // chrome simply pushes it down as before.
        y = besidePorts ? std::max(y, chromeY) : chromeY;
    }

    // A tabbed card (the Wavetable) replaces the two flat grids below with a pinned row, a tab
    // strip and one page of controls. Everything after the grids — toggles, scope — is shared.
    const bool tabbed = !wavetableTabs.isEmpty();
    if (tabbed)
        y = layoutWavetableTabs(y, contentX, contentW, apply);

    // Combos stack one per row on a standard card. A double-width card pairs them up instead —
    // otherwise a high parameter count alone would add ~180px of dead single-column height.
    const int comboColumns = (width >= synth::LayoutUtil::kDoubleWidth) ? 2 : 1;
    if (tabbed) {
        // handled per page above
    } else if (comboColumns == 1) {
        for (int i = 0; i < comboBoxes.size(); ++i) {
            if (apply) {
                comboLabels[i]->setBounds(narrowX, y, narrowW, kLabelHeight);
                comboBoxes[i]->setBounds(narrowX, y + kLabelHeight, narrowW, kRowHeight);
            }
            y += kLabelHeight + kRowHeight + 6;
        }
    } else {
        const int cellW = contentW / comboColumns;
        for (int i = 0; i < comboBoxes.size(); ++i) {
            const int row = i / comboColumns;
            const int col = i % comboColumns;
            const int cellX = contentX + col * cellW;
            const int rowY = y + row * (kLabelHeight + kRowHeight + 6);
            if (apply) {
                comboLabels[i]->setBounds(cellX, rowY, cellW - 8, kLabelHeight);
                comboBoxes[i]->setBounds(cellX, rowY + kLabelHeight, cellW - 8, kRowHeight);
            }
        }
        const int comboRows = (comboBoxes.size() + comboColumns - 1) / comboColumns;
        y += comboRows * (kLabelHeight + kRowHeight + 6);
    }

    for (int i = 0; i < toggles.size(); ++i) {
        if (apply)
            toggles[i]->setBounds(contentX, y, contentW, kRowHeight);
        y += kRowHeight + 2;
    }

    // --- Threshold control: Sample & Hold is meter-only above its rotary; ADSR / Comparator
    // embed the Threshold slider here so the slice sits on the live level bar.
    if (thresholdControl) {
        if (apply)
            thresholdControl->setBounds(contentX, y, contentW, thresholdControl->getPreferredHeight());
        y += thresholdControl->getPreferredHeight() + 6;
    }

    if (!tabbed)
        y = layoutKnobGrid(y, contentX, contentW, width, apply);

    // Envelope (ADSR) graph section: a disclosure toggle sharing its row with the BPM|MS
    // segmented control, then the curve editor itself when expanded — see
    // ModuleComponentEnvelopeCard.cpp. A no-op (returns `y` unchanged) for every other module.
    y = layoutEnvelopeGraphSection(y, contentX, contentW, apply);

    if (freqResponseToggle) {
        if (apply)
            freqResponseToggle->setBounds(contentX, y, contentW, kRowHeight);
        y += kRowHeight + 2;
    }

    if (freqResponseComponent && freqResponseComponent->isVisible()) {
        if (apply)
            freqResponseComponent->setBounds(contentX, y, contentW, 120);
        y += 128;
    }

    if (spectrumToggle && spectrumToggle->isVisible()) {
        if (apply)
            spectrumToggle->setBounds(contentX, y, contentW, kRowHeight);
        y += kRowHeight + 2;
    }

    if (scopeToggle) {
        if (apply)
            scopeToggle->setBounds(contentX, y, contentW, kRowHeight);
        y += kRowHeight + 2;
    }

    if (scopeComponent && scopeComponent->isVisible()) {
        if (apply)
            scopeComponent->setBounds(contentX, y, contentW, 100);
        y += 100;
    }

    return y + kBottomPadding;
}

int ModuleComponent::layoutKnobGrid(int y, int contentX, int contentW, int width, bool apply) {
    const int knobColumns = (width >= synth::LayoutUtil::kDoubleWidth) ? (kKnobColumns * 2) : kKnobColumns;
    const int knobWidth = contentW / knobColumns;
    for (int i = 0; i < sliders.size(); ++i) {
        const int row = i / knobColumns;
        const int col = i % knobColumns;
        const int x = contentX + col * knobWidth;
        const int rowY = y + row * (kLabelHeight + kKnobHeight);

        if (apply) {
            sliderLabels[i]->setBounds(x, rowY, knobWidth, kLabelHeight);
            sliders[i]->setBounds(x, rowY + kLabelHeight, knobWidth, kKnobHeight);
        }
    }
    const int knobRows = (sliders.size() + knobColumns - 1) / knobColumns;
    return y + knobRows * (kLabelHeight + kKnobHeight);
}
