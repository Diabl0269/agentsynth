#include "ModuleComponent.h"
#include "../Modules/ExternalMidiModule.h"
#include "../Modules/ModuleBase.h"
#include "../Modules/SequencerModule.h"
#include "GraphEditor.h"

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
            scopeComponent = std::make_unique<ScopeComponent>(*vb);
            addAndMakeVisible(scopeComponent.get());

            scopeToggle = std::make_unique<juce::ToggleButton>("Show Scope");
            bool isFilter = (getType(module) == ModuleType::Filter);
            scopeToggle->setToggleState(!isFilter, juce::dontSendNotification);
            scopeComponent->setVisible(!isFilter);
            scopeToggle->onClick = [this] {
                scopeComponent->setVisible(scopeToggle->getToggleState());
                updateLayout();
            };
            addAndMakeVisible(scopeToggle.get());
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
        bypassButton = std::make_unique<juce::TextButton>("B");
        bypassButton->setClickingTogglesState(true);
        bypassButton->setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
        bypassButton->setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
        bypassButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        bypassButton->setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible(*bypassButton);
    }

    setTitle(module->getName());
    createControls();
    startTimerHz(30); // 30 FPS for step visualization
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
        sliderAttachments.clear();
        comboAttachments.clear();
        buttonAttachments.clear();
    } else {
        // Processor already freed — leak attachments to avoid use-after-free
        // in ~ParameterAttachment which calls parameter->removeListener()
        (void)bypassAttachment.release();
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

    repaint();
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

        deviceCombo->onChange = [extMidi, deviceCombo, devices]() {
            int selectedId = deviceCombo->getSelectedId();
            if (selectedId > 1) {
                extMidi->setMidiDeviceName(devices[selectedId - 2].name);
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
                if (boolParam->paramID == "bypassed")
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
                    break;
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
    if (getType(module) == ModuleType::Sequencer) {
        setSize(510, 380); // 8 cols * 60 + margins, 3 rows
        return;
    }

    updateLayout();
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

    if (getType(module) == ModuleType::Sequencer) {
        setSize(510, 380);
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

    g.fillAll(isBypassed ? juce::Colours::lightgrey.withAlpha(0.4f) : juce::Colours::lightgrey);

    // Activity glow indicator
    if (cachedRMS > 0.01f && !isBypassed) {
        float glowAlpha = juce::jlimit(0.0f, 0.3f, cachedRMS * 0.5f);
        g.setColour(juce::Colours::limegreen.withAlpha(glowAlpha));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 3.0f);
    }

    // Highlight Active Step (Sequencer only)
    if (getType(module) == ModuleType::Sequencer) {
        if (auto* seq = dynamic_cast<SequencerModule*>(module)) {
            int activeStep = seq->currentActiveStep.load();
            // Coordinates match resized()
            int startX = 10;
            int stepWidth = 60;
            int x = startX + activeStep * stepWidth;

            g.setColour(juce::Colours::yellow.withAlpha(0.3f));
            g.fillRect(x, 110, stepWidth - 5, 220); // Cover Gate+Pitch+F.Env area
        }
    }

    g.setColour(juce::Colours::black);
    g.drawRect(getLocalBounds(), 2);

    // Header
    g.setColour(isBypassed ? juce::Colour(0xff555555) : juce::Colours::darkgrey);
    g.fillRect(0, 0, getWidth(), 24);
    g.setColour(juce::Colours::white);
    g.drawText(module->getName(), 0, 0, getWidth(), 24, juce::Justification::centred, true);

    // Activity LED in header
    if (cachedRMS > 0.01f && !isBypassed) {
        float ledAlpha = juce::jlimit(0.3f, 1.0f, cachedRMS * 2.0f);
        g.setColour(juce::Colours::limegreen.withAlpha(ledAlpha));
        g.fillEllipse(6.0f, 8.0f, 8.0f, 8.0f);
    }

    // --- PORTS ---
    int numIns = module->getTotalNumInputChannels();
    int numOuts = module->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(module)) {
        numIns = mb->getVisibleInputPortCount();
        numOuts = mb->getVisibleOutputPortCount();
    }
    bool midiOutDrawn = false; // Reintroduced
                               // MIDI Output (Top Right if produces midi)
    if (module->producesMidi()) {
        g.setColour(juce::Colours::white);
        auto p = juce::Point<int>(getWidth() - 10, 30); // Top right, below header
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);
        g.drawText("Midi Out", p.x - 65, p.y - 5, 60, 10, juce::Justification::right, false);
    }
    // MIDI Input (Top Left if accepts midi components)
    if (module->acceptsMidi()) {
        g.setColour(juce::Colours::white);
        auto p = juce::Point<int>(10, 30); // Top left near header
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);
        g.drawText("Midi In", p.x + 10, p.y - 5, 60, 10, juce::Justification::left, false);
    }

    // Inputs
    g.setColour(juce::Colours::yellow);
    for (int i = 0; i < numIns; ++i) {
        auto p = getPortCenter(i, true);
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);

        juce::String label = "In " + juce::String(i);
        if (auto* mb = dynamic_cast<ModuleBase*>(module))
            label = mb->getInputPortLabel(i);
        else if (dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(module))
            label = (i == 0) ? "Left" : (i == 1) ? "Right" : "In " + juce::String(i);

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
        g.fillEllipse(p.x - 5, p.y - 5, 10, 10);

        juce::String label = "Out " + juce::String(i);
        if (auto* mb = dynamic_cast<ModuleBase*>(module))
            label = mb->getOutputPortLabel(i);
        else if (dynamic_cast<juce::AudioProcessorGraph::AudioGraphIOProcessor*>(module))
            label = (i == 0) ? "Left" : (i == 1) ? "Right" : "Out " + juce::String(i);
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

                constexpr float startAngle = -juce::MathConstants<float>::pi * 0.75f;
                constexpr float endAngle = juce::MathConstants<float>::pi * 0.75f;
                float baseAngle = juce::jmap(baseNorm, 0.0f, 1.0f, startAngle, endAngle);
                float modAngle = juce::jmap(modNorm, 0.0f, 1.0f, startAngle, endAngle);

                if (std::abs(modAngle - baseAngle) > 0.01f) {
                    juce::Path arc;
                    arc.addCentredArc(cx, cy, radius, radius, 0.0f, std::min(baseAngle, modAngle),
                                      std::max(baseAngle, modAngle), true);
                    bool isPositive = info.modSignalValue >= 0.0f;
                    auto ringColour = isPositive ? juce::Colour(0xff00e5ff) : juce::Colour(0xffff6e00);
                    g.setColour(ringColour);
                    g.strokePath(arc, juce::PathStrokeType(3.5f));
                }
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

    if (isInput) {
        return {10, headerHeight + portOffset + index * yStep + 20}; // Left side, apply offset
    } else {
        // No additional midiOffset for outputs here, as MIDI out is now fixed.
        return {getWidth() - 10, headerHeight + portOffset + index * yStep + 20}; // Right side, apply offset
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

    if (bypassButton)
        bypassButton->setBounds(getWidth() - 26, 2, 22, 20);

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

        // Steps Row
        // Steps Row
        int startX = 10;
        int startY = 110;
        int stepWidth = 60;

        for (int step = 1; step <= 8; ++step) {
            int colX = startX + (step - 1) * stepWidth;

            // Gate
            juce::String gateId = "Gate " + juce::String(step);
            for (int i = 0; i < sliders.size(); ++i) {
                if (sliders[i]->getComponentID().equalsIgnoreCase(gateId)) {
                    sliderLabels[i]->setBounds(colX, startY, stepWidth - 5, 20);
                    sliders[i]->setBounds(colX, startY + 20, stepWidth - 5, 50);
                }
            }

            // Pitch
            juce::String pitchId = "Pitch " + juce::String(step);
            for (int i = 0; i < sliders.size(); ++i) {
                if (sliders[i]->getComponentID().equalsIgnoreCase(pitchId)) {
                    sliderLabels[i]->setBounds(colX, startY + 80, stepWidth - 5, 20);
                    sliders[i]->setBounds(colX, startY + 100, stepWidth - 5, 50);
                }
            }

            // Filter Env
            juce::String fEnvId = "F.Env " + juce::String(step);
            for (int i = 0; i < sliders.size(); ++i) {
                if (sliders[i]->getComponentID().equalsIgnoreCase(fEnvId)) {
                    sliderLabels[i]->setBounds(colX, startY + 160, stepWidth - 5, 20);
                    sliders[i]->setBounds(colX, startY + 180, stepWidth - 5, 50);
                }
            }
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
        setSize(500, 150); // Appropriate size for a keyboard
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
        if (auto* p = getParentComponent())
            p->repaint();
    }
}

void ModuleComponent::mouseUp(const juce::MouseEvent& e) {
    if (getPortForPoint(e.getMouseDownPosition())) {
        owner.endConnectionDrag(e.getScreenPosition());
    } else if (undoManager && getPosition() != dragStartPosition) {
        undoManager->pushSnapshotFromCapture(owner.getAudioEngine().getGraph());
    }
}
