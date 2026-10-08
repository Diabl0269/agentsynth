// ModuleComponentLayout.cpp -- the generic auto-layout pass: per-module-type sizing dispatch
// (updateLayout), the macro-port docked widget's size, the content-top-Y/input-column helpers,
// and the default body-content layout shared by most module cards. ModuleComponent is declared
// in ModuleComponent.h; the rest of its implementation lives in the sibling ModuleComponent*.cpp
// units next to this one.
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"
#include "Modules/MacroControlModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Layout/LayoutUtil.h"

using namespace detail;

static_assert(ModuleComponent::kMacroPortWidgetRowStep == kMacroPortRowHeight,
              "a docked port widget's jack rows are the sidebar's rows");

// A knob that moves inside the card (a layout pass, the on-card layout editor) carries a cable's knob landing with it.
// The canvas keeps its cables between frames rather than rebuilding them every tick, so it has to hear of it. Only a
// knob: anything else moving on the card (a swap ghost, a header button) lands no cable, and telling the canvas would
// rebuild every cable for it.
void ModuleComponent::childBoundsChanged(juce::Component* child) {
    if (dynamic_cast<juce::Slider*>(child) != nullptr)
        owner.notifyModuleContentChanged();
}

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

    // ADSR does not measure itself: its envelope graph is a view of the card body, its stage faders
    // and Time/Tempo switch items of the body's layout, so it takes the generic path below.

    // Parametric EQ is double-width with a bespoke band grid, so it measures itself.
    if (getType(module) == ModuleType::ParametricEQ) {
        setSize(synth::LayoutUtil::kDoubleWidth, parametricEQHeight());
        resized();
        return;
    }

    // The Wavetable card carries 15 knobs, 8 combos and 16 input jacks, so it goes double-width: its
    // chrome sits beside a two-column jack gutter and its body's runs take their wide-card branches
    // (doubled knob columns, paired combos). At single width the same content would run past 1150px.
    const int cardWidth =
        (getType(module) == ModuleType::Wavetable) ? synth::LayoutUtil::kDoubleWidth : synth::LayoutUtil::kSingleWidth;

    // Width must be final before measuring: the slider grid wraps on it.
    if (getWidth() != cardWidth)
        setSize(cardWidth, juce::jmax(getHeight(), 100));

    const int bodyHeight = layoutDefaultContent(/*apply*/ false);
    setSize(cardWidth, std::max(100, bodyHeight));
    resized();
}

// Docked widget for the four macro-port types (docs/macros/ports.md#how-a-port-is-drawn): one 16px row per jack
// row, no header chrome and no body. The row count comes from the module's own visible jack count, which for a
// Mono/Poly-N port (or a MIDI port, no shape at all) is one row on each side and for Stereo is two —
// MacroInletModule/MacroOutletModule's declare-max/vary-visible mechanism keeps that in sync with the port's shape, so
// this needs no shape-aware branching of its own. The WIDTH is the sidebar strip's and is set by
// dockMacroPortWidgets (macroHullPortLayout); before the first dock the widget gets a nominal width so it is never
// zero-sized.
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
    constexpr int kNominalWidth = kMacroCardStripWidth;
    setSize(getWidth() > 0 ? getWidth() : kNominalWidth, rows * kMacroPortWidgetRowStep);
}

int ModuleComponent::getContentTopY() {
    int y = 38; // below the title bar (header hairline at 24 + 8px breathing room + jack radius)
    if (module->acceptsMidi())
        y += 30; // the "Midi In"/"Midi Out" row

    int numOuts = module->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        numOuts = mb->getVisibleOutputPortCount();
    }

    // Clears the last DRAWN input jack, not the last VISIBLE one -- a knob-bound jack (see
    // isInputJackKnobBound) draws no gutter row at all, so a card whose only inputs are knob-bound
    // reserves no dead space here for them. Ask for the real jack positions instead of
    // recomputing them: a port label box spans centre ± 10, so clear the lowest jack by a little
    // more than that. The LAST drawn input is not necessarily the lowest once the gutter has more
    // than one column (an odd jack count leaves the second column a row short), so take the
    // maximum over all of them.
    for (int i : drawnInputJackIndices())
        y = std::max(y, getPortCenter(i, true).y + kPortLabelClearance);
    if (numOuts > 0)
        y = std::max(y, getPortCenter(numOuts - 1, false).y + kPortLabelClearance);

    return y;
}

int ModuleComponent::getInputPortColumns() const {
    // Only the Wavetable card needs this today: 16 CV jacks in one column would set a ~390px
    // floor on the card height before any control is placed. Keyed off the DRAWN jack count
    // (a knob-bound jack draws no gutter row, so it must not count towards this threshold)
    // rather than the type so a future high-jack module gets the same treatment for free.
    if (getWidth() >= synth::LayoutUtil::kDoubleWidth)
        if ((int)drawnInputJackIndices().size() > 10)
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

    // --- Hosted Plugin chrome: the Open Editor / Edit Layout... row (no-op on every other module) ---
    y = layoutHostedPluginChrome(y, narrowX, narrowW, apply);

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

    // --- Wavetable chrome: the scanned frame view, the Table selector, the load row, the file caption ---
    if (wavetableDisplay != nullptr && loadWavetableButton != nullptr)
        y = layoutWavetableChrome(y, contentX, contentW, apply);

    y = layoutBodyControls(y, width, apply);

    // LFO custom-waveform section: the Grid/Shapes/Tools toolbar, then the curve editor
    // itself while shape == Custom -- see ModuleComponentLfoCard.cpp. A no-op for every other
    // module (and for an LFO not currently on the Custom shape).
    y = layoutLfoCustomWaveSection(y, contentX, contentW, apply);

    y = layoutChromeRows(y, contentX, contentW, apply);

    // The folded More row sits at the very bottom, under the footer rows.
    if (cardBody_ != nullptr)
        y = cardBody_->layoutMoreRow(y, synth::cardbody::BodyGeometry::forCardWidth(width), apply);

    return y + kBottomPadding;
}

// The Show Response / Spectrum / Scope rows and the panels they open. On a card whose layout has a footer
// row, the toggles are pills in that row instead, under the open panels.
int ModuleComponent::layoutChromeRows(int y, int contentX, int contentW, bool apply) {
    const bool footer = cardBody_ != nullptr && cardBody_->hasFooter();
    if (envelopeGraphToggle && !footer) {
        if (apply)
            envelopeGraphToggle->setBounds(contentX, y, contentW, kRowHeight);
        y += kRowHeight + 2;
    }

    if (freqResponseToggle && !footer) {
        if (apply)
            freqResponseToggle->setBounds(contentX, y, contentW, kRowHeight);
        y += kRowHeight + 2;
    }

    // The panels below fade in and out and take their height in step (blockHeight), so what is under them slides.
    if (freqResponseComponent && freqResponseComponent->isVisible()) {
        if (apply)
            freqResponseComponent->setBounds(contentX, y, contentW, blockHeight(responseFade_, 120));
        y += blockHeight(responseFade_, 128);
    }

    if (spectrumToggle && spectrumToggle->isVisible() && !footer) {
        if (apply)
            spectrumToggle->setBounds(contentX, y, contentW, blockHeight(responseFade_, kRowHeight));
        y += blockHeight(responseFade_, kRowHeight + 2);
    }

    if (scopeToggle && !footer) {
        if (apply)
            scopeToggle->setBounds(contentX, y, contentW, kRowHeight);
        y += kRowHeight + 2;
    }

    if (scopeComponent && scopeComponent->isVisible()) {
        if (apply)
            scopeComponent->setBounds(contentX, y, contentW, blockHeight(scopeFade_, 100));
        y += blockHeight(scopeFade_, 100);
    }

    if (footer)
        y = cardBody_->layoutFooter(y, synth::cardbody::BodyGeometry::forCardWidth(getWidth()), apply,
                                    footerChromeToggles(/*visibleOnly*/ true));
    return y;
}

std::vector<juce::ToggleButton*> ModuleComponent::footerChromeToggles(bool visibleOnly) const {
    std::vector<juce::ToggleButton*> toggles;
    for (auto* toggle : {envelopeGraphToggle.get(), freqResponseToggle.get(), spectrumToggle.get(), scopeToggle.get()})
        if (toggle != nullptr && (!visibleOnly || toggle->isVisible()))
            toggles.push_back(toggle);
    return toggles;
}

// The folded More row opens under a modulation cable, so a hidden parameter's knob can still take it.
bool ModuleComponent::unfoldMoreRowForCableDrag(juce::Point<int> localPoint) {
    return cardBody_ != nullptr && cardBody_->unfoldForCableDragAt(localPoint);
}

// A card body lays out its own sections. A card without one (External MIDI's combos, a hosted plugin's
// slots) stacks its widget arrays in the generic order -- combos, toggles, knobs -- with the same runs.
int ModuleComponent::layoutBodyControls(int y, int width, bool apply) {
    using namespace synth::cardbody;
    const auto g = BodyGeometry::forCardWidth(width);
    if (cardBody_ != nullptr)
        return cardBody_->layout(y, g, apply);

    std::vector<CaptionedWidget> combos, knobs;
    for (int i = 0; i < comboBoxes.size(); ++i)
        combos.emplace_back(comboBoxes[i], comboLabels[i]);
    for (int i = 0; i < sliders.size(); ++i)
        knobs.emplace_back(sliders[i], sliderLabels[i]);
    std::vector<juce::Component*> toggleRow(toggles.begin(), toggles.end());
    y = layoutChoiceRun(combos, y, g, apply);
    y = layoutToggleRun(toggleRow, y, g, apply);
    return layoutKnobRun(knobs, kKnobColumns, y, g, apply);
}

namespace {

// The Wavetable chrome's rows, top to bottom; its stack starts kWavetableChromeTopY down a double-width
// card (just under the header, beside the jack gutter).
constexpr int kWavetableChromeTopY = 60;
constexpr int kWavetableDisplayStep = kWaveformHeight + 8;
constexpr int kWavetableTableStep = kLabelHeight + kRowHeight + 6;
constexpr int kWavetableLoadRowStep = kRowHeight + 4;
constexpr int kWavetableCaptionStep = kLabelHeight + 8;

} // namespace

int ModuleComponent::wavetableChromeBottomY() {
    return kWavetableChromeTopY + kWavetableDisplayStep + kWavetableTableStep + kWavetableLoadRowStep +
           kWavetableCaptionStep;
}

// The port labels only occupy a narrow gutter down each edge, so on a double-width card this chrome sits
// BESIDE the jack stack, starting just under the header, instead of below all of it -- the same reclaim
// the Parametric EQ card makes for its response curve. The Table selector belongs with the display it
// drives. One button row, not two: [Load...] [Folder...] [<] [>], with the file caption on its own line
// under them so a long wavetable name is readable instead of ellipsised. Beside the ports the body still
// cannot start above the last jack; below them the chrome simply pushes it down.
int ModuleComponent::layoutWavetableChrome(int y, int contentX, int contentW, bool apply) {
    constexpr int kPortGutterWidth = 88;
    const bool besidePorts = getWidth() >= synth::LayoutUtil::kDoubleWidth;
    // Every input column is on the left, so the chrome has to clear all of them.
    const int inputGutter = kPortGutterWidth + (getInputPortColumns() - 1) * kPortColumnStride;
    const int chromeX = besidePorts ? (contentX + inputGutter) : contentX;
    const int chromeW = besidePorts ? std::max(120, contentW - inputGutter - kPortGutterWidth) : contentW;
    int chromeY = besidePorts ? kWavetableChromeTopY : y;

    if (apply)
        wavetableDisplay->setBounds(chromeX, chromeY, chromeW, kWaveformHeight);
    chromeY += kWavetableDisplayStep;

    // The Table selector, built before the body's combos (createWavetableTableSelector).
    if (apply && !comboParams.isEmpty() && comboParams[0] != nullptr && comboParams[0]->paramID == "table") {
        comboLabels[0]->setBounds(chromeX, chromeY, chromeW, kLabelHeight);
        comboBoxes[0]->setBounds(chromeX, chromeY + kLabelHeight, chromeW, kRowHeight);
    }
    chromeY += kWavetableTableStep;

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
    chromeY += kWavetableLoadRowStep;

    if (apply)
        wavetableNameLabel->setBounds(chromeX, chromeY, chromeW, kLabelHeight);
    chromeY += kWavetableCaptionStep;

    return besidePorts ? std::max(y, chromeY) : chromeY;
}
