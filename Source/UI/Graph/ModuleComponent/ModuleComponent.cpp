// ModuleComponent.cpp -- construction/teardown of a graph node's card, per-scope header/theme
// helpers, and the auto-UI control builder (createControls). ModuleComponent is declared in
// ModuleComponent.h; the rest of its implementation lives in the sibling ModuleComponent*.cpp
// units next to this one (FRO65 split of the former single ModuleComponent.cpp).
#include "ModuleComponent.h"
#include "AudioEngine/AudioEngine.h"
#include "CardKnobSlider.h"
#include "ModuleComponentHostedPluginCard.h"
#include "ModuleComponentInternal.h"
#include "Modules/ExternalMidiModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/PolySequencerModule.h"
#include "Modules/SequencerModule.h"
#include "Modules/ThresholdMeterSource.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

using namespace detail;

namespace {

// Every module's float-param sliders are rotary knobs. ADSR's five (attack/hold/decay/sustain/
// release — FRO112 moved its three curve params onto the envelope graph's bend handles instead,
// see createControls()) put their text box ABOVE the dial rather than below, so the numeric
// readout reads as a value sitting over its knob rather than a caption under it; every other
// module keeps the readout below, matching the label above.
void setAdsrAwareSliderStyle(juce::Slider& slider, ModuleType type) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(type == ModuleType::ADSR ? juce::Slider::TextBoxAbove : juce::Slider::TextBoxBelow, false,
                           50, 20);
}

// attack/hold/decay/release: the four ADSR TIME params. Their AudioParameterFloat range is
// deliberately linear (see docs/modules/modules.md#adsr-envelope-module) -- a skewed NormalisableRange there would
// badly worsen AIStateMapper's untrusted in-[0,1] rescale heuristic for AI-authored patches. Sustain and the three
// curve params are excluded on purpose and stay linear on the slider too.
bool isAdsrTimeParamId(const juce::String& paramID) {
    return paramID == "attack" || paramID == "hold" || paramID == "decay" || paramID == "release";
}

// Gives an ADSR time slider the 0.3 skew that keeps it usable at the new 1 ms attack default,
// without touching the parameter's own (linear) range. MUST run AFTER the slider's
// SliderParameterAttachment is constructed: that constructor installs a NormalisableRange<double>
// built from lambda convertFrom0to1/convertTo0to1 functions, and NormalisableRange::convertFrom0to1
// returns via that function early whenever one is set -- its own `skew` field is never consulted
// once a lambda is installed, so a plain Slider::setSkewFactor() call before or after the
// attachment is a silent no-op. Replacing the slider's range with a plain (function-free),
// already-skewed NormalisableRange<double> here restores a real pixel<->value skew curve while
// leaving slider.getValue()/setValue() -- what the attachment reads and writes -- exchanging real
// units exactly as before.
void applyAdsrTimeSliderSkew(juce::Slider& slider, const juce::AudioParameterFloat& param) {
    if (!isAdsrTimeParamId(param.paramID))
        return;
    const auto& r = param.getNormalisableRange();
    slider.setNormalisableRange(
        juce::NormalisableRange<double>((double)r.start, (double)r.end, (double)r.interval, 0.3));
}

// True for a float param createControls()'s generic auto-slider loop must NOT build a knob for:
// the threshold slider (it lives inside ThresholdControlComponent instead), or -- ADSR only,
// FRO112 -- the three curve amounts (edited only via the envelope graph's bend handles).
bool shouldSkipGenericFloatSlider(juce::AudioProcessor* module, const juce::AudioParameterFloat& floatParam) {
    if (auto* src = dynamic_cast<ThresholdMeterSource*>(module))
        if (getType(module) != ModuleType::SampleHold && floatParam.paramID == src->getThresholdParamID())
            return true;
    return getType(module) == ModuleType::ADSR &&
           (floatParam.paramID == "attackCurve" || floatParam.paramID == "decayCurve" ||
            floatParam.paramID == "releaseCurve");
}

// True for a bool param createControls()'s generic auto-toggle loop must NOT build a toggle for,
// beyond the fixed bypassed/muted/dualIO trio: ADSR's tempoSync (FRO113), which the envelope
// card's own MS|BPM segmented buttons already expose and drive (FRO117).
bool shouldSkipGenericBoolToggle(juce::AudioProcessor* module, const juce::AudioParameterBool& boolParam) {
    return getType(module) == ModuleType::ADSR && boolParam.paramID == "tempoSync";
}

// True for a choice param createControls()'s generic auto-combo loop must NOT build a combo for:
// ADSR's four note-division params (FRO113's attackDiv/holdDiv/decayDiv/releaseDiv). They have no
// UI of their own yet (tracked separately, FRO118) but must not leak into the generic per-param
// grid in the meantime -- each one otherwise renders an extra combo+label row nothing here uses.
bool shouldSkipGenericChoiceCombo(juce::AudioProcessor* module, const juce::AudioParameterChoice& choiceParam) {
    return getType(module) == ModuleType::ADSR &&
           (choiceParam.paramID == "attackDiv" || choiceParam.paramID == "holdDiv" ||
            choiceParam.paramID == "decayDiv" || choiceParam.paramID == "releaseDiv");
}

} // namespace

juce::Point<int> ModuleComponent::getMidiPortCenter(bool isOutput) const {
    return {isOutput ? getWidth() - 10 : 10, midiJackY(module)};
}

ModuleComponent::ModuleComponent(juce::AudioProcessor* m, juce::AudioProcessorGraph::NodeID nId, GraphEditor& owner,
                                 AppUndoManager* undoMgr)
    : module(m)
    , nodeId(nId)
    , owner(owner)
    , undoManager(undoMgr) {

    showContextMenuHook_ = [](juce::PopupMenu& menu) { menu.showMenuAsync(juce::PopupMenu::Options()); };

    if (auto* modBase = dynamic_cast<ModuleBase*>(module)) {
        if (auto* vb = modBase->getVisualBuffer()) {
            // Parametric EQ keeps its VisualBuffer for the spectrum analyser's FFT, but a scope
            // on top of that analyser is redundant clutter, so it gets no scope UI. A macro-port
            // widget (P8-15 fix F2) enables a VisualBuffer too (for a future activity LED — none
            // of the four types draw one today), but "no body" (item 1) rules out a scope toggle
            // here as firmly as it rules out bypass/mute/delete.
            if (getType(module) != ModuleType::ExternalMidi && getType(module) != ModuleType::ParametricEQ &&
                !isMacroPortType(getType(module))) {
                scopeComponent = std::make_unique<ScopeComponent>(*vb);
                addAndMakeVisible(scopeComponent.get());

                scopeToggle = std::make_unique<juce::ToggleButton>("Show Scope");
                scopeToggle->setToggleState(false, juce::dontSendNotification);
                scopeComponent->setVisible(false);
                scopeToggle->onClick = [this] {
                    scopeComponent->setVisible(scopeToggle->getToggleState());
                    updateLayout();
                };
                addAndMakeVisible(scopeToggle.get());
            }
        }
    }

    if (auto* src = dynamic_cast<ThresholdMeterSource*>(module)) {
        juce::AudioParameterFloat* thresholdParam = nullptr;
        // Sample & Hold keeps its rotary Threshold; the control is meter-only there. ADSR and
        // Comparator embed the slider in the control so the slice sits on the live level bar.
        if (getType(module) != ModuleType::SampleHold)
            thresholdParam =
                dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(module, src->getThresholdParamID()));
        thresholdControl = std::make_unique<ThresholdControlComponent>(*src, thresholdParam);
        addAndMakeVisible(thresholdControl.get());
    }

    if (auto* filterMod = dynamic_cast<FilterModule*>(module)) {
        freqResponseComponent = std::make_unique<FrequencyResponseComponent>(*filterMod);
        addAndMakeVisible(freqResponseComponent.get());

        // Same pattern as the scope: hidden by default so a Filter card does not pay for a
        // 30 Hz response/spectrum timer until the user asks for it.
        freqResponseToggle = std::make_unique<juce::ToggleButton>("Show Response");
        freqResponseToggle->setToggleState(false, juce::dontSendNotification);
        freqResponseComponent->setVisible(false);
        freqResponseToggle->onClick = [this] {
            const bool show = freqResponseToggle->getToggleState();
            freqResponseComponent->setVisible(show);
            if (spectrumToggle != nullptr) {
                spectrumToggle->setVisible(show);
                if (!show) {
                    spectrumToggle->setToggleState(false, juce::dontSendNotification);
                    freqResponseComponent->setShowSpectrum(false);
                }
            }
            updateLayout();
        };
        addAndMakeVisible(freqResponseToggle.get());

        spectrumToggle = std::make_unique<juce::ToggleButton>("Show Spectrum");
        spectrumToggle->setToggleState(false, juce::dontSendNotification);
        spectrumToggle->onClick = [this] { freqResponseComponent->setShowSpectrum(spectrumToggle->getToggleState()); };
        addChildComponent(spectrumToggle.get()); // hidden until the response view is shown
    }

    if (auto* eqMod = dynamic_cast<ParametricEQModule*>(module)) {
        eqCurveComponent = std::make_unique<EQCurveComponent>(*eqMod);
        wireEqGestureCallbacks(*eqCurveComponent);
        addAndMakeVisible(eqCurveComponent.get());

        // The spectrum is this curve's backdrop, so it starts on (the analyser gates itself on
        // actual signal, so an idle patch still costs no repaints).
        spectrumToggle = std::make_unique<juce::ToggleButton>("Show Spectrum");
        spectrumToggle->setToggleState(eqCurveComponent->getShowSpectrum(), juce::dontSendNotification);
        spectrumToggle->onClick = [this] { eqCurveComponent->setShowSpectrum(spectrumToggle->getToggleState()); };
        addAndMakeVisible(spectrumToggle.get());

        eqPopOutButton = std::make_unique<juce::TextButton>("Open EQ Window");
        eqPopOutButton->setComponentID("eqPopOut");
        eqPopOutButton->setTooltip("Edit this EQ in a larger resizable window");
        eqPopOutButton->onClick = [this] { openEqWindow(); };
        addAndMakeVisible(eqPopOutButton.get());
    }

    // Attenuverter has no header at all; a macro-port widget (P8-15 fix F2) has no header CHROME —
    // "no module header chrome and no body" — so neither gets bypass/mute/delete/Dual I/O buttons.
    if (getType(module) != ModuleType::Attenuverter && !isMacroPortType(getType(module))) {
        // MidiLearnableDrawableButton (FRO130): plain juce::DrawableButton fires its click on a
        // RIGHT click too (Button::mouseDown/mouseUp have no isPopupMenu() guard), which would
        // toggle bypass/mute/Dual I/O before the addMouseListener(this) below ever sees the press.
        bypassButton =
            std::make_unique<detail::MidiLearnableDrawableButton>("Bypass", juce::DrawableButton::ImageFitted);
        bypassButton->setClickingTogglesState(true);
        bypassButton->setTooltip("Bypass");
        addAndMakeVisible(*bypassButton);
        bypassButton->addMouseListener(this, false);

        muteButton = std::make_unique<detail::MidiLearnableDrawableButton>("Mute", juce::DrawableButton::ImageFitted);
        muteButton->setClickingTogglesState(true);
        muteButton->setTooltip("Mute");
        addAndMakeVisible(*muteButton);
        muteButton->addMouseListener(this, false);

        deleteButton = std::make_unique<juce::DrawableButton>("Delete", juce::DrawableButton::ImageFitted);
        deleteButton->setTooltip("Delete module");
        deleteButton->onClick = [this] { this->owner.requestDeleteModule(this->nodeId); };
        addAndMakeVisible(*deleteButton);

        if (auto* mb = dynamic_cast<ModuleBase*>(module); mb != nullptr && mb->hasDualIOParameter()) {
            dualIOButton =
                std::make_unique<detail::MidiLearnableDrawableButton>("Dual I/O", juce::DrawableButton::ImageFitted);
            dualIOButton->setClickingTogglesState(true);
            updateDualIOTooltip();
            addAndMakeVisible(*dualIOButton);
            dualIOButton->addMouseListener(this, false);
        }
    }

    createSamplerControls();
    createWavetableControls();

    setTitle(module->getName());
    // Buffered to image (docs/layout/rendering.md) through our own cache so a zoom gesture can pin the
    // raster scale — see ZoomFrozenCachedImage. Do NOT add setBufferedToImage() back anywhere on
    // this component: JUCE asserts if a custom cache is already installed (juce_Component.cpp:567).
    {
        auto cache = std::make_unique<synth::ui::ZoomFrozenCachedImage>(*this);
        rasterCache = cache.get();
        setCachedComponentImage(cache.release()); // Component takes ownership
    }
    createControls();
    // ADSR only (createEnvelopeCardControls() no-ops the componentID rename otherwise); after
    // createControls() so its five remaining knobs (sliders/sliderLabels) already exist to
    // shorten and read.
    if (getType(module) == ModuleType::ADSR) {
        applyEnvelopeKnobShortLabels();
        createEnvelopeCardControls();
    }
    createWavetableTabs(); // after createControls(): it groups the sliders/combos that call made
    applyHeaderButtonIcons();
    startTimerHz(15); // 15 FPS is plenty for activity glow / step indicator; lower CPU than 30
}

ModuleComponent::~ModuleComponent() { detachFromProcessor(); }

void ModuleComponent::detachFromProcessor() {
    stopTimer();
    setVisible(false);

    // Hosted Plugin card: leave the module's observers and unbind every hosted parameter listener FIRST,
    // while the instance is still alive (the module frees it once its node goes).
    releaseHostedPluginCard();

    // Destroy scope component first — it has its own timer reading from the module's VisualBuffer
    scopeComponent.reset();
    scopeToggle.reset();
    // The pop-out EQ editor holds the module by reference and runs its own timer, so it must be
    // torn down before the processor goes away. The dialog is self-owning; deleting it closes it.
    if (eqWindow != nullptr)
        delete eqWindow.getComponent();

    // Same for the frequency-domain views: their 30 Hz timers poll the module for parameter
    // values and FFT samples, so they must go before the module pointer is dropped.
    eqCurveComponent.reset();
    freqResponseComponent.reset();
    freqResponseToggle.reset();
    spectrumToggle.reset();
    eqPopOutButton.reset();
    openPluginEditorButton.reset();
    keyboardComponent.reset();
    // Same reasoning: the threshold control times itself and holds a reference to the module.
    thresholdControl.reset();

    // Same reason: the waveform view times against the SamplerModule, so it must go before the
    // processor pointer is dropped.
    sampleWaveform.reset();
    loadSampleButton.reset();
    sampleNameLabel.reset();
    sampleChooser.reset();

    // Same reason: the wavetable display holds a module reference and its own timer, and the
    // load button's onClick lambda reaches back into this component.
    wavetableDisplay.reset();
    loadWavetableButton.reset();
    wavetableChooser.reset();

    if (auto* parent = getParentComponent())
        parent->removeChildComponent(this);

    // Destroy attachments ONLY if the processor is still alive (node exists in graph).
    // During undo, graph.clear() may have already freed the processor and its parameters.
    // If the processor is gone, release ownership to avoid use-after-free in ~ParameterAttachment.
    bool processorAlive = false;
    if (module != nullptr) {
        for (auto* node : owner.getAudioEngine().getGraph().getNodes()) {
            if (node->getProcessor() == module) {
                processorAlive = true;
                break;
            }
        }
    }
    if (processorAlive) {
        bypassAttachment.reset();
        muteAttachment.reset();
        dualIOAttachment.reset();
        sliderAttachments.clear();
        comboAttachments.clear();
        buttonAttachments.clear();
    } else {
        // Processor already freed — leak attachments to avoid use-after-free
        // in ~ParameterAttachment which calls parameter->removeListener()
        (void)bypassAttachment.release();
        (void)muteAttachment.release();
        (void)dualIOAttachment.release();
        while (sliderAttachments.size() > 0)
            (void)sliderAttachments.removeAndReturn(sliderAttachments.size() - 1);
        while (comboAttachments.size() > 0)
            (void)comboAttachments.removeAndReturn(comboAttachments.size() - 1);
        while (buttonAttachments.size() > 0)
            (void)buttonAttachments.removeAndReturn(buttonAttachments.size() - 1);
    }

    if (module == nullptr)
        return;

    if (auto* node = owner.getAudioEngine().getGraph().getNodeForId(nodeId)) {
        for (auto* param : node->getProcessor()->getParameters())
            param->removeListener(this);
    }

    module = nullptr;
}
void ModuleComponent::applyHeaderButtonIcons() {
    // Headless-safe: when our themed LnF is not installed (unit tests), buttons remain blank
    // (no image set). The DrawableButton still exists and functions correctly without an image.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;

    if (bypassButton) {
        if (auto d = lf->getIcon(synth::theme::Icon::ModuleBypass))
            bypassButton->setImages(d.get());
    }
    if (muteButton) {
        if (auto d = lf->getIcon(synth::theme::Icon::ModuleMute))
            muteButton->setImages(d.get());
    }
    if (deleteButton) {
        if (auto d = lf->getIcon(synth::theme::Icon::ModuleDelete))
            deleteButton->setImages(d.get());
    }
    if (dualIOButton) {
        if (auto d = lf->getIcon(synth::theme::Icon::ModuleDualIO))
            dualIOButton->setImages(d.get());
    }
}

void ModuleComponent::refreshWaveformComboIcons() {
    using synth::theme::AppLookAndFeel;
    using synth::theme::Icon;

    auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
    // Headless / no themed LnF: nothing to refresh.
    if (lf == nullptr)
        return;

    const Icon kIcons[4] = {Icon::WaveformSine, Icon::WaveformSquare, Icon::WaveformSaw, Icon::WaveformTriangle};
    const juce::StringArray kChoices{"Sine", "Square", "Saw", "Triangle"};

    for (auto* combo : comboBoxes) {
        // Detect waveform combos by the same 4-element choice set used in createControls().
        // We probe the root menu: it must have exactly 4 items with the right IDs and text.
        const juce::PopupMenu* root = combo->getRootMenu();
        if (root == nullptr)
            continue;

        // Count items and check texts match the waveform set.
        bool isWaveform = true;
        int itemCount = 0;
        {
            juce::PopupMenu::MenuItemIterator it(*root, false);
            while (it.next()) {
                const auto& item = it.getItem();
                if (itemCount >= 4 || item.text != kChoices[itemCount]) {
                    isWaveform = false;
                    break;
                }
                ++itemCount;
            }
            if (itemCount != 4)
                isWaveform = false;
        }
        if (!isWaveform)
            continue;

        // Save the current selection BEFORE rebuilding so we can restore it.
        const int savedId = combo->getSelectedId();

        // Rebuild the root menu items with freshly tinted icon clones.
        // getRootMenu() returns a const pointer; clear via the combo's non-const accessor.
        combo->clear(juce::dontSendNotification);
        for (int i = 0; i < 4; ++i) {
            std::unique_ptr<juce::Drawable> icon = lf->getIcon(kIcons[i]); // may be nullptr in headless
            combo->getRootMenu()->addItem(i + 1, kChoices[i], true, false, std::move(icon));
        }

        // Restore selection without notifying listeners — the ComboBoxParameterAttachment
        // is NOT disturbed (it listens on parameterValueChanged, not on the combo's onChange
        // when dontSendNotification is passed), so no spurious parameter change fires.
        combo->setSelectedId(savedId, juce::dontSendNotification);
    }
}

void ModuleComponent::applyKeyboardThemeColours() {
    if (keyboardComponent == nullptr)
        return;

    using synth::theme::AppLookAndFeel;
    auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;

    const auto& c = lf->getTheme().colors;
    keyboardComponent->setColour(juce::MidiKeyboardComponent::whiteNoteColourId, c.bg1);
    keyboardComponent->setColour(juce::MidiKeyboardComponent::blackNoteColourId, c.surfaceHi);
    keyboardComponent->setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, c.border);
    keyboardComponent->setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, c.accent.withAlpha(0.3f));
    keyboardComponent->setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, c.accent);
    keyboardComponent->setColour(juce::MidiKeyboardComponent::textLabelColourId, c.textPrimary);
}

void ModuleComponent::lookAndFeelChanged() {
    applyHeaderButtonIcons();
    refreshWaveformComboIcons();
    applyKeyboardThemeColours();
}

void ModuleComponent::timerCallback() {
    if (module == nullptr)
        return;

    // An async plugin load can flip hasInstance() at any moment, well after the button was built —
    // polled at the card's existing 15 Hz rate rather than adding a second timer.
    if (openPluginEditorButton != nullptr) {
        if (auto* hostedPlugin = dynamic_cast<synth::HostedPluginModule*>(module))
            openPluginEditorButton->setEnabled(hostedPlugin->hasInstance());
    }

    if (auto* modBase = dynamic_cast<ModuleBase*>(module)) {
        if (auto* vb = modBase->getVisualBuffer()) {
            if (rmsReadBuffer.empty())
                rmsReadBuffer.resize(vb->getSize(), 0.0f);
            vb->copyTo(rmsReadBuffer);
            float sum = 0.0f;
            for (float s : rmsReadBuffer)
                sum += s * s;
            cachedRMS = std::sqrt(sum / (float)rmsReadBuffer.size());
        }
    }

    // Envelope playhead: reuses this existing gated 15 Hz tick rather than a new Timer (see
    // docs/layout/rendering.md) — self-guards on type/visibility, and setPlayhead
    // itself no-ops when the (segment, progress) pair is unchanged, so an idle or collapsed card
    // costs nothing beyond the guard check.
    updateEnvelopePlayhead();

    // Gate repaint: only invalidate the buffered image when something has
    // visually changed.  Idle modules (no signal, no modulation) produce no
    // repaint, so the parent content.repaint() from GraphEditor composites the
    // cached image cheaply instead of re-running the expensive text layout.
    bool needsRepaint = false;

    // 1. RMS changed meaningfully (activity glow / LED).
    //    Threshold is deliberately coarse (0.05): a steady tone produces small
    //    tick-to-tick RMS jitter from the sliding analysis window, and a tight
    //    threshold (e.g. 0.002) treated that jitter as "activity changed" and
    //    repainted every tick — invalidating the buffered image and re-running
    //    the expensive text layout 30x/sec (the startup/preset-switch freeze).
    if (std::abs(cachedRMS - lastPaintedRMS) > 0.05f)
        needsRepaint = true;

    // 2. Active modulation targeting this module (Serum mod rings)
    if (!needsRepaint) {
        const auto& modInfo = owner.getCachedModDisplayInfo();
        for (const auto& info : modInfo) {
            if (info.destNodeID == nodeId && std::abs(info.modSignalValue) > 0.001f) {
                needsRepaint = true;
                break;
            }
        }
    }

    // 3. Sequencer / PolySequencer: repaint only when the active step changes
    //    so the playhead highlight animates without repainting on every idle tick.
    if (!needsRepaint) {
        auto t = getType(module);
        if (t == ModuleType::Sequencer) {
            if (auto* seq = dynamic_cast<SequencerModule*>(module)) {
                int step = seq->currentActiveStep.load();
                if (step != lastActiveStep) {
                    lastActiveStep = step;
                    needsRepaint = true;
                }
            } else {
                // Fallback: sequencer type but no accessor — repaint each tick
                needsRepaint = true;
            }
        } else if (t == ModuleType::PolySequencer) {
            if (auto* pseq = dynamic_cast<PolySequencerModule*>(module)) {
                int step = pseq->currentActiveStep.load();
                if (step != lastActiveStep) {
                    lastActiveStep = step;
                    needsRepaint = true;
                }
            } else {
                // Fallback: sequencer type but no accessor — repaint each tick
                needsRepaint = true;
            }
        }
    }

    // NOTE: condition #4 (visible animated children — scope/freqResponse/eqCurve) is
    // intentionally omitted.  FrequencyResponseComponent, EQCurveComponent and ScopeComponent
    // manage their own repaints via their own timers and only invalidate when their data
    // changes.  Forcing a full parent repaint every tick because a child is visible caused a
    // repaint storm on every Filter module (the response view used to be always-on; it is now
    // opt-in via "Show Response", and its timer stops while hidden).

    if (needsRepaint) {
        lastPaintedRMS = cachedRMS;
        repaint();
    }

    // FRO130: MIDI-mapped badges (ONE query per module, repainting only on an actual change --
    // see refreshMidiLearnBadges' own comment) and, while a control on THIS card is armed, its
    // breathing outline -- confined to that control's own bounds, never the whole card, and
    // bounded overall by RemoteEngine's 10 s learn timeout, not by this tick. FRO256: this repaint
    // is what makes the outline's alpha (computed from wall time on every paint(), see
    // synth::ui::midilearn::paintMidiLearnArmedOutline) actually animate -- MixerColumnComponent/
    // MixerMasterColumn/TimelineTransportBar turned out to have NO equivalent repaint at all, which
    // froze their own outlines at whatever alpha their first paint happened to land on;
    // midiLearnArmedRepaintCount_ (getMidiLearnArmedRepaintCountForTest()) proves this one already
    // fires on every tick, the same way those three surfaces' own new counters prove their fix.
    refreshMidiLearnBadges();
    if (midiLearnArmedParamId_.isNotEmpty()) {
        for (const auto& e : midiLearnableRegistry_.entries()) {
            if (e.param != nullptr && e.paramId == midiLearnArmedParamId_) {
                repaint(e.component->getBounds().expanded(2));
                ++midiLearnArmedRepaintCount_;
                break;
            }
        }
    }
}

// External MIDI's device + channel combos, extracted out of createControls (FRO117) to keep that
// function under its own line-count ratchet. Neither combo is ComboBoxParameterAttachment-driven
// (the device name and channel index are plain module state, not AudioParameters).
void ModuleComponent::createExternalMidiControls(ExternalMidiModule* extMidi) {
    auto* deviceCombo = comboBoxes.add(new juce::ComboBox("Device"));
    deviceCombo->addItem("None", 1);
    int i = 2;
    auto devices = juce::MidiInput::getAvailableDevices();
    for (auto& info : devices) {
        deviceCombo->addItem(info.name, i++);
    }
    deviceCombo->setSelectedId(1, juce::dontSendNotification);
    comboParams.add(nullptr); // not ComboBoxParameterAttachment-driven — see the header

    deviceCombo->onChange = [extMidi, deviceCombo, devices, this]() {
        int selectedId = deviceCombo->getSelectedId();
        if (selectedId > 1) {
            juce::String deviceName = devices[selectedId - 2].name;
            owner.getAudioEngine().ensureMidiDeviceOpen(deviceName);
            extMidi->setMidiDeviceName(deviceName);
        } else {
            extMidi->setMidiDeviceName("External MIDI");
        }
    };

    addAndMakeVisible(deviceCombo);
    comboLabels.add(new juce::Label("Device", "Device"));
    addAndMakeVisible(comboLabels.getLast());

    auto* channelCombo = comboBoxes.add(new juce::ComboBox("Channel"));
    channelCombo->addItem("All", 1);
    for (int c = 1; c <= 16; ++c) {
        channelCombo->addItem("Channel " + juce::String(c), c + 1);
    }
    channelCombo->setSelectedId(1, juce::dontSendNotification);
    comboParams.add(nullptr); // not ComboBoxParameterAttachment-driven — see the header

    channelCombo->onChange = [extMidi, channelCombo]() {
        int selectedId = channelCombo->getSelectedId();
        // selectedId 1 -> param 0 (All)
        // selectedId 2 -> param 1 (Channel 1)
        // selectedId 17 -> param 16 (Channel 16)
        auto* param = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(extMidi, "channel"));
        if (param != nullptr) {
            // If ID is 1, we set 0 (All).
            // If ID is 2, we set 1 (Ch1).
            param->setValueNotifyingHost(param->convertTo0to1(selectedId - 1));
        }
    };

    addAndMakeVisible(channelCombo);
    comboLabels.add(new juce::Label("Channel", "Channel"));
    addAndMakeVisible(comboLabels.getLast());
}

void ModuleComponent::createControls() {
    // Auto-UI
    if (auto* midiKeyboard = dynamic_cast<MidiKeyboardModule*>(module)) {
        keyboardComponent = std::make_unique<juce::MidiKeyboardComponent>(
            midiKeyboard->getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard);

        applyKeyboardThemeColours();
        keyboardComponent->setWantsKeyboardFocus(true);
        addAndMakeVisible(keyboardComponent.get());
    } else if (auto* extMidi = dynamic_cast<ExternalMidiModule*>(module)) {
        createExternalMidiControls(extMidi);
    } else if (auto* hostedPlugin = dynamic_cast<synth::HostedPluginModule*>(module)) {
        createHostedPluginControls(*hostedPlugin);
    } else {
        const auto& params = module->getParameters();

        for (auto* param : params) {
            if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                if (shouldSkipGenericChoiceCombo(module, *choiceParam))
                    continue;
                auto* combo = comboBoxes.add(new juce::ComboBox());

                // Oscillator waveform selector: the exact choice set {"Sine", "Square", "Saw",
                // "Triangle"} (in that order) gets per-item waveform glyph icons attached via
                // PopupMenu::addItem(..., std::unique_ptr<Drawable> iconToUse).
                // All other choice params keep the plain addItemList path.
                const juce::StringArray& choices = choiceParam->choices;
                const bool isOscWaveform = (choices.size() == 4 && choices[0] == "Sine" && choices[1] == "Square" &&
                                            choices[2] == "Saw" && choices[3] == "Triangle");

                if (isOscWaveform) {
                    using synth::theme::Icon;
                    const Icon kIcons[4] = {Icon::WaveformSine, Icon::WaveformSquare, Icon::WaveformSaw,
                                            Icon::WaveformTriangle};
                    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
                    for (int i = 0; i < 4; ++i) {
                        std::unique_ptr<juce::Drawable> icon;
                        if (lf != nullptr)
                            icon = lf->getIcon(kIcons[i]); // may be nullptr in headless
                        combo->getRootMenu()->addItem(i + 1, choices[i], true, false, std::move(icon));
                    }
                } else {
                    combo->addItemList(choiceParam->choices, 1);
                }
                addAndMakeVisible(combo);
                // Right-click MIDI Learn (FRO130). juce::ComboBox::mouseDown already refuses to
                // open its popup on a right click (checks e.mods.isPopupMenu() itself), so no
                // subclass is needed here the way the toggle/header buttons below need one.
                combo->addMouseListener(this, false);
                registerMidiLearnable(*combo, choiceParam);

                auto* attach = comboAttachments.add(new juce::ComboBoxParameterAttachment(*choiceParam, *combo));
                comboParams.add(choiceParam); // param -> control mapping for reflection

                auto* label = comboLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                addAndMakeVisible(label);
            } else if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param)) {
                if (shouldSkipGenericFloatSlider(module, *floatParam))
                    continue;
                auto* knob = new synth::ui::CardKnobSlider();
                auto* slider = sliders.add(knob);
                slider->setComponentID(param->getName(100)); // ID for lookup
                setAdsrAwareSliderStyle(*slider, getType(module));
                addAndMakeVisible(slider);
                // Right-click-any-knob. `this` outlives every child slider (sliders is a member
                // OwnedArray, destroyed as part of this component's own teardown before the outer
                // object finishes destructing), so attaching `this` as the listener rather than a
                // separately-owned object has no dangling-pointer window to reason about.
                slider->addMouseListener(this, false);
                registerMidiLearnable(*slider, floatParam);      // FRO130: right-click MIDI Learn
                wireCardKnobModAmountGesture(*knob, floatParam); // FRO287

                auto* attach = sliderAttachments.add(new juce::SliderParameterAttachment(*floatParam, *slider));
                applyAdsrTimeSliderSkew(*slider, *floatParam);
                sliderParams.add(floatParam); // param -> control mapping for reflection

                auto* label = sliderLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                label->setJustificationType(juce::Justification::centred);
                addAndMakeVisible(label);
            } else if (auto* intParam = dynamic_cast<juce::AudioParameterInt*>(param)) {
                auto* knob = new synth::ui::CardKnobSlider();
                auto* slider = sliders.add(knob);
                slider->setComponentID(param->getName(100)); // ID for lookup
                slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
                // slider->setRange(intParam->getRange().start,
                // intParam->getRange().end, 1.0); // Attachment handles range
                addAndMakeVisible(slider);
                slider->addMouseListener(this, false);         // right-click-any-knob, see above
                registerMidiLearnable(*slider, intParam);      // FRO130: right-click MIDI Learn
                wireCardKnobModAmountGesture(*knob, intParam); // FRO287

                auto* attach = sliderAttachments.add(new juce::SliderParameterAttachment(*intParam, *slider));
                sliderParams.add(intParam); // param -> control mapping for reflection

                auto* label = sliderLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                label->setJustificationType(juce::Justification::centred);
                addAndMakeVisible(label);
            } else if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param)) {
                if (boolParam->paramID == "bypassed" || boolParam->paramID == "muted" || boolParam->paramID == "dualIO")
                    continue;
                if (shouldSkipGenericBoolToggle(module, *boolParam))
                    continue;

                auto* toggle = toggles.add(new detail::MidiLearnableToggleButton(boolParam->getName(100)));
                toggle->setComponentID(boolParam->getName(100)); // ID for Lookup
                addAndMakeVisible(toggle);
                // Right-click MIDI Learn (FRO130) -- same idiom as the generic slider loop above.
                toggle->addMouseListener(this, false);
                registerMidiLearnable(*toggle, boolParam);

                auto* attach = buttonAttachments.add(new juce::ButtonParameterAttachment(*boolParam, *toggle));
            }
        }
    }

    if (bypassButton) {
        for (auto* param : module->getParameters()) {
            if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param)) {
                if (boolParam->paramID == "bypassed") {
                    bypassAttachment =
                        std::make_unique<juce::ButtonParameterAttachment>(*boolParam, *bypassButton, nullptr);
                    registerMidiLearnable(*bypassButton, boolParam);
                } else if (boolParam->paramID == "muted") {
                    muteAttachment =
                        std::make_unique<juce::ButtonParameterAttachment>(*boolParam, *muteButton, nullptr);
                    registerMidiLearnable(*muteButton, boolParam);
                } else if (boolParam->paramID == "dualIO" && dualIOButton) {
                    dualIOAttachment =
                        std::make_unique<juce::ButtonParameterAttachment>(*boolParam, *dualIOButton, nullptr);
                    registerMidiLearnable(*dualIOButton, boolParam);
                }
            }
        }

        // Wire repaint into the header-button onClick lambdas.
        // onClick runs on the message thread, so a direct repaint() is safe.
        // This ensures the faded/active visual updates immediately regardless of
        // whether undoManager is non-null (the parameterValueChanged listener path
        // is only registered when undoManager != nullptr).
        bypassButton->onClick = [this] { repaint(); };
        muteButton->onClick = [this] { repaint(); };
    }

    // Register as parameter listener for undo tracking AND for bypass/mute repaint.
    // Always register all params so parameterValueChanged fires for bypass/mute changes
    // even when undoManager is null (e.g. during tests or early construction).
    for (auto* param : module->getParameters())
        param->addListener(this);

    captureLogicalPortMaps();

    // Auto-resize
    if (getType(module) == ModuleType::Sequencer || getType(module) == ModuleType::PolySequencer) {
        // 8 cols * 60 + margins, 3 rows, +26 for the Sync to Transport toggle row.
        setSize(synth::LayoutUtil::kDoubleWidth, 406);
        return;
    }

    updateLayout();
}

// FRO287: the first live AttenuverterChain routing landing on `param`'s knob, in
// getCachedModDisplayInfo() order (several routings on one knob all target the same first one --
// docs/modules/modulation.md#drag-to-knob-modulation). An invalid NodeID means either `param` isn't
// a modulation target at all, or it is but nothing is currently routed to it through an
// attenuverter (a DirectCV/PolyBus routing has none to adjust).
// FRO288: shared by firstAttenuverterForParam and the knob-hover -> cable-hover wiring in
// wireCardKnobModAmountGesture, so the two can never resolve a different channel for the same
// param.
int ModuleComponent::destChannelForBoundParam(juce::RangedAudioParameter* param) const {
    auto* mod = dynamic_cast<ModuleBase*>(module);
    if (mod == nullptr || param == nullptr)
        return -1;
    for (const auto& t : mod->getModulationTargets())
        if (mod->parameterForModTarget(t) == param)
            return t.channelIndex;
    return -1;
}

juce::AudioProcessorGraph::NodeID ModuleComponent::firstAttenuverterForParam(juce::RangedAudioParameter* param) const {
    const int destChannel = destChannelForBoundParam(param);
    if (destChannel < 0)
        return {};

    for (const auto& info : owner.getCachedModDisplayInfo()) {
        if (info.destNodeID == nodeId && info.destChannelIndex == destChannel && info.attenuverterNodeID.uid != 0)
            return info.attenuverterNodeID;
    }
    return {};
}

// CardKnobSlider::wantsModAmountGesture: claim the gesture when this knob drives an attenuverter
// AND the click is either Alt-modified or lands within +-5px of the ring's own radius (the same
// geometry paintModulationRings draws it at -- see modRingCentreFor/modRingRadiusFor). `bounds` is
// the knob's own local bounds (CardKnobSlider hands this its getLocalBounds()).
bool ModuleComponent::wantsModAmountGestureFor(juce::RangedAudioParameter* param, juce::Rectangle<float> bounds,
                                               const juce::MouseEvent& e) const {
    if (firstAttenuverterForParam(param).uid == 0)
        return false;
    if (e.mods.isAltDown())
        return true;

    const float radius = modRingRadiusFor(bounds);
    if (radius <= 0.0f)
        return false;
    const auto centre = modRingCentreFor(bounds);
    const float dist = centre.getDistanceFrom(e.position);
    return std::abs(dist - radius) <= 5.0f;
}

// CardKnobSlider::onModAmountGesture: phase 0 (down) captures undo and remembers which
// attenuverter this gesture targets (recomputed fresh, not reused across gestures, in case the
// routing changed since the last drag); phase 1 (drag) adjusts it by the same per-event Y delta
// the cable midpoint knob has always used; phase 2 (up) commits the undo snapshot. The knob's own
// juce::Slider value is never touched here -- CardKnobSlider only forwards to Slider when this
// function is NOT what claimed the gesture.
void ModuleComponent::handleModAmountGesture(juce::RangedAudioParameter* param, const juce::MouseEvent& e, int phase) {
    if (phase == 0) {
        modAmountGestureAttenuverterId_ = firstAttenuverterForParam(param);
        modAmountGestureLastPos_ = e.getPosition();
        owner.beginModAmountGesture();
        return;
    }
    if (modAmountGestureAttenuverterId_.uid == 0)
        return;
    if (phase == 1) {
        const float delta = (e.getPosition().y - modAmountGestureLastPos_.y) * -0.01f;
        modAmountGestureLastPos_ = e.getPosition();
        owner.adjustModAmount(modAmountGestureAttenuverterId_, delta);
        return;
    }
    owner.commitModAmountGesture(); // phase 2
    modAmountGestureAttenuverterId_ = {};
}

void ModuleComponent::wireCardKnobModAmountGesture(synth::ui::CardKnobSlider& knob, juce::RangedAudioParameter* param) {
    knob.wantsModAmountGesture = [this, param, &knob](const juce::MouseEvent& e) {
        return wantsModAmountGestureFor(param, knob.getLocalBounds().toFloat(), e);
    };
    knob.onModAmountGesture = [this, param](const juce::MouseEvent& e, int phase) {
        handleModAmountGesture(param, e, phase);
    };
    // FRO288: knob-hover -> cable-hover, the reverse direction of the cable-hover -> ring-highlight
    // wiring in GraphEditorCanvas.cpp's mouseMove. Only correlates while a live AttenuverterChain
    // routing actually lands here (same gate wantsModAmountGestureFor uses) -- a DirectCV/PolyBus
    // target has no cable re-anchored onto it to highlight.
    knob.onHoverChanged = [this, param](bool entered) {
        if (!entered || firstAttenuverterForParam(param).uid == 0) {
            owner.setHoveredModTarget(std::nullopt);
            return;
        }
        const int destChannel = destChannelForBoundParam(param);
        if (destChannel < 0)
            return;
        owner.setHoveredModTarget(GraphEditor::HoveredModTarget{nodeId, destChannel});
    };
}

void ModuleComponent::setRasterFrozen(bool frozen) {
    if (rasterCache != nullptr)
        rasterCache->setFrozen(frozen);
}

bool ModuleComponent::isRasterFrozen() const noexcept { return rasterCache != nullptr && rasterCache->isFrozen(); }
