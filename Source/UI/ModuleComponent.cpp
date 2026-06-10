#include "ModuleComponent.h"
#include "../Modules/ExternalMidiModule.h"
#include "../Modules/ModuleBase.h"
#include "../Modules/PolySequencerModule.h"
#include "../Modules/SequencerModule.h"
#include "GraphEditor.h"
#include "LayoutUtil.h"
#include "Theme/GravisynthLookAndFeel.h"

static ModuleType getType(juce::AudioProcessor* module) {
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        return mb->getModuleType();
    return ModuleType::Oscillator;
}

ModuleComponent::ModuleComponent(juce::AudioProcessor* m, juce::AudioProcessorGraph::NodeID nId, GraphEditor& owner,
                                 GravisynthUndoManager* undoMgr)
    : module(m)
    , nodeId(nId)
    , owner(owner)
    , undoManager(undoMgr) {

    if (auto* modBase = dynamic_cast<ModuleBase*>(module)) {
        if (auto* vb = modBase->getVisualBuffer()) {
            if (getType(module) != ModuleType::ExternalMidi) {
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

    if (auto* filterMod = dynamic_cast<FilterModule*>(module)) {
        freqResponseComponent = std::make_unique<FrequencyResponseComponent>(*filterMod);
        addAndMakeVisible(freqResponseComponent.get());

        spectrumToggle = std::make_unique<juce::ToggleButton>("Show Spectrum");
        spectrumToggle->setToggleState(false, juce::dontSendNotification);
        spectrumToggle->onClick = [this] { freqResponseComponent->setShowSpectrum(spectrumToggle->getToggleState()); };
        addAndMakeVisible(spectrumToggle.get());
    }

    if (getType(module) != ModuleType::Attenuverter) {
        bypassButton = std::make_unique<juce::DrawableButton>("Bypass", juce::DrawableButton::ImageFitted);
        bypassButton->setClickingTogglesState(true);
        addAndMakeVisible(*bypassButton);

        muteButton = std::make_unique<juce::DrawableButton>("Mute", juce::DrawableButton::ImageFitted);
        muteButton->setClickingTogglesState(true);
        addAndMakeVisible(*muteButton);

        deleteButton = std::make_unique<juce::DrawableButton>("Delete", juce::DrawableButton::ImageFitted);
        deleteButton->setTooltip("Delete module");
        deleteButton->onClick = [this] { this->owner.requestDeleteModule(this->nodeId); };
        addAndMakeVisible(*deleteButton);
    }

    setTitle(module->getName());
    setBufferedToImage(true);
    createControls();
    applyHeaderButtonIcons();
    startTimerHz(15); // 15 FPS is plenty for activity glow / step indicator; lower CPU than 30
}

ModuleComponent::~ModuleComponent() { detachFromProcessor(); }

void ModuleComponent::detachFromProcessor() {
    stopTimer();
    setVisible(false);

    // Destroy scope component first — it has its own timer reading from the module's VisualBuffer
    scopeComponent.reset();
    scopeToggle.reset();
    keyboardComponent.reset();

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
        sliderAttachments.clear();
        comboAttachments.clear();
        buttonAttachments.clear();
    } else {
        // Processor already freed — leak attachments to avoid use-after-free
        // in ~ParameterAttachment which calls parameter->removeListener()
        (void)bypassAttachment.release();
        (void)muteAttachment.release();
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
    auto* lf = dynamic_cast<gsynth::theme::GravisynthLookAndFeel*>(&getLookAndFeel());
    if (lf == nullptr)
        return;

    if (bypassButton) {
        if (auto d = lf->getIcon(gsynth::theme::Icon::ModuleBypass))
            bypassButton->setImages(d.get());
    }
    if (muteButton) {
        if (auto d = lf->getIcon(gsynth::theme::Icon::ModuleMute))
            muteButton->setImages(d.get());
    }
    if (deleteButton) {
        if (auto d = lf->getIcon(gsynth::theme::Icon::ModuleDelete))
            deleteButton->setImages(d.get());
    }
}

void ModuleComponent::lookAndFeelChanged() { applyHeaderButtonIcons(); }

void ModuleComponent::timerCallback() {
    if (module == nullptr)
        return;

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

    // NOTE: condition #4 (visible animated children — scope/freqResponse) is intentionally
    // omitted.  FrequencyResponseComponent and ScopeComponent manage their own repaints
    // via their own timers and only invalidate when their data changes.  Forcing a full
    // parent repaint every tick because a child is visible caused a repaint storm on every
    // Filter module (freqResponseComponent is always visible).

    if (needsRepaint) {
        lastPaintedRMS = cachedRMS;
        repaint();
    }
}

void ModuleComponent::createControls() {
    // Auto-UI
    if (auto* midiKeyboard = dynamic_cast<MidiKeyboardModule*>(module)) {
        keyboardComponent = std::make_unique<juce::MidiKeyboardComponent>(
            midiKeyboard->getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard);
        keyboardComponent->setWantsKeyboardFocus(true);
        addAndMakeVisible(keyboardComponent.get());
    } else if (auto* extMidi = dynamic_cast<ExternalMidiModule*>(module)) {
        auto* deviceCombo = comboBoxes.add(new juce::ComboBox("Device"));
        deviceCombo->addItem("None", 1);
        int i = 2;
        auto devices = juce::MidiInput::getAvailableDevices();
        for (auto& info : devices) {
            deviceCombo->addItem(info.name, i++);
        }
        deviceCombo->setSelectedId(1, juce::dontSendNotification);

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

        channelCombo->onChange = [extMidi, channelCombo]() {
            int selectedId = channelCombo->getSelectedId();
            // selectedId 1 -> param 0 (All)
            // selectedId 2 -> param 1 (Channel 1)
            // selectedId 17 -> param 16 (Channel 16)
            auto* param = dynamic_cast<juce::AudioParameterInt*>(extMidi->getParameters()[1]);
            if (param != nullptr) {
                // If ID is 1, we set 0 (All).
                // If ID is 2, we set 1 (Ch1).
                param->setValueNotifyingHost(param->convertTo0to1(selectedId - 1));
            }
        };

        addAndMakeVisible(channelCombo);
        comboLabels.add(new juce::Label("Channel", "Channel"));
        addAndMakeVisible(comboLabels.getLast());
    } else {
        const auto& params = module->getParameters();

        for (auto* param : params) {
            if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param)) {
                auto* combo = comboBoxes.add(new juce::ComboBox());
                combo->addItemList(choiceParam->choices, 1);
                addAndMakeVisible(combo);

                auto* attach = comboAttachments.add(new juce::ComboBoxParameterAttachment(*choiceParam, *combo));

                auto* label = comboLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                addAndMakeVisible(label);
            } else if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param)) {
                auto* slider = sliders.add(new juce::Slider());
                slider->setComponentID(param->getName(100)); // ID for lookup
                if (getType(module) == ModuleType::ADSR) {
                    slider->setSliderStyle(juce::Slider::LinearVertical);
                    slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
                } else {
                    slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                    slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
                }
                addAndMakeVisible(slider);

                auto* attach = sliderAttachments.add(new juce::SliderParameterAttachment(*floatParam, *slider));

                auto* label = sliderLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                label->setJustificationType(juce::Justification::centred);
                addAndMakeVisible(label);
            } else if (auto* intParam = dynamic_cast<juce::AudioParameterInt*>(param)) {
                auto* slider = sliders.add(new juce::Slider());
                slider->setComponentID(param->getName(100)); // ID for lookup
                slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
                // slider->setRange(intParam->getRange().start,
                // intParam->getRange().end, 1.0); // Attachment handles range
                addAndMakeVisible(slider);

                auto* attach = sliderAttachments.add(new juce::SliderParameterAttachment(*intParam, *slider));

                auto* label = sliderLabels.add(new juce::Label(param->getName(100), param->getName(100)));
                label->setJustificationType(juce::Justification::centred);
                addAndMakeVisible(label);
            } else if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param)) {
                if (boolParam->paramID == "bypassed" || boolParam->paramID == "muted")
                    continue;

                auto* toggle = toggles.add(new juce::ToggleButton(boolParam->getName(100)));
                toggle->setComponentID(boolParam->getName(100)); // ID for Lookup
                addAndMakeVisible(toggle);

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
                } else if (boolParam->paramID == "muted") {
                    muteAttachment =
                        std::make_unique<juce::ButtonParameterAttachment>(*boolParam, *muteButton, nullptr);
                }
            }
        }
    }

    // Register as parameter listener for undo tracking
    if (undoManager) {
        for (auto* param : module->getParameters())
            param->addListener(this);
    }

    // Auto-resize
    if (getType(module) == ModuleType::Sequencer || getType(module) == ModuleType::PolySequencer) {
        setSize(gsynth::LayoutUtil::kDoubleWidth, 380); // 8 cols * 60 + margins, 3 rows
        return;
    }

    updateLayout();
}

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

void ModuleComponent::updateLayout() {
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

    if (getType(module) == ModuleType::Sequencer || getType(module) == ModuleType::PolySequencer) {
        setSize(gsynth::LayoutUtil::kDoubleWidth, 380);
        return;
    }

    if (getType(module) == ModuleType::ADSR) {
        setSize(220 + 60, 180); // 180 is height from ADSR Layout
        return;
    }

    int contentHeight = 40; // Header
    // Account for port label space on modules with many inputs
    int numInputs = module->getTotalNumInputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module))
        numInputs = mb->getVisibleInputPortCount();
    if (numInputs > 2)
        contentHeight = std::max(contentHeight, 30 + numInputs * 20 + 10);
    contentHeight += comboBoxes.size() * 50;
    contentHeight += toggles.size() * 30;

    if (scopeToggle)
        contentHeight += 30;

    int numSliders = sliders.size();
    int rows = (numSliders + 1) / 2;
    contentHeight += rows * 80;

    if (scopeComponent && scopeComponent->isVisible())
        contentHeight += 110;

    if (freqResponseComponent)
        contentHeight += 130;

    if (spectrumToggle)
        contentHeight += 30;

    setSize(280, std::max(100, contentHeight + 20));
    resized();
}

void ModuleComponent::paint(juce::Graphics& g) {
    if (module == nullptr)
        return;

    if (getType(module) == ModuleType::Attenuverter) {
        return; // Transparent background, no ports, no header
    }

    auto* mod = dynamic_cast<ModuleBase*>(module);
    bool isBypassed = mod && mod->isBypassed();

    // Guarded LnF cast: headless tests construct this component WITHOUT installing our LnF.
    // When the cast is null we fall back to a plain themed-ish fill so those tests don't crash.
    auto* lf = dynamic_cast<gsynth::theme::GravisynthLookAndFeel*>(&getLookAndFeel());

    if (lf != nullptr) {
        // Single owner of card treatment: background, drop shadow, body fill, border, and the
        // header band (filled + title drawn) all come from the active theme. selected=false for
        // v1 (no per-module selection model yet — see spec section 12).
        lf->drawModulePanel(g, getLocalBounds().toFloat(), 24, module->getName(), /*selected*/ false, isBypassed);
    } else {
        // Fallback path (no themed LnF): plain fill + simple header so tests render without crashing.
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        g.setColour(juce::Colours::black);
        g.drawRect(getLocalBounds(), 2);
        g.setColour(juce::Colours::darkgrey);
        g.fillRect(0, 0, getWidth(), 24);
        g.setColour(juce::Colours::white);
        g.drawText(module->getName(), 0, 0, getWidth(), 24, juce::Justification::centred, true);
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
        auto p = juce::Point<int>(getWidth() - 10, 30); // Top right, below header
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);
        g.setColour(labelColour);
        g.drawText("Midi Out", p.x - 65, p.y - 5, 60, 10, juce::Justification::right, false);
    }
    // MIDI Input (Top Left if accepts midi components)
    if (module->acceptsMidi()) {
        g.setColour(audioJackColour);
        auto p = juce::Point<int>(10, 30); // Top left near header
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

            for (int si = 0; si < sliders.size(); ++si) {
                if (sliders[si]->getComponentID() != targetParamName)
                    continue;
                if (sliders[si]->getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag)
                    continue;

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
                break;
            }
        }
    }
}

juce::Point<int> ModuleComponent::getPortCenter(int index, bool isInput) {
    if (module == nullptr)
        return {0, 0};

    if (getType(module) == ModuleType::Attenuverter) {
        return {getWidth() / 2, getHeight() / 2};
    }

    int yStep = 20;
    int headerHeight = 30;

    int portOffset = 0;
    if (module->producesMidi()) {
        portOffset = 20; // Additional offset for all ports if MIDI out is present, to avoid collision with MIDI Out at
                         // (getWidth() - 10, 30)
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
        auto p = juce::Point<int>(getWidth() - 10, 30); // Matches paint()
        if (localPoint.getDistanceFrom(p) < 10) {
            return Port{{p.x - 5, p.y - 5, 10, 10},
                        juce::AudioProcessorGraph::midiChannelIndex,
                        false,
                        true}; // Index 0, Output, IsMidi
        }
    }

    // MIDI Input detection (Top Left)
    if (module->acceptsMidi()) {
        auto p = juce::Point<int>(10, 30); // Top left near header
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

    // Header icon buttons: delete (rightmost) → bypass → mute (leftmost of the three).
    // Attenuverter path: all three are null → no-op.
    if (deleteButton)
        deleteButton->setBounds(getWidth() - 26, 2, 22, 20);

    if (bypassButton)
        bypassButton->setBounds(getWidth() - 50, 2, 22, 20);

    if (muteButton)
        muteButton->setBounds(getWidth() - 74, 2, 22, 20);

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

        return;
    }

    // --- ADSR Layout ---
    if (getType(module) == ModuleType::ADSR) {
        int margin = 30;                // Side margins for ports
        setSize(220 + margin * 2, 180); // Increase width
        int y = 30;
        int contentWidth = getWidth() - margin * 2;
        int sliderWidth = 50;
        int sliderHeight = 120;

        // We expect 4 sliders: A, D, S, R
        for (int i = 0; i < sliders.size(); ++i) {
            int x = margin + 10 + i * sliderWidth;
            sliderLabels[i]->setBounds(x, y, sliderWidth, 20);
            sliders[i]->setBounds(x, y + 20, sliderWidth, sliderHeight);
        }
        return;
    }

    // --- MIDI Keyboard Layout ---
    if (getType(module) == ModuleType::MidiKeyboard) {
        setSize(gsynth::LayoutUtil::kDoubleWidth, 150); // Appropriate size for a keyboard
        if (keyboardComponent) {
            keyboardComponent->setBounds(10, 50, getWidth() - 20, getHeight() - 60);
        }
        return;
    }

    // --- Default Layout ---
    int y = 30;
    // Increase top y if MIDI IN is present to avoid overlap
    if (module->acceptsMidi())
        y += 30;
    // Push content below input port labels
    int numInputs2 = module->getTotalNumInputChannels();
    if (auto* mb2 = dynamic_cast<ModuleBase*>(module))
        numInputs2 = mb2->getVisibleInputPortCount();
    if (numInputs2 > 2)
        y = std::max(y, 30 + numInputs2 * 20 + 10);

    int margin = 70; // Wider margin for labels
    int contentWidth = getWidth() - (margin * 2);

    for (int i = 0; i < comboBoxes.size(); ++i) {
        comboLabels[i]->setBounds(margin, y, contentWidth, 20);
        y += 20;
        comboBoxes[i]->setBounds(margin, y, contentWidth, 24);
        y += 30;
    }

    for (int i = 0; i < toggles.size(); ++i) {
        toggles[i]->setBounds(margin, y, contentWidth, 24);
        y += 30;
    }

    int sliderWidth = contentWidth / 2;
    int sliderHeight = 60;

    for (int i = 0; i < sliders.size(); ++i) {
        int row = i / 2;
        int col = i % 2;

        int x = margin + col * sliderWidth;
        int localY = y + row * (sliderHeight + 20);

        sliderLabels[i]->setBounds(x, localY, sliderWidth, 20);
        sliders[i]->setBounds(x, localY + 20, sliderWidth, sliderHeight);
    }

    // Update y to the end of sliders for scope toggle/scope
    int finalSlidersRow = (sliders.size() + 1) / 2;
    y += finalSlidersRow * (sliderHeight + 20);

    if (freqResponseComponent) {
        freqResponseComponent->setBounds(10, y, getWidth() - 20, 120);
        y += 130;
    }

    if (spectrumToggle) {
        spectrumToggle->setBounds(margin, y, contentWidth, 24);
        y += 30;
    }

    if (scopeToggle) {
        scopeToggle->setBounds(margin, y, contentWidth, 24);
        y += 30;
    }

    if (scopeComponent && scopeComponent->isVisible()) {
        scopeComponent->setBounds(10, y, getWidth() - 20, 100);
    }
}

void ModuleComponent::parameterValueChanged(int parameterIndex, float newValue) {
    juce::ignoreUnused(parameterIndex, newValue);
}

void ModuleComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    if (!undoManager || module == nullptr)
        return;

    if (gestureIsStarting) {
        // Capture full graph snapshot at gesture start
        gestureStartValues[parameterIndex] = 1.0f; // flag that gesture is active
        undoManager->captureBeforeState(owner.getAudioEngine().getGraph());
    } else {
        auto it = gestureStartValues.find(parameterIndex);
        if (it != gestureStartValues.end()) {
            // Capture after snapshot and push as undo action
            auto* graphEditor = &owner;
            undoManager->pushSnapshotFromCapture(owner.getAudioEngine().getGraph());
            gestureStartValues.erase(it);
        }
    }
}

void ModuleComponent::mouseDown(const juce::MouseEvent& e) {
    auto port = getPortForPoint(e.getPosition());
    if (port) {
        if (e.mods.isPopupMenu()) {
            // Right click -> Disconnect
            // Show menu? Or just disconnect?
            // User asked for "way to disconnect". Instant disconnect is fast.
            // Or a menu "Disconnect".
            juce::PopupMenu m;
            m.addItem("Disconnect",
                      [this, port] { owner.disconnectPort(this, port->index, port->isInput, port->isMidi); });

            m.showMenuAsync(juce::PopupMenu::Options());
        } else {
            // Start Connection Drag
            owner.beginConnectionDrag(this, port->index, port->isInput, port->isMidi, e.getScreenPosition());
        }
    } else {
        // Click on Body
        if (getType(module) == ModuleType::Attenuverter)
            return; // cannot drag

        if (e.mods.isPopupMenu()) {
            juce::PopupMenu m;

            // Bypass toggle (only for actual modules)
            if (auto* mod = dynamic_cast<ModuleBase*>(module)) {
                m.addItem(mod->isBypassed() ? "Enable Module" : "Bypass Module", [this] {
                    if (auto* mod = dynamic_cast<ModuleBase*>(module)) {
                        mod->setBypassed(!mod->isBypassed());
                        repaint();
                    }
                });
                m.addSeparator();
            }

            // "Replace with..." submenu (only for actual modules, not AudioGraphIOProcessor)
            if (dynamic_cast<ModuleBase*>(module) != nullptr) {
                juce::PopupMenu replaceMenu;
                auto currentType = getType(module);

                struct ModEntry {
                    const char* name;
                    ModuleType type;
                };
                struct Category {
                    const char* header;
                    std::vector<ModEntry> modules;
                };
                std::vector<Category> categories = {
                    {"Sources", {{"Oscillator", ModuleType::Oscillator}, {"LFO", ModuleType::LFO}}},
                    {"Sequencing",
                     {{"Sequencer", ModuleType::Sequencer},
                      {"Poly Sequencer", ModuleType::PolySequencer},
                      {"MIDI Keyboard", ModuleType::MidiKeyboard},
                      {"Poly MIDI", ModuleType::PolyMidi},
                      {"External MIDI", ModuleType::ExternalMidi}}},
                    {"Envelopes & Control", {{"ADSR", ModuleType::ADSR}, {"VCA", ModuleType::VCA}}},
                    {"Filters", {{"Filter", ModuleType::Filter}}},
                    {"Modulation FX",
                     {{"Chorus", ModuleType::Chorus},
                      {"Phaser", ModuleType::Phaser},
                      {"Flanger", ModuleType::Flanger},
                      {"Distortion", ModuleType::Distortion}}},
                    {"Time FX", {{"Delay", ModuleType::Delay}, {"Reverb", ModuleType::Reverb}}},
                    {"Dynamics", {{"Compressor", ModuleType::Compressor}, {"Limiter", ModuleType::Limiter}}},
                };

                for (auto& cat : categories) {
                    juce::PopupMenu catMenu;
                    bool hasItems = false;
                    for (auto& mod : cat.modules) {
                        if (mod.type == currentType)
                            continue;
                        juce::String typeName(mod.name);
                        catMenu.addItem(typeName, [this, typeName] { owner.replaceModule(this, typeName); });
                        hasItems = true;
                    }
                    if (hasItems)
                        replaceMenu.addSubMenu(cat.header, catMenu);
                }

                m.addSubMenu("Replace with...", replaceMenu);
                m.addSeparator();
            }

            m.addItem("Delete Module", [this] { owner.deleteModule(this); });
            m.showMenuAsync(juce::PopupMenu::Options());
        } else {
            dragStartPosition = getPosition();
            if (undoManager)
                undoManager->captureBeforeState(owner.getAudioEngine().getGraph());
            dragger.startDraggingComponent(this, e);
            // Show grid + ghost for this module-body drag.
            owner.beginDragPreview(getWidth(), getHeight(), getNodeId());
        }
    }
}

void ModuleComponent::moved() {
    if (module != nullptr)
        owner.updateModulePosition(this);
}

void ModuleComponent::mouseDrag(const juce::MouseEvent& e) {
    if (getPortForPoint(e.getMouseDownPosition())) {
        owner.dragConnection(e.getScreenPosition());
    } else {
        dragger.dragComponent(this, e, nullptr);
        // Update the landing ghost to follow the live drag position.
        owner.updateDragPreview(getPosition());
        if (auto* p = getParentComponent())
            p->repaint();
    }
}

void ModuleComponent::mouseUp(const juce::MouseEvent& e) {
    if (getPortForPoint(e.getMouseDownPosition())) {
        owner.endConnectionDrag(e.getScreenPosition());
    } else {
        // Clear ghost overlay before finalizing so the overlay is gone at the exact
        // moment the module lands in its snapped position.
        owner.endDragPreview();
        if (getPosition() != dragStartPosition) {
            // Snap to grid and resolve overlap BEFORE the undo snapshot so the
            // snapped/cleared final position is what gets captured in the diff.
            owner.finalizeModuleDrag(this);
            if (undoManager)
                undoManager->pushSnapshotFromCapture(owner.getAudioEngine().getGraph());
        }
    }
}
