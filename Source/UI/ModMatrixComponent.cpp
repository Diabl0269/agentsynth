#include "ModMatrixComponent.h"
#include "../Modules/AttenuverterModule.h"
#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <map>

ModMatrixComponent::ModMatrixComponent(AudioEngine& engine, AppUndoManager* undoMgr)
    : audioEngine(engine)
    , undoManager(undoMgr) {
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentContainer);

    addAndMakeVisible(addButton);
    addButton.setComponentID("addModulation");
    addButton.onClick = [this] { addModulation(); };

    addAndMakeVisible(flatToggle);
    flatToggle.onClick = [this] { setFlatSourceMenu(flatToggle.getToggleState()); };
    flatToggle.setToggleState(isSourceMenuFlat, juce::dontSendNotification);

    startTimerHz(10);
}

ModMatrixComponent::~ModMatrixComponent() { stopTimer(); }

void ModMatrixComponent::setFlatSourceMenu(bool shouldBeFlat) {
    if (isSourceMenuFlat != shouldBeFlat) {
        isSourceMenuFlat = shouldBeFlat;
        for (auto& row : rows)
            row->populateCombos();
    }
}

void ModMatrixComponent::paint(juce::Graphics& g) {
    // Resolve the themed LookAndFeel; in headless tests the default JUCE LnF is installed and the
    // cast returns null, so we fall back to the legacy literals.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    const juce::Colour bgColour = lf != nullptr ? lf->getTheme().colors.bg1 : juce::Colour(0xff1a1c1e);
    const juce::Colour surfaceHiColour =
        lf != nullptr ? lf->getTheme().colors.surfaceHi : juce::Colours::white.withAlpha(0.1f);
    const juce::Colour surfaceColour =
        lf != nullptr ? lf->getTheme().colors.surface : juce::Colours::white.withAlpha(0.05f);
    const juce::Colour textPrimaryColour = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
    const juce::Colour textMutedColour =
        lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::white.withAlpha(0.6f);
    const juce::Colour textDisabledColour =
        lf != nullptr ? lf->getTheme().colors.textDisabled : juce::Colours::white.withAlpha(0.3f);

    g.fillAll(bgColour); // Solid dark background

    auto area = getLocalBounds();
    auto titleArea = area.removeFromTop(40);

    g.setColour(surfaceHiColour);
    g.fillRect(titleArea);

    g.setColour(textPrimaryColour);
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.drawText("MODULATION MATRIX", titleArea.reduced(10, 0), juce::Justification::centredLeft, true);

    // Column Headers
    auto headerArea = area.removeFromTop(30);
    g.setColour(surfaceColour);
    g.fillRect(headerArea);

    g.setColour(textMutedColour);
    g.setFont(juce::Font(12.0f, juce::Font::bold));

    float w = (float)area.getWidth();
    // Headers offset by 30px for the row numbers
    g.drawText("SOURCE", juce::Rectangle<float>(30, (float)headerArea.getY(), (w - 30) * 0.3f, 30.0f),
               juce::Justification::centred, true);
    g.drawText("DESTINATION",
               juce::Rectangle<float>(30 + (w - 30) * 0.3f, (float)headerArea.getY(), (w - 30) * 0.35f, 30.0f),
               juce::Justification::centred, true);
    g.drawText("AMOUNT",
               juce::Rectangle<float>(30 + (w - 30) * 0.65f, (float)headerArea.getY(), (w - 30) * 0.25f, 30.0f),
               juce::Justification::centred, true);

    if (rows.empty()) {
        g.setColour(textDisabledColour);
        g.setFont(juce::Font(14.0f, juce::Font::italic));
        g.drawText("No modulations active.\nClick '+ Add Modulation' to start.", getLocalBounds(),
                   juce::Justification::centred, true);
    }
}

void ModMatrixComponent::resized() {
    auto area = getLocalBounds();
    auto titleArea = area.removeFromTop(40); // Title
    area.removeFromTop(30);                  // Column Headers

    auto footer = area.removeFromBottom(40);
    addButton.setBounds(footer.removeFromRight(150).reduced(5));
    flatToggle.setBounds(footer.removeFromLeft(120).reduced(5));

    viewport.setBounds(area);

    const int contentWidth = viewport.getWidth(); // Don't subtract for scrollbar yet, JUCE handles it
    contentContainer.setBounds(0, 0, contentWidth, (int)rows.size() * kRowHeight);

    for (int i = 0; i < (int)rows.size(); ++i) {
        rows[i]->setBounds(0, i * kRowHeight, contentWidth, kRowHeight);
    }
}

void ModMatrixComponent::timerCallback() { updateRowsFromGraph(); }

void ModMatrixComponent::updateRowsFromGraph() {
    auto activeRoutings = audioEngine.getActiveModRoutings();

    // Stable sort by NodeID so row numbers are consistent
    std::sort(activeRoutings.begin(), activeRoutings.end(),
              [](const AudioEngine::ModRoutingInfo& a, const AudioEngine::ModRoutingInfo& b) {
                  return a.attenuverterNodeID.uid < b.attenuverterNodeID.uid;
              });

    bool componentsChanged = false;

    // Check for removed rows
    for (int i = (int)rows.size() - 1; i >= 0; --i) {
        bool found = false;
        for (const auto& routing : activeRoutings) {
            if (routing.attenuverterNodeID == rows[i]->attenuverterId) {
                found = true;
                break;
            }
        }
        if (!found) {
            rows.erase(rows.begin() + i);
            componentsChanged = true;
        }
    }

    // Check for new or changed rows
    for (const auto& routing : activeRoutings) {
        bool existingFound = false;
        for (auto& row : rows) {
            if (row->attenuverterId == routing.attenuverterNodeID) {
                row->refresh(routing);
                existingFound = true;
                break;
            }
        }

        if (!existingFound) {
            auto newRow = std::make_unique<ModRow>(*this, routing.attenuverterNodeID);
            newRow->populateCombos();
            contentContainer.addAndMakeVisible(*newRow);
            rows.push_back(std::move(newRow));
            componentsChanged = true;
        }
    }

    // Assign indices for display
    for (int i = 0; i < (int)rows.size(); ++i) {
        rows[i]->setRowIndex(i);
        // Find corresponding routing for refresh
        for (const auto& r : activeRoutings) {
            if (r.attenuverterNodeID == rows[i]->attenuverterId) {
                rows[i]->refresh(r);
                break;
            }
        }
    }

    // Refresh combo items if the number of nodes in graph changed
    int currentNodeCount = audioEngine.getGraph().getNumNodes();
    if (currentNodeCount != lastNodeCount) {
        audioEngine.updateModuleNames();
        for (auto& row : rows)
            row->populateCombos();
        lastNodeCount = currentNodeCount;
    }

    if (componentsChanged) {
        setHoveredRow(-1);
        resized();
        repaint();
    }
}

void ModMatrixComponent::addModulation() {
    // Just add an unconnected attenuverter node to create an "empty" row
    audioEngine.addEmptyModRouting();
    updateRowsFromGraph();
}

// --- ModRow Implementation ---

ModMatrixComponent::ModRow::ModRow(ModMatrixComponent& o, juce::AudioProcessorGraph::NodeID id)
    : owner(o)
    , attenuverterId(id) {
    addAndMakeVisible(sourceCombo);
    addAndMakeVisible(destCombo);
    addAndMakeVisible(amountSlider);
    addAndMakeVisible(bypassToggle);
    addAndMakeVisible(deleteButton);

    amountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    amountSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    amountSlider.setRange(-1.0, 1.0);

    // Slider colours from theme tokens when the themed LnF is installed; otherwise leave the
    // ColourId defaults (the LnF maps them in applyTheme(), so explicit literals are not needed).
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&owner.getLookAndFeel())) {
        const auto& colors = lf->getTheme().colors;
        amountSlider.setColour(juce::Slider::thumbColourId, colors.knobPointer);
        amountSlider.setColour(juce::Slider::trackColourId, colors.accent);
        amountSlider.setColour(juce::Slider::backgroundColourId, colors.surface);
    }

    sourceCombo.addListener(this);
    destCombo.addListener(this);
    deleteButton.onClick = [this] {
        if (owner.undoManager) {
            owner.undoManager->recordStructuralChange(owner.audioEngine.getGraph(),
                                                      [this] { owner.audioEngine.removeModRouting(attenuverterId); });
        } else {
            owner.audioEngine.removeModRouting(attenuverterId);
        }
    };

    bypassToggle.setClickingTogglesState(true);
    bypassToggle.setTooltip("Bypass modulation");
    // Keep only the semantic on-colour from the theme (bypass-active = warning). The off-state,
    // text and base colours re-skin via the LnF ColourId mapping. When unthemed, leave defaults.
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&owner.getLookAndFeel())) {
        const auto& colors = lf->getTheme().colors;
        bypassToggle.setColour(juce::TextButton::buttonOnColourId, colors.warning);
        bypassToggle.setColour(juce::TextButton::textColourOnId, colors.bg0);
    }
    bypassToggle.setComponentID("modBypass");
    deleteButton.setComponentID("modDelete");

    // Attach to attenuverter params
    if (auto* node = owner.audioEngine.getGraph().getNodeForId(attenuverterId)) {
        const auto& params = node->getProcessor()->getParameters();
        if (params.size() > 0) {
            if (auto* bParam = dynamic_cast<juce::AudioParameterBool*>(params[0])) {
                bypassAttachment = std::make_unique<juce::ButtonParameterAttachment>(*bParam, bypassToggle);
            }
        }
        if (params.size() > 1) {
            if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(params[1])) {
                amountAttachment = std::make_unique<juce::SliderParameterAttachment>(*param, amountSlider);
            }
        }

        // Register as parameter listener for undo tracking
        if (owner.undoManager) {
            for (auto* p : params)
                p->addListener(this);
        }
    }
}

void ModMatrixComponent::ModRow::resized() {
    auto area = getLocalBounds().reduced(2);

    // Space for row number
    area.removeFromLeft(30);

    // Proportional layout
    int totalWidth = area.getWidth();
    sourceCombo.setBounds(area.removeFromLeft(totalWidth * 0.3f).reduced(2));
    destCombo.setBounds(area.removeFromLeft(totalWidth * 0.35f).reduced(2));
    deleteButton.setBounds(area.removeFromRight(30).reduced(2));
    bypassToggle.setBounds(area.removeFromRight(30).reduced(2));
    amountSlider.setBounds(area.reduced(2));
}

void ModMatrixComponent::ModRow::paint(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&owner.getLookAndFeel());

    // --- Row background: zebra striping + hover highlight ---
    const bool isHovered = (owner.getHoveredRow() == rowIndex);
    juce::Colour rowBg;
    if (isHovered) {
        // Hover: accent at low alpha over the base surface so it works on both even/odd rows.
        const juce::Colour accentColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
        rowBg = accentColour.withAlpha(0.10f);
    } else if (ModMatrixComponent::isZebraRow(rowIndex)) {
        // Odd rows: slightly raised surface to distinguish from even rows.
        rowBg =
            lf != nullptr ? lf->getTheme().colors.surfaceHi.withAlpha(0.45f) : juce::Colours::white.withAlpha(0.04f);
    } else {
        // Even rows: transparent (parent bg1 shows through).
        rowBg = juce::Colours::transparentBlack;
    }
    g.setColour(rowBg);
    g.fillRect(getLocalBounds());

    // --- Row number label ---
    g.setColour(lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::white.withAlpha(0.6f));
    g.setFont(12.0f);
    g.drawText(juce::String(rowIndex + 1), 0, 0, 30, getHeight(), juce::Justification::centred);
}

void ModMatrixComponent::ModRow::mouseEnter(const juce::MouseEvent& /*e*/) { owner.setHoveredRow(rowIndex); }

void ModMatrixComponent::ModRow::mouseExit(const juce::MouseEvent& /*e*/) {
    // Only clear if this row still owns the hover (avoids races when moving between rows).
    if (owner.getHoveredRow() == rowIndex)
        owner.setHoveredRow(-1);
}

ModMatrixComponent::ModRow::~ModRow() {
    detach();
    sourceCombo.removeListener(this);
    destCombo.removeListener(this);
}

void ModMatrixComponent::ModRow::detach() {
    amountAttachment.reset();
    bypassAttachment.reset();

    if (auto* node = owner.audioEngine.getGraph().getNodeForId(attenuverterId)) {
        for (auto* p : node->getProcessor()->getParameters())
            p->removeListener(this);
    }
}

void ModMatrixComponent::detachAllRows() {
    for (auto& row : rows)
        row->detach();
}

void ModMatrixComponent::setHoveredRow(int rowIndex) {
    if (hoveredRow_ == rowIndex)
        return;
    hoveredRow_ = rowIndex;
    // Repaint only the content area so the hover highlight updates without a full component repaint.
    contentContainer.repaint();
}

void ModMatrixComponent::ModRow::parameterValueChanged(int parameterIndex, float newValue) {
    juce::ignoreUnused(parameterIndex, newValue);
}

void ModMatrixComponent::ModRow::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {
    if (!owner.undoManager)
        return;

    if (gestureIsStarting) {
        gestureStartValues[parameterIndex] = 1.0f;
        owner.undoManager->captureBeforeState(owner.audioEngine.getGraph());
    } else {
        auto it = gestureStartValues.find(parameterIndex);
        if (it != gestureStartValues.end()) {
            owner.undoManager->pushSnapshotFromCapture(owner.audioEngine.getGraph());
            gestureStartValues.erase(it);
        }
    }
}

void ModMatrixComponent::ModRow::populateCombos() {
    sourceCombo.clear(juce::dontSendNotification);
    destCombo.clear(juce::dontSendNotification);

    auto& graph = owner.audioEngine.getGraph();
    bool useGroups = !owner.isSourceMenuFlat;

    std::map<ModulationCategory, juce::String> categoryNames = {{ModulationCategory::Envelope, "Envelopes"},
                                                                {ModulationCategory::LFO, "LFOs"},
                                                                {ModulationCategory::Oscillator, "Oscillators"},
                                                                {ModulationCategory::Sequencer, "Sequencers"},
                                                                {ModulationCategory::Filter, "Filters"},
                                                                {ModulationCategory::FX, "Effects"},
                                                                {ModulationCategory::Other, "Other"}};

    std::map<ModulationCategory, std::vector<juce::AudioProcessorGraph::Node*>> modulesByCategory;

    for (auto* node : graph.getNodes()) {
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor())) {
            modulesByCategory[module->getModulationCategory()].push_back(node);
        }
    }

    if (!useGroups) {
        // Flat list implementation
        for (auto const& [cat, modules] : modulesByCategory) {
            for (auto* node : modules) {
                auto* module = static_cast<ModuleBase*>(node->getProcessor());
                juce::String displayName = module->getName();

                for (int i = 0; i < module->getTotalNumOutputChannels(); ++i) {
                    int itemId = (int)((node->nodeID.uid << 8) | (uint32_t)i);
                    juce::String label = displayName;
                    if (module->getTotalNumOutputChannels() > 1)
                        label += " Out " + juce::String(i + 1);
                    sourceCombo.addItem(label, itemId);
                }

                auto targets = module->getModulationTargets();
                for (const auto& target : targets) {
                    int itemId = (int)((node->nodeID.uid << 8) | (uint32_t)target.channelIndex);
                    destCombo.addItem(displayName + ": " + target.name, itemId);
                }
            }
        }
    } else {
        // Nested Menu Implementation
        juce::PopupMenu sourceMenu;
        juce::PopupMenu destMenu;

        for (auto const& [cat, modules] : modulesByCategory) {
            juce::PopupMenu catSourceSub;
            juce::PopupMenu catDestSub;
            int sourceItems = 0;
            int destItems = 0;

            for (auto* node : modules) {
                auto* module = static_cast<ModuleBase*>(node->getProcessor());
                juce::String displayName = module->getName();

                if (module->getTotalNumOutputChannels() > 0) {
                    if (module->getTotalNumOutputChannels() == 1) {
                        int itemId = (int)((node->nodeID.uid << 8) | 0);
                        catSourceSub.addItem(itemId, displayName);
                        sourceCombo.addItem(displayName, itemId);
                    } else {
                        juce::PopupMenu instSourceSub;
                        for (int i = 0; i < module->getTotalNumOutputChannels(); ++i) {
                            int itemId = (int)((node->nodeID.uid << 8) | (uint32_t)i);
                            instSourceSub.addItem(itemId, "Out " + juce::String(i + 1));
                            sourceCombo.addItem(displayName + " Out " + juce::String(i + 1), itemId);
                        }
                        catSourceSub.addSubMenu(displayName, instSourceSub);
                    }
                    sourceItems++;
                }

                auto targets = module->getModulationTargets();
                if (!targets.empty()) {
                    juce::PopupMenu instDestSub;
                    for (const auto& target : targets) {
                        int itemId = (int)((node->nodeID.uid << 8) | (uint32_t)target.channelIndex);
                        instDestSub.addItem(itemId, target.name);
                        destCombo.addItem(displayName + ": " + target.name, itemId);
                    }
                    catDestSub.addSubMenu(displayName, instDestSub);
                    destItems++;
                }
            }

            if (sourceItems > 0)
                sourceMenu.addSubMenu(categoryNames[cat], catSourceSub);
            if (destItems > 0)
                destMenu.addSubMenu(categoryNames[cat], catDestSub);
        }

        *sourceCombo.getRootMenu() = sourceMenu;
        *destCombo.getRootMenu() = destMenu;
    }
}

void ModMatrixComponent::ModRow::refresh(const AudioEngine::ModRoutingInfo& info) {
    int srcId = (int)((info.sourceNodeID.uid << 8) | (uint32_t)info.sourceChannelIndex);
    sourceCombo.setSelectedId(srcId, juce::dontSendNotification);
    int destId = (int)((info.destNodeID.uid << 8) | (uint32_t)info.destChannelIndex);
    destCombo.setSelectedId(destId, juce::dontSendNotification);
}

void ModMatrixComponent::ModRow::comboBoxChanged(juce::ComboBox* comboBox) {
    if (comboBox == &sourceCombo || comboBox == &destCombo) {
        uint32_t srcEncoded = (uint32_t)sourceCombo.getSelectedId();
        uint32_t destEncoded = (uint32_t)destCombo.getSelectedId();

        uint32_t srcNodeId = srcEncoded >> 8;
        int srcChannel = (int)(srcEncoded & 0xFF);

        uint32_t dstNodeId = destEncoded >> 8;
        int dstChannel = (int)(destEncoded & 0xFF);

        if (srcNodeId != 0 || dstNodeId != 0) {
            auto doReroute = [this, srcNodeId, srcChannel, dstNodeId, dstChannel] {
                auto& graph = owner.audioEngine.getGraph();

                for (auto& conn : graph.getConnections()) {
                    if (conn.destination.nodeID == attenuverterId && conn.destination.channelIndex == 0) {
                        graph.removeConnection(conn);
                        break;
                    }
                }
                if (srcNodeId != 0)
                    graph.addConnection(
                        {{juce::AudioProcessorGraph::NodeID(srcNodeId), srcChannel}, {attenuverterId, 0}});

                for (auto& conn : graph.getConnections()) {
                    if (conn.source.nodeID == attenuverterId && conn.source.channelIndex == 0) {
                        graph.removeConnection(conn);
                        break;
                    }
                }
                if (dstNodeId != 0)
                    graph.addConnection(
                        {{attenuverterId, 0}, {juce::AudioProcessorGraph::NodeID(dstNodeId), dstChannel}});
            };

            if (owner.undoManager) {
                owner.undoManager->recordStructuralChange(owner.audioEngine.getGraph(), doReroute);
            } else {
                doReroute();
            }
        }
    }
}
