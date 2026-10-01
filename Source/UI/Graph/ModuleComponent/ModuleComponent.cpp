// ModuleComponent.cpp -- construction/teardown of a graph node's card, per-scope header/theme
// helpers, and the auto-UI control builder (createControls). ModuleComponent is declared in
// ModuleComponent.h; the rest of its implementation lives in the sibling ModuleComponent*.cpp
// units next to this one.
#include "ModuleComponent.h"
#include "AudioEngine/AudioEngine.h"
#include "CardKnobSlider.h"
#include "ModuleComponentHostedPluginCard.h"
#include "ModuleComponentInternal.h"
#include "Modules/ExternalMidiModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/PolySequencerModule.h"
#include "Modules/SequencerModule.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

using namespace detail;

juce::Point<int> ModuleComponent::getMidiPortCenter(bool isOutput) const {
    if (isMacroPortType(getType(module)))
        return {macroPortJackX(!isOutput), midiJackY(module)};
    return {isOutput ? getWidth() - 10 : 10, midiJackY(module)};
}

ModuleComponent::ModuleComponent(juce::AudioProcessor* m, juce::AudioProcessorGraph::NodeID nId, GraphEditor& owner,
                                 AppUndoManager* undoMgr)
    : module(m)
    , nodeId(nId)
    , owner(owner)
    , undoManager(undoMgr) {

    // See the member's own comment and detachFromProcessor() for why this is captured
    // up front rather than re-derived later (by the time detachFromProcessor() runs, the node may
    // legitimately have been removed -- that's the case this flag has to tell apart from "never
    // was in a graph to begin with").
    for (auto* node : owner.getAudioEngine().getGraph().getNodes()) {
        if (node->nodeID == nodeId) {
            nodeWasInGraphAtConstruction_ = true;
            break;
        }
    }

    showContextMenuHook_ = [this](juce::PopupMenu& menu) { showRealContextMenu(menu); };

    if (auto* modBase = dynamic_cast<ModuleBase*>(module)) {
        if (auto* vb = modBase->getVisualBuffer()) {
            // Parametric EQ keeps its VisualBuffer for the spectrum analyser's FFT, but a scope
            // on top of that analyser is redundant clutter, so it gets no scope UI. A macro-port
            // widget enables a VisualBuffer too (for a future activity LED — none
            // of the four types draw one today), but "no body" (item 1) rules out a scope toggle
            // here as firmly as it rules out bypass/mute/delete.
            if (getType(module) != ModuleType::ExternalMidi && getType(module) != ModuleType::ParametricEQ &&
                !isMacroPortType(getType(module))) {
                scopeComponent = std::make_unique<ScopeComponent>(*vb);
                addAndMakeVisible(scopeComponent.get());

                scopeToggle = std::make_unique<juce::ToggleButton>("Show Scope");
                scopeToggle->setToggleState(false, juce::dontSendNotification);
                scopeComponent->setVisible(false);
                scopeToggle->onClick = [this] { setScopeShown(scopeToggle->getToggleState()); };
                addAndMakeVisible(scopeToggle.get());
            }
        }
    }

    // The card body's views (the Threshold control) are built here, where the card always built them,
    // so the child order is unchanged; its parameter widgets follow in createControls().
    cardBody_ = synth::CardBody::createFor(*this, *module, owner.getAudioEngine().getGraph(), nodeId);
    if (cardBody_ != nullptr) {
        cardBody_->createViews();
        thresholdControl = cardBody_->getThresholdView();
    }

    if (auto* filterMod = dynamic_cast<FilterModule*>(module)) {
        freqResponseComponent = std::make_unique<FrequencyResponseComponent>(*filterMod);
        addAndMakeVisible(freqResponseComponent.get());

        // Same pattern as the scope: hidden by default so a Filter card does not pay for a
        // 30 Hz response/spectrum timer until the user asks for it.
        freqResponseToggle = std::make_unique<juce::ToggleButton>("Show Response");
        freqResponseToggle->setToggleState(false, juce::dontSendNotification);
        freqResponseComponent->setVisible(false);
        freqResponseToggle->onClick = [this] { setResponseShown(freqResponseToggle->getToggleState()); };
        addAndMakeVisible(freqResponseToggle.get());

        spectrumToggle = std::make_unique<juce::ToggleButton>("Show Spectrum");
        spectrumToggle->setToggleState(false, juce::dontSendNotification);
        spectrumToggle->onClick = [this] { setSpectrumShown(spectrumToggle->getToggleState()); };
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
        spectrumToggle->onClick = [this] { setSpectrumShown(spectrumToggle->getToggleState()); };
        addAndMakeVisible(spectrumToggle.get());

        eqPopOutButton = std::make_unique<juce::TextButton>("Open EQ Window");
        eqPopOutButton->setComponentID("eqPopOut");
        eqPopOutButton->setTooltip("Edit this EQ in a larger resizable window");
        eqPopOutButton->onClick = [this] { openEqWindow(); };
        addAndMakeVisible(eqPopOutButton.get());
    }

    // Attenuverter has no header at all; a macro-port widget has no header CHROME —
    // "no module header chrome and no body" — so neither gets bypass/mute/delete/Dual I/O buttons.
    if (getType(module) != ModuleType::Attenuverter && !isMacroPortType(getType(module))) {
        // MidiLearnableDrawableButton: plain juce::DrawableButton fires its click on a
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
    if (getType(module) == ModuleType::LFO)
        createLfoCardControls();
    createWavetableTabs(); // after createControls(): it groups the sliders/combos that call made
    applyHeaderButtonIcons();
    restoreCardView();           // after every panel exists and has had its first layout
    applyControlAccessibility(); // last: names every control the steps above built
    startTimerHz(15);            // 15 FPS is plenty for activity glow / step indicator; lower CPU than 30
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
    // The LFO wave graph holds no module reference of its own, but its onGestureStart/
    // onGestureEnd lambdas capture a SafePointer<ModuleComponent> and reach back into `module` --
    // reset alongside every other module-referencing child.
    lfoCurveEditor.reset();
    lfoGridCombo.reset();
    lfoShapesButton.reset();
    lfoToolsButton.reset();
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
    thresholdControl = nullptr;
    if (cardBody_ != nullptr)
        cardBody_->releaseViews();

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

    // Destroy attachments and remove our own parameter listener ONLY if the processor is still
    // alive. During undo, graph.clear() may have already freed the processor and its parameters
    // by the time this runs — if so, touching it (even just to call removeListener) is a
    // use-after-free, so we release/leak instead.
    //
    // "Still alive" means something different depending on how this card came to exist:
    //  - The normal case: this card's node WAS part of `owner`'s graph when the card was built
    //    (nodeWasInGraphAtConstruction_, captured once in the ctor — see the comment there).
    //    A later single-node removal (GraphEditor::deleteSelection / requestDeleteModule /
    //    replaceModule) frees the processor before this destructor runs, so we confirm liveness
    //    by re-checking the CURRENT graph for a node whose processor still matches `module`.
    //  - The card was built directly on a bare processor that was never added to any graph at all
    //    (never true in production — GraphEditor only ever builds a card for an existing node —
    //    but exactly what ModuleComponentKnobCoverageTests.cpp and ModuleComponentLifecycleTests.cpp
    //    do). There the graph can never have freed it out from under us, because the graph never
    //    owned it in the first place, so it is always safe to detach (treating it as "gone" would leave our listener
    //    registration dangling on the still-live processor — a heap-use-after-free on the next parameter write).
    bool processorAlive = false;
    if (module != nullptr) {
        if (!nodeWasInGraphAtConstruction_) {
            processorAlive = true;
        } else {
            for (auto* node : owner.getAudioEngine().getGraph().getNodes()) {
                if (node->getProcessor() == module) {
                    processorAlive = true;
                    break;
                }
            }
        }
    }
    if (cardBody_ != nullptr)
        cardBody_->releaseBindings(processorAlive);
    if (processorAlive) {
        bypassAttachment.reset();
        muteAttachment.reset();
        dualIOAttachment.reset();
        envelopeDivAttachments_.clear(); // same live-processor-pointer contract as the card body's attachments
        for (auto* param : module->getParameters())
            param->removeListener(this);
    } else {
        // Processor already freed — leak attachments (and skip removeListener entirely) to avoid
        // use-after-free in ~ParameterAttachment / our own removeListener call, both of which
        // would otherwise dereference the freed processor's parameters.
        (void)bypassAttachment.release();
        (void)muteAttachment.release();
        (void)dualIOAttachment.release();
        while (envelopeDivAttachments_.size() > 0)
            (void)envelopeDivAttachments_.removeAndReturn(envelopeDivAttachments_.size() - 1);
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
    // Reverse-sync the LFO wave graph from an undo/redo or preset load this card didn't
    // itself just write (writeLfoWaveFromCurve/applyLfoWavePreset/applyLfoWaveTool already keep
    // lfoLastSeenWaveGeneration current for their OWN writes), then poll the playhead -- same
    // gated 15 Hz tick as everything else here.
    if (auto* lfo = dynamic_cast<LFOModule*>(module)) {
        if (!lfoCurveGestureActive && lfo->getCustomWaveGeneration() != lfoLastSeenWaveGeneration)
            syncLfoCurveFromModule();
    }
    updateLfoWavePlayhead();

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

    // MIDI-mapped badges (ONE query per module, repainting only on an actual change --
    // see refreshMidiLearnBadges' own comment) and, while a control on THIS card is armed, its
    // breathing outline -- confined to that control's own bounds, never the whole card, and
    // bounded overall by RemoteEngine's 10 s learn timeout, not by this tick. This repaint is what
    // makes the outline's alpha (computed from wall time on every paint(), see
    // synth::ui::midilearn::paintMidiLearnArmedOutline) actually animate; without a repaint the
    // outline would freeze at whatever alpha its first paint landed on.
    // midiLearnArmedRepaintCount_ (getMidiLearnArmedRepaintCountForTest()) proves it fires on every
    // tick.
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

// External MIDI's device + channel combos, extracted out of createControls to keep that
// function under its own line-count ratchet. Neither combo is ComboBoxParameterAttachment-driven
// (the device name and channel index are plain module state, not AudioParameters).
void ModuleComponent::createExternalMidiControls(ExternalMidiModule* extMidi) {
    auto* deviceCombo = adoptBespokeWidget(new juce::ComboBox("Device"));
    comboBoxes.add(deviceCombo);
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
    comboLabels.add(adoptBespokeWidget(new juce::Label("Device", "Device")));
    addAndMakeVisible(comboLabels.getLast());

    auto* channelCombo = adoptBespokeWidget(new juce::ComboBox("Channel"));
    comboBoxes.add(channelCombo);
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
    comboLabels.add(adoptBespokeWidget(new juce::Label("Channel", "Channel")));
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
    } else if (cardBody_ != nullptr) {
        cardBody_->createParameterWidgets();
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

// The first live AttenuverterChain routing landing on `param`'s knob, in
// getCachedModDisplayInfo() order (several routings on one knob all target the same first one --
// docs/modules/modulation.md#drag-to-knob-modulation). An invalid NodeID means either `param` isn't
// a modulation target at all, or it is but nothing is currently routed to it through an
// attenuverter (a DirectCV/PolyBus routing has none to adjust).
// Shared by firstAttenuverterForParam and the knob-hover -> cable-hover wiring in
// wireCardControlGestures, so the two can never resolve a different channel for the same
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

// CardControlGestures::wantsModAmountGesture: claim the gesture when this control drives an
// attenuverter AND the click is either Alt-modified or lands within +-5px of the ring's own radius (the
// same geometry paintModulationRings draws it at -- see modRingCentreFor/modRingRadiusFor), or, on a
// fader, on its modulation bar's strip. `knob` is the control itself (its local bounds and layout give
// the ring centre).
bool ModuleComponent::wantsModAmountGestureFor(juce::RangedAudioParameter* param, const juce::Slider& knob,
                                               const juce::MouseEvent& e) const {
    if (firstAttenuverterForParam(param).uid == 0)
        return false;
    if (e.mods.isAltDown())
        return true;
    if (const auto* fader = dynamic_cast<const synth::ui::CardFader*>(&knob))
        return fader->hitsModBar(e.position);

    const auto bounds = knob.getLocalBounds().toFloat();
    const float radius = modRingRadiusFor(bounds);
    if (radius <= 0.0f)
        return false;
    const auto centre = modRingCentreFor(knob, bounds);
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

// Claims a click on `param`'s knob when its CV jack is knob-bound (hidden -- getPortForPoint
// can no longer offer this parameter's jack as a pickup point, whether or not a cable currently
// lands here, exactly like a real, empty gutter jack still accepts a click to START a drag) and the
// click is within a few px of the landing dot GraphEditorCables.cpp paints there -- never inside
// the ring's own annulus, which wantsModAmountGestureFor above already claimed first. `knob`'s
// LOCAL bounds are used (same as wantsModAmountGestureFor) -- `e.position` arrives in that same
// local frame, so the landing point must be computed from them too, NOT from getModTargetKnobAnchor
// (which is CARD-local).
bool ModuleComponent::wantsCablePickupGestureFor(juce::RangedAudioParameter* param, const juce::Slider& knob,
                                                 const juce::MouseEvent& e) const {
    if (destChannelForBoundParam(param) < 0)
        return false;
    constexpr float kPickupHitPad = 4.0f; // the dot is only 7px across; a pixel-perfect target is unfriendly
    if (const auto* fader = dynamic_cast<const synth::ui::CardFader*>(&knob))
        return fader->landingPoint(kKnobLandingDotDiameter).getDistanceFrom(e.position) <=
               kKnobLandingDotDiameter * 0.5f + kPickupHitPad;
    const auto bounds = knob.getLocalBounds().toFloat();
    const float ringRadius = modRingRadiusFor(bounds);
    if (ringRadius <= 0.0f)
        return false;
    const auto centre = modRingCentreFor(knob, bounds);
    const float landingRadius = ringRadius + knobLandingRadiusOffset();
    const auto anchor = modRingPointForNorm(centre, landingRadius, 0.0f);

    // The pad stays comfortably clear of the ring annulus (see knobLandingRadiusOffset's push-out
    // math): the dot sits well beyond the ring, so it never reaches back into the ring's +-5px zone.
    return anchor.getDistanceFrom(e.position) <= (kKnobLandingDotDiameter * 0.5f + kPickupHitPad);
}

// CardKnobSlider::onCablePickupGesture: forwards straight into the SAME connection-drag machinery
// a click on a real (visible) input jack starts -- GraphEditor::beginConnectionDrag/dragConnection/
// endConnectionDrag, as an INPUT drag (dragSourceIsInput = true), keyed by the target's VISIBLE
// jack index (mapInputChannel, matching what routing.destVisibleJack/getPortForPoint speak in
// everywhere else). Whatever disconnect-and-redrag behaviour that machinery already gives a real
// input jack, this knob now gets for free, unchanged.
void ModuleComponent::handleCablePickupGesture(juce::RangedAudioParameter* param, const juce::MouseEvent& e,
                                               int phase) {
    auto* mb = dynamic_cast<ModuleBase*>(module);
    if (mb == nullptr)
        return;
    if (phase == 0) {
        const int destChannel = destChannelForBoundParam(param);
        if (destChannel < 0)
            return;
        const int visibleJack = mb->mapInputChannel(destChannel).visibleJackIndex;
        owner.beginConnectionDrag(this, visibleJack, /*isInput*/ true, /*isMidi*/ false, e.getScreenPosition());
        return;
    }
    if (phase == 1) {
        owner.dragConnection(e.getScreenPosition());
        return;
    }
    owner.endConnectionDrag(e.getScreenPosition()); // phase 2
}

// The one wiring for every continuous card control (knob, large knob, fader): the gestures live in
// CardControlGestures, the hit zones in wantsModAmountGestureFor/wantsCablePickupGestureFor above.
void ModuleComponent::wireCardControlGestures(juce::Slider& knob, synth::ui::CardControlGestures& gestures,
                                              juce::RangedAudioParameter* param) {
    gestures.wantsModAmountGesture = [this, param, &knob](const juce::MouseEvent& e) {
        return wantsModAmountGestureFor(param, knob, e);
    };
    gestures.onModAmountGesture = [this, param](const juce::MouseEvent& e, int phase) {
        handleModAmountGesture(param, e, phase);
    };
    // Knob-hover -> cable-hover, the reverse direction of the cable-hover -> ring-highlight
    // wiring in GraphEditorCanvas.cpp's mouseMove. Only correlates while a live AttenuverterChain
    // routing actually lands here (same gate wantsModAmountGestureFor uses) -- a DirectCV/PolyBus
    // target has no cable re-anchored onto it to highlight.
    gestures.onHoverChanged = [this, param](bool entered) {
        if (!entered || firstAttenuverterForParam(param).uid == 0) {
            owner.setHoveredModTarget(std::nullopt);
            return;
        }
        const int destChannel = destChannelForBoundParam(param);
        if (destChannel < 0)
            return;
        owner.setHoveredModTarget(GraphEditor::HoveredModTarget{nodeId, destChannel});
    };

    // This knob's own CV jack is hidden when it's a bound modulation target (see
    // isInputJackKnobBound) -- a click near where the cable lands (getModTargetKnobAnchor, just
    // outside the ring, never inside its annulus, so this never fights wantsModAmountGestureFor
    // above) is the only way left to pick the cable back up. `&knob` is safe the same way it is
    // above: the lambda only ever runs while `knob` is alive, from `knob`'s own mouseDown.
    gestures.wantsCablePickupGesture = [this, param, &knob](const juce::MouseEvent& e) {
        return wantsCablePickupGestureFor(param, knob, e);
    };
    gestures.onCablePickupGesture = [this, param](const juce::MouseEvent& e, int phase) {
        handleCablePickupGesture(param, e, phase);
    };
}

void ModuleComponent::setRasterFrozen(bool frozen) {
    // A docked macro-port widget is never pinned: its name alpha follows the zoom, so it must repaint live
    // instead of resampling a stale image until the gesture settles.
    if (frozen && module != nullptr && isMacroPortType(getType(module)))
        return;
    if (rasterCache != nullptr)
        rasterCache->setFrozen(frozen);
}

bool ModuleComponent::isRasterFrozen() const noexcept { return rasterCache != nullptr && rasterCache->isFrozen(); }
