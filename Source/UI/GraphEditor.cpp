#include "GraphEditor.h"
#include "../AI/AIStateMapper.h"
#include "../Modules/ADSRModule.h"
#include "../Modules/AttenuverterModule.h"
#include "../Modules/ExternalMidiModule.h"
#include "../Modules/FX/ChorusModule.h"
#include "../Modules/FX/CompressorModule.h"
#include "../Modules/FX/DelayModule.h"
#include "../Modules/FX/DistortionModule.h"
#include "../Modules/FX/FlangerModule.h"
#include "../Modules/FX/LimiterModule.h"
#include "../Modules/FX/PhaserModule.h"
#include "../Modules/FX/ReverbModule.h"
#include "../Modules/FilterModule.h"
#include "../Modules/LFOModule.h"
#include "../Modules/MidiKeyboardModule.h"
#include "../Modules/OscillatorModule.h"
#include "../Modules/PolyMidiModule.h"
#include "../Modules/SequencerModule.h"
#include "../Modules/VCAModule.h"
#include "../Modules/VoiceMixerModule.h"
#include "ModuleComponent.h"

GraphEditor::GraphEditor(AudioEngine& engine, GravisynthUndoManager* undoMgr)
    : audioEngine(engine)
    , content(*this)
    , modMatrix(engine, undoMgr)
    , undoManager(undoMgr) {
    addAndMakeVisible(content);
    addAndMakeVisible(modMatrix);
    content.setInterceptsMouseClicks(false, true); // Fallback clicks to parent
    startTimerHz(30);
}

GraphEditor::~GraphEditor() { stopTimer(); }

GraphEditor::GraphContentComponent::GraphContentComponent(GraphEditor& ed)
    : editor(ed) {}

void GraphEditor::GraphContentComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::darkgrey);

    // Draw connections
    auto& graph = editor.audioEngine.getGraph();
    g.setColour(juce::Colours::yellow);

    for (auto& connection : graph.getConnections()) {
        auto* node1 = graph.getNodeForId(connection.source.nodeID);
        auto* node2 = graph.getNodeForId(connection.destination.nodeID);

        if (!node1 || !node2)
            continue;

        // Hide poly connections that exceed visible port counts
        // Skip drawing if source channel exceeds visible output ports
        if (connection.source.channelIndex != juce::AudioProcessorGraph::midiChannelIndex) {
            if (auto* srcMb = dynamic_cast<ModuleBase*>(node1->getProcessor())) {
                if (connection.source.channelIndex >= srcMb->getVisibleOutputPortCount())
                    continue;
            }
        }

        // Skip drawing if destination channel exceeds visible input ports
        if (connection.destination.channelIndex != juce::AudioProcessorGraph::midiChannelIndex) {
            if (auto* dstMb = dynamic_cast<ModuleBase*>(node2->getProcessor())) {
                if (connection.destination.channelIndex >= dstMb->getVisibleInputPortCount())
                    continue;
            }
        }

        if (dynamic_cast<AttenuverterModule*>(node2->getProcessor()) != nullptr) {
            juce::AudioProcessorGraph::Node* realDstNode = nullptr;
            int realDstPort = 0;
            for (auto& c : graph.getConnections()) {
                if (c.source.nodeID == node2->nodeID) {
                    realDstNode = graph.getNodeForId(c.destination.nodeID);
                    realDstPort = c.destination.channelIndex;
                    break;
                }
            }

            if (realDstNode) {
                juce::Point<int> p1, p2;
                bool found1 = false, found2 = false;
                for (auto* comp : moduleComponents) {
                    if (comp->getModule() == node1->getProcessor()) {
                        auto localP = comp->getPortCenter(connection.source.channelIndex, false);
                        p1 = comp->getBounds().getPosition() + localP;
                        found1 = true;
                    }
                    if (comp->getModule() == realDstNode->getProcessor()) {
                        auto localP = comp->getPortCenter(realDstPort, true);
                        p2 = comp->getBounds().getPosition() + localP;
                        found2 = true;
                    }
                }

                if (found1 && found2) {
                    // Pulse modulation line based on signal activity
                    float modPeak = 0.0f;
                    for (auto& info : editor.cachedModDisplayInfo) {
                        if (info.attenuverterNodeID == node2->nodeID) {
                            modPeak = info.modSignalPeak;
                            break;
                        }
                    }
                    float lineWidth = 2.0f + modPeak * 2.0f;
                    float brightness = juce::jlimit(0.5f, 1.0f, 0.5f + modPeak * 0.5f);
                    g.setColour(juce::Colours::yellow.withMultipliedBrightness(brightness));
                    g.drawLine(p1.toFloat().x, p1.toFloat().y, p2.toFloat().x, p2.toFloat().y, lineWidth);

                    float amt = 0.0f;
                    if (auto* p = node2->getProcessor()->getParameters()[1]) {
                        amt = p->getValue();
                        amt = amt * 2.0f - 1.0f; // 0 to 1 back to -1.0 to 1.0
                    }

                    auto mid = (p1 + p2) / 2;
                    juce::Rectangle<float> knobArea(mid.toFloat().x - 10, mid.toFloat().y - 10, 20, 20);
                    g.setColour(juce::Colours::darkgrey);
                    g.fillEllipse(knobArea);
                    g.setColour(juce::Colours::white);
                    g.drawEllipse(knobArea, 1.0f);

                    float angle = juce::jmap(amt, -1.0f, 1.0f, -juce::MathConstants<float>::pi * 0.75f,
                                             juce::MathConstants<float>::pi * 0.75f);
                    float dx = std::sin(angle) * 8.0f;
                    float dy = -std::cos(angle) * 8.0f;
                    g.drawLine(mid.toFloat().x, mid.toFloat().y, mid.toFloat().x + dx, mid.toFloat().y + dy, 2.0f);

                    // Animated dots along modulation connection
                    for (int d = 0; d < 3; ++d) {
                        float t = std::fmod(connectionAnimPhase + (float)d / 3.0f, 1.0f);
                        float dotX = p1.toFloat().x + (p2.toFloat().x - p1.toFloat().x) * t;
                        float dotY = p1.toFloat().y + (p2.toFloat().y - p1.toFloat().y) * t;
                        g.setColour(juce::Colours::cyan.withAlpha(0.7f));
                        g.fillEllipse(dotX - 2.5f, dotY - 2.5f, 5.0f, 5.0f);
                    }
                }
            }
            continue;
        } else if (dynamic_cast<AttenuverterModule*>(node1->getProcessor()) != nullptr) {
            continue;
        }

        juce::Point<int> p1, p2;
        bool found1 = false;
        bool found2 = false;

        for (auto* comp : moduleComponents) {
            if (comp->getModule() == node1->getProcessor()) {
                juce::Point<int> localP;
                if (connection.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex) {
                    localP = comp->getPortCenter(0, false);
                } else {
                    localP = comp->getPortCenter(connection.source.channelIndex, false);
                }
                p1 = comp->getBounds().getPosition() + localP;
                found1 = true;
            }
            if (comp->getModule() == node2->getProcessor()) {
                juce::Point<int> localP;
                if (connection.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex) {
                    localP = juce::Point<int>(10, 30);
                } else {
                    localP = comp->getPortCenter(connection.destination.channelIndex, true);
                }
                p2 = comp->getBounds().getPosition() + localP;
                found2 = true;
            }
        }

        if (found1 && found2) {
            g.drawLine(p1.toFloat().x, p1.toFloat().y, p2.toFloat().x, p2.toFloat().y, 2.0f);

            // Animated dots along audio connection
            for (int d = 0; d < 3; ++d) {
                float t = std::fmod(connectionAnimPhase + (float)d / 3.0f, 1.0f);
                float dotX = p1.toFloat().x + (p2.toFloat().x - p1.toFloat().x) * t;
                float dotY = p1.toFloat().y + (p2.toFloat().y - p1.toFloat().y) * t;
                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.fillEllipse(dotX - 2.5f, dotY - 2.5f, 5.0f, 5.0f);
            }
        }
    }

    // Draw Line being dragged
    if (editor.isDraggingConnection) {
        g.setColour(juce::Colours::white);
        if (editor.dragSourceModule) {
            juce::Point<int> p;
            if (editor.dragSourceIsMidi) {
                if (editor.dragSourceIsInput)
                    p = juce::Point<int>(10, 30);
                else
                    p = editor.dragSourceModule->getPortCenter(0, false);
            } else {
                p = editor.dragSourceModule->getPortCenter(editor.dragSourceChannel, editor.dragSourceIsInput);
            }

            auto posInContent = editor.dragSourceModule->getBounds().getPosition() + p;
            auto mouseInContent = getLocalPoint(&(editor), editor.dragCurrentPos);

            // Since 'content' is transformed, we need to handle coordinates
            // carefully. But if this 'paint' is called on content, and we use
            // getLocalPoint(editor, ...) it should be transformed back. Actually,
            // easier: editor.dragCurrentPos is screen pos.
            auto mouseLocal = getLocalPoint(nullptr, editor.dragCurrentPos);

            g.drawLine(posInContent.toFloat().x, posInContent.toFloat().y, mouseLocal.toFloat().x,
                       mouseLocal.toFloat().y, 3.0f);
        }
    }

    g.setColour(juce::Colours::white);
    g.drawText("Gravisynth (Double click to refresh)", getLocalBounds().removeFromTop(20), juce::Justification::centred,
               true);
}

void GraphEditor::GraphContentComponent::resized() {}

void GraphEditor::beginConnectionDrag(ModuleComponent* sourceModule, int channelIndex, bool isInput, bool isMidi,
                                      juce::Point<int> screenPos) {
    isDraggingConnection = true;
    dragSourceModule = sourceModule;
    dragSourceChannel = channelIndex;
    dragSourceIsInput = isInput;
    dragSourceIsMidi = isMidi;
    dragCurrentPos = screenPos;
    content.repaint();
}

void GraphEditor::dragConnection(juce::Point<int> screenPos) {
    if (!isDraggingConnection)
        return;
    dragCurrentPos = screenPos;
    content.repaint();
}

void GraphEditor::endConnectionDrag(juce::Point<int> screenPos) {
    if (!isDraggingConnection)
        return;

    // Hit test for target in content space
    auto contentPos = content.getLocalPoint(nullptr, screenPos);

    for (auto* comp : content.getModules()) {
        auto localPos = comp->getLocalPoint(nullptr, screenPos);
        auto port = comp->getPortForPoint(localPos);

        if (port) {
            if (comp == dragSourceModule)
                continue;
            if (port->isInput == dragSourceIsInput)
                continue;
            if (port->isMidi != dragSourceIsMidi)
                continue;

            auto& graph = audioEngine.getGraph();
            juce::AudioProcessorGraph::Node* srcNode = nullptr;
            juce::AudioProcessorGraph::Node* dstNode = nullptr;

            for (auto* n : graph.getNodes()) {
                if (n->getProcessor() == dragSourceModule->getModule())
                    srcNode = n;
                if (n->getProcessor() == comp->getModule())
                    dstNode = n;
            }

            if (srcNode && dstNode) {
                auto* realSrc = dragSourceIsInput ? dstNode : srcNode;
                auto* realDst = dragSourceIsInput ? srcNode : dstNode;
                int sPort = dragSourceIsInput ? port->index : dragSourceChannel;
                int dPort = dragSourceIsInput ? dragSourceChannel : port->index;

                if (undoManager) {
                    auto srcId = realSrc->nodeID;
                    auto dstId = realDst->nodeID;
                    bool isMidiConn = dragSourceIsMidi;
                    bool isCV = false;
                    if (!isMidiConn) {
                        if (auto* modBase = dynamic_cast<ModuleBase*>(realDst->getProcessor())) {
                            for (const auto& t : modBase->getModulationTargets()) {
                                if (t.channelIndex == dPort) {
                                    isCV = true;
                                    break;
                                }
                            }
                        }
                    }
                    undoManager->recordStructuralChange(
                        graph, [this, &graph, srcId, dstId, sPort, dPort, isMidiConn, isCV] {
                            if (isMidiConn) {
                                graph.addConnection({{srcId, juce::AudioProcessorGraph::midiChannelIndex},
                                                     {dstId, juce::AudioProcessorGraph::midiChannelIndex}});
                            } else if (isCV) {
                                audioEngine.addModRouting(srcId, sPort, dstId, dPort);
                            } else {
                                graph.addConnection({{srcId, sPort}, {dstId, dPort}});
                            }
                        });
                } else {
                    if (dragSourceIsMidi) {
                        graph.addConnection({{realSrc->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                             {realDst->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});
                    } else {
                        bool isCV = false;
                        if (auto* modBase = dynamic_cast<ModuleBase*>(realDst->getProcessor())) {
                            for (const auto& t : modBase->getModulationTargets()) {
                                if (t.channelIndex == dPort) {
                                    isCV = true;
                                    break;
                                }
                            }
                        }
                        if (isCV) {
                            audioEngine.addModRouting(realSrc->nodeID, sPort, realDst->nodeID, dPort);
                        } else {
                            graph.addConnection({{realSrc->nodeID, sPort}, {realDst->nodeID, dPort}});
                        }
                    }
                }
            }
        }
    }

    isDraggingConnection = false;
    dragSourceModule = nullptr;
    content.repaint();
}

void GraphEditor::detachAllModuleComponents() {
    for (auto* comp : content.getModules())
        comp->detachFromProcessor();
    content.getModules().clear(); // Remove after detach so ~ModuleComponent doesn't double-detach freed params
    modMatrix.detachAllRows();
    modMatrix.clearRows();
}

void GraphEditor::updateComponents() {
    auto& graph = audioEngine.getGraph();
    auto& modules = content.getModules();

    // 1. Remove components for nodes that no longer exist
    for (int i = modules.size(); --i >= 0;) {
        auto* comp = modules.getUnchecked(i);
        bool stillExists = false;
        for (auto* node : graph.getNodes()) {
            if (node->getProcessor() == comp->getModule()) {
                stillExists = true;
                break;
            }
        }
        if (!stillExists) {
            content.removeChildComponent(comp);
            modules.remove(i);
        }
    }

    // 2. Add components for new nodes
    int moduleIndex = 0;
    for (auto* node : graph.getNodes()) {
        auto* processor = node->getProcessor();
        if (!processor)
            continue;

        if (dynamic_cast<AttenuverterModule*>(processor) != nullptr)
            continue;

        // Check if we already have a component for this module
        ModuleComponent* existingComp = nullptr;
        for (auto* comp : modules) {
            if (comp->getModule() == processor) {
                existingComp = comp;
                break;
            }
        }

        if (existingComp == nullptr) {
            auto* newComp = modules.add(new ModuleComponent(processor, node->nodeID, *this, undoManager));
            content.addAndMakeVisible(newComp);
            existingComp = newComp;
        }

        // Always sync position from properties OR deterministic fallback
        auto x = node->properties.getWithDefault("x", -1);
        auto y = node->properties.getWithDefault("y", -1);

        if (static_cast<int>(x) != -1 && static_cast<int>(y) != -1) {
            existingComp->setTopLeftPosition(static_cast<int>(x), static_cast<int>(y));
        } else {
            // Determine fallback based on current count to be stable
            existingComp->setTopLeftPosition(100 + (moduleIndex * 150), 600);
        }

        moduleIndex++;
    }

    // Refresh mod matrix to pick up any new/removed attenuverter routings
    // Use callAsync to avoid re-entrancy during graph modification
    // SafePointer guards against the GraphEditor being destroyed before the callback fires
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* self = safeThis.getComponent())
            self->modMatrix.updateRowsFromGraph();
    });

    repaint();
}

void GraphEditor::paint(juce::Graphics& g) {
    // GraphEditor itself can draw a background or overlay if needed
    // But content handles it now.
}

void GraphEditor::resized() {
    if (isMatrixVisible) {
        modMatrix.setBounds(getWidth() - 600, 0, 600, getHeight());
    }
    updateTransform();
}

void GraphEditor::toggleModMatrixVisibility() {
    isMatrixVisible = !isMatrixVisible;
    modMatrix.setVisible(isMatrixVisible);
    resized();
}

void GraphEditor::updateTransform() {
    juce::AffineTransform t;
    t = t.scaled(zoomLevel, zoomLevel);
    t = t.translated(panOffset);

    content.setBounds(0, 0, 10000, 10000);
    content.setTransform(t);
    repaint();
}

void GraphEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    float oldZoom = zoomLevel;
    zoomLevel += wheel.deltaY * 0.1f * zoomLevel;
    zoomLevel = juce::jlimit(0.1f, 2.0f, zoomLevel);

    if (oldZoom != zoomLevel) {
        auto mousePos = e.position;
        // Transform the mouse position to get the graph point before scaling
        auto invT =
            juce::AffineTransform::translation(-panOffset.x, -panOffset.y).scaled(1.0f / oldZoom, 1.0f / oldZoom);
        float gx = mousePos.x;
        float gy = mousePos.y;
        invT.transformPoint(gx, gy);

        // Transform the mouse position to get the graph point after scaling
        // We want to keep the graph point under the mouse constant
        // mousePos = (graphPointBefore * zoomLevel) + newPanOffset
        // newPanOffset = mousePos - (graphPointBefore * zoomLevel)
        panOffset.x = mousePos.x - (gx * zoomLevel);
        panOffset.y = mousePos.y - (gy * zoomLevel);
    }

    updateTransform();
}

void GraphEditor::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown()) {
        lastMousePos = e.getPosition();

        auto localPos = content.getLocalPoint(this, e.getPosition());
        auto attenId = getAttenuverterNodeAt(localPos.toFloat());
        if (attenId.uid != 0) {
            draggingAttenuverterNodeId = attenId;
            if (undoManager)
                undoManager->captureBeforeState(audioEngine.getGraph());
        } else {
            draggingAttenuverterNodeId = juce::AudioProcessorGraph::NodeID();
        }
    }
}

void GraphEditor::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown() && !isDraggingConnection) {
        if (draggingAttenuverterNodeId.uid != 0) {
            auto& graph = audioEngine.getGraph();
            auto* node = graph.getNodeForId(draggingAttenuverterNodeId);
            if (node) {
                if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(node->getProcessor()->getParameters()[1])) {
                    float delta = (e.getPosition().y - lastMousePos.y) * -0.01f;
                    float currentVal = p->get(); // -1 to 1
                    currentVal = juce::jlimit(-1.0f, 1.0f, currentVal + delta);
                    p->setValueNotifyingHost(p->convertTo0to1(currentVal));
                    content.repaint();
                }
            }
            lastMousePos = e.getPosition();
            return;
        }

        auto delta = e.getPosition() - lastMousePos;
        panOffset += delta.toFloat();
        lastMousePos = e.getPosition();
        updateTransform();
    }
}

void GraphEditor::mouseUp(const juce::MouseEvent& e) {
    if (draggingAttenuverterNodeId.uid != 0 && undoManager) {
        undoManager->pushSnapshotFromCapture(audioEngine.getGraph());
    }
    draggingAttenuverterNodeId = juce::AudioProcessorGraph::NodeID();
}

void GraphEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    auto localPos = content.getLocalPoint(this, e.getPosition());
    auto attenId = getAttenuverterNodeAt(localPos.toFloat());
    if (attenId.uid != 0) {
        if (undoManager) {
            undoManager->recordStructuralChange(audioEngine.getGraph(),
                                                [this, attenId] { audioEngine.removeModRouting(attenId); });
        } else {
            audioEngine.removeModRouting(attenId);
        }
        content.repaint();
    }
}

juce::AudioProcessorGraph::NodeID GraphEditor::getAttenuverterNodeAt(juce::Point<float> localPos) {
    auto& graph = audioEngine.getGraph();
    for (auto& connection : graph.getConnections()) {
        auto* node1 = graph.getNodeForId(connection.source.nodeID);
        auto* node2 = graph.getNodeForId(connection.destination.nodeID);

        if (!node1 || !node2)
            continue;

        // An attenuverter always has a source connection into its port 0
        if (dynamic_cast<AttenuverterModule*>(node2->getProcessor()) != nullptr) {
            juce::AudioProcessorGraph::Node* realDstNode = nullptr;
            int realDstPort = 0;
            // Find the output connection FROM this attenuverter
            for (auto& c : graph.getConnections()) {
                if (c.source.nodeID == node2->nodeID) {
                    realDstNode = graph.getNodeForId(c.destination.nodeID);
                    realDstPort = c.destination.channelIndex;
                    break;
                }
            }

            if (realDstNode) {
                juce::Point<int> p1, p2;
                bool found1 = false, found2 = false;
                for (auto* comp : content.getModules()) {
                    if (comp->getModule() == node1->getProcessor()) {
                        auto localP = comp->getPortCenter(connection.source.channelIndex, false);
                        p1 = comp->getBounds().getPosition() + localP;
                        found1 = true;
                    }
                    if (comp->getModule() == realDstNode->getProcessor()) {
                        auto localP = comp->getPortCenter(realDstPort, true);
                        p2 = comp->getBounds().getPosition() + localP;
                        found2 = true;
                    }
                }

                if (found1 && found2) {
                    auto mid = (p1 + p2) / 2;
                    if (juce::Point<float>(mid.toFloat().x, mid.toFloat().y).getDistanceFrom(localPos) <= 15.0f) {
                        return node2->nodeID;
                    }
                }
            }
        }
    }
    return {};
}

void GraphEditor::updateModulePosition(ModuleComponent* module) {
    if (!module)
        return;
    for (auto* n : audioEngine.getGraph().getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            n->properties.set("x", module->getX());
            n->properties.set("y", module->getY());
            break;
        }
    }
}

void GraphEditor::deleteModule(ModuleComponent* module) {
    auto& graph = audioEngine.getGraph();
    juce::AudioProcessorGraph::NodeID nodeId;

    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            nodeId = n->nodeID;
            break;
        }
    }

    if (nodeId.uid == 0)
        return;

    if (undoManager) {
        undoManager->recordStructuralChange(graph, [this, nodeId, &graph] {
            modMatrix.clearRows();
            graph.removeNode(nodeId);
            updateComponents();
        });
    } else {
        modMatrix.clearRows();
        graph.removeNode(nodeId);
        updateComponents();
    }
    repaint();
}

void GraphEditor::replaceModule(ModuleComponent* moduleComp, const juce::String& newModuleType) {
    auto& graph = audioEngine.getGraph();

    // Find the old node by processor pointer (same pattern as deleteModule)
    juce::AudioProcessorGraph::NodeID oldNodeId;
    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == moduleComp->getModule()) {
            oldNodeId = n->nodeID;
            break;
        }
    }
    if (oldNodeId.uid == 0)
        return;

    // Use shared_ptr to make the lambda copyable (std::function requires it)
    auto newModuleTypeCopy = newModuleType;

    auto doReplace = [this, &graph, oldNodeId, newModuleTypeCopy] {
        auto* oldNode = graph.getNodeForId(oldNodeId);
        if (!oldNode)
            return;

        // 1. Create the new module
        auto newProcessor = gsynth::AIStateMapper::createModule(newModuleTypeCopy);
        if (!newProcessor)
            return;

        // 2. Snapshot old module's properties
        int posX = oldNode->properties.getWithDefault("x", 0);
        int posY = oldNode->properties.getWithDefault("y", 0);
        auto* oldProc = oldNode->getProcessor();
        int oldNumInputs = oldProc->getTotalNumInputChannels();
        int oldNumOutputs = oldProc->getTotalNumOutputChannels();
        bool oldAcceptsMidi = oldProc->acceptsMidi();
        bool oldProducesMidi = oldProc->producesMidi();

        // 3. Get new module capabilities before addNode moves it
        int newNumInputs = newProcessor->getTotalNumInputChannels();
        int newNumOutputs = newProcessor->getTotalNumOutputChannels();
        bool newAcceptsMidi = newProcessor->acceptsMidi();
        bool newProducesMidi = newProcessor->producesMidi();

        // 4. Collect all connections involving the old node
        struct ConnectionInfo {
            juce::AudioProcessorGraph::NodeID otherNodeId;
            int otherChannelIndex;
            int oldChannelIndex;
            bool isIncoming; // true = other->old, false = old->other
            bool isMidi;
        };
        std::vector<ConnectionInfo> directConnections;

        struct ModRoutingRewire {
            juce::AudioProcessorGraph::NodeID attenuverterId;
            int channelOnOldModule;
            bool oldModuleIsSource;
        };
        std::vector<ModRoutingRewire> modRoutings;

        for (auto& conn : graph.getConnections()) {
            bool srcIsOld = (conn.source.nodeID == oldNodeId);
            bool dstIsOld = (conn.destination.nodeID == oldNodeId);
            if (!srcIsOld && !dstIsOld)
                continue;

            // Check if this involves an attenuverter
            if (srcIsOld) {
                auto* dstNode = graph.getNodeForId(conn.destination.nodeID);
                if (dstNode && dynamic_cast<AttenuverterModule*>(dstNode->getProcessor())) {
                    ModRoutingRewire rw;
                    rw.attenuverterId = conn.destination.nodeID;
                    rw.channelOnOldModule = conn.source.channelIndex;
                    rw.oldModuleIsSource = true;
                    modRoutings.push_back(rw);
                    continue;
                }
            }
            if (dstIsOld) {
                auto* srcNode = graph.getNodeForId(conn.source.nodeID);
                if (srcNode && dynamic_cast<AttenuverterModule*>(srcNode->getProcessor())) {
                    ModRoutingRewire rw;
                    rw.attenuverterId = conn.source.nodeID;
                    rw.channelOnOldModule = conn.destination.channelIndex;
                    rw.oldModuleIsSource = false;
                    modRoutings.push_back(rw);
                    continue;
                }
            }

            // Direct connection (not attenuverter-mediated)
            ConnectionInfo ci;
            bool isMidiConn =
                (srcIsOld && conn.source.channelIndex == juce::AudioProcessorGraph::midiChannelIndex) ||
                (dstIsOld && conn.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex);
            ci.isMidi = isMidiConn;
            if (srcIsOld) {
                ci.otherNodeId = conn.destination.nodeID;
                ci.otherChannelIndex = conn.destination.channelIndex;
                ci.oldChannelIndex = conn.source.channelIndex;
                ci.isIncoming = false;
            } else {
                ci.otherNodeId = conn.source.nodeID;
                ci.otherChannelIndex = conn.source.channelIndex;
                ci.oldChannelIndex = conn.destination.channelIndex;
                ci.isIncoming = true;
            }
            directConnections.push_back(ci);
        }

        // 5. Add the new node to the graph
        auto newNode = graph.addNode(std::move(newProcessor));
        if (!newNode)
            return;
        auto newNodeId = newNode->nodeID;
        newNode->properties.set("x", posX);
        newNode->properties.set("y", posY);

        // 6. Remove the old node (this removes all its connections)
        modMatrix.clearRows();
        graph.removeNode(oldNodeId);

        // 7. Re-create compatible direct connections
        for (auto& ci : directConnections) {
            if (ci.isMidi) {
                if (ci.isIncoming && newAcceptsMidi) {
                    graph.addConnection({{ci.otherNodeId, juce::AudioProcessorGraph::midiChannelIndex},
                                         {newNodeId, juce::AudioProcessorGraph::midiChannelIndex}});
                } else if (!ci.isIncoming && newProducesMidi) {
                    graph.addConnection({{newNodeId, juce::AudioProcessorGraph::midiChannelIndex},
                                         {ci.otherNodeId, juce::AudioProcessorGraph::midiChannelIndex}});
                }
            } else {
                if (ci.isIncoming && ci.oldChannelIndex < newNumInputs) {
                    graph.addConnection({{ci.otherNodeId, ci.otherChannelIndex}, {newNodeId, ci.oldChannelIndex}});
                } else if (!ci.isIncoming && ci.oldChannelIndex < newNumOutputs) {
                    graph.addConnection({{newNodeId, ci.oldChannelIndex}, {ci.otherNodeId, ci.otherChannelIndex}});
                }
            }
        }

        // 8. Re-create compatible modulation routings
        // removeNode(oldNodeId) only removes connections TO/FROM oldNodeId.
        // For mod routings: source->attenuverter->dest
        // If old was source: old->atten connection is removed, atten->dest survives
        // If old was dest: atten->old connection is removed, source->atten survives
        // We only need to re-add the destroyed leg.
        for (auto& rw : modRoutings) {
            if (rw.oldModuleIsSource) {
                if (rw.channelOnOldModule < newNumOutputs) {
                    graph.addConnection({{newNodeId, rw.channelOnOldModule}, {rw.attenuverterId, 0}});
                }
            } else {
                if (rw.channelOnOldModule < newNumInputs) {
                    graph.addConnection({{rw.attenuverterId, 0}, {newNodeId, rw.channelOnOldModule}});
                }
            }
        }

        // 9. Refresh UI
        updateComponents();
        audioEngine.updateModuleNames();
    };

    if (undoManager) {
        undoManager->recordStructuralChange(graph, doReplace);
    } else {
        doReplace();
    }
    repaint();
}

void GraphEditor::disconnectPort(ModuleComponent* module, int portIndex, bool isInput, bool isMidi) {
    auto& graph = audioEngine.getGraph();
    juce::AudioProcessorGraph::NodeID nodeId;

    for (auto* n : graph.getNodes()) {
        if (n->getProcessor() == module->getModule()) {
            nodeId = n->nodeID;
            break;
        }
    }

    if (nodeId.uid == 0)
        return;

    auto doDisconnect = [this, &graph, nodeId, portIndex, isInput, isMidi] {
        std::vector<juce::AudioProcessorGraph::Connection> toRemove;
        int targetChannel = isMidi ? juce::AudioProcessorGraph::midiChannelIndex : portIndex;

        for (auto& c : graph.getConnections()) {
            if (isInput) {
                if (c.destination.nodeID == nodeId && c.destination.channelIndex == targetChannel) {
                    if (auto* srcNode = graph.getNodeForId(c.source.nodeID)) {
                        if (dynamic_cast<AttenuverterModule*>(srcNode->getProcessor()) != nullptr)
                            audioEngine.removeModRouting(srcNode->nodeID);
                        else
                            toRemove.push_back(c);
                    }
                }
            } else {
                if (c.source.nodeID == nodeId && c.source.channelIndex == targetChannel) {
                    if (auto* dstNode = graph.getNodeForId(c.destination.nodeID)) {
                        if (dynamic_cast<AttenuverterModule*>(dstNode->getProcessor()) != nullptr)
                            audioEngine.removeModRouting(dstNode->nodeID);
                        else
                            toRemove.push_back(c);
                    }
                }
            }
        }
        for (auto& c : toRemove)
            graph.removeConnection(c);
    };

    if (undoManager) {
        undoManager->recordStructuralChange(graph, doDisconnect);
    } else {
        doDisconnect();
    }
    repaint();
}

void GraphEditor::timerCallback() {
    cachedModDisplayInfo = audioEngine.getModulationDisplayInfo();
    content.connectionAnimPhase += 0.02f;
    if (content.connectionAnimPhase >= 1.0f)
        content.connectionAnimPhase -= 1.0f;
    content.repaint();
}

bool GraphEditor::isInterestedInDragSource(const SourceDetails& dragSourceDetails) { return true; }

void GraphEditor::itemDropped(const SourceDetails& dragSourceDetails) {
    juce::String name = dragSourceDetails.description.toString();
    std::unique_ptr<juce::AudioProcessor> newProcessor;

    if (name == "Oscillator")
        newProcessor = std::make_unique<OscillatorModule>();
    else if (name == "Filter")
        newProcessor = std::make_unique<FilterModule>();
    else if (name == "ADSR")
        newProcessor = std::make_unique<ADSRModule>();
    else if (name == "VCA")
        newProcessor = std::make_unique<VCAModule>();
    else if (name == "Sequencer")
        newProcessor = std::make_unique<SequencerModule>();
    else if (name == "LFO")
        newProcessor = std::make_unique<LFOModule>();
    else if (name == "Distortion")
        newProcessor = std::make_unique<DistortionModule>();
    else if (name == "Delay")
        newProcessor = std::make_unique<DelayModule>();
    else if (name == "Reverb")
        newProcessor = std::make_unique<ReverbModule>();
    else if (name == "MidiKeyboard")
        newProcessor = std::make_unique<MidiKeyboardModule>();
    else if (name == "Attenuverter")
        newProcessor = std::make_unique<AttenuverterModule>();
    else if (name == "Chorus")
        newProcessor = std::make_unique<ChorusModule>();
    else if (name == "Phaser")
        newProcessor = std::make_unique<PhaserModule>();
    else if (name == "Compressor")
        newProcessor = std::make_unique<CompressorModule>();
    else if (name == "Flanger")
        newProcessor = std::make_unique<FlangerModule>();
    else if (name == "Limiter")
        newProcessor = std::make_unique<LimiterModule>();
    else if (name == "Poly MIDI")
        newProcessor = std::make_unique<PolyMidiModule>();
    else if (name == "External MIDI")
        newProcessor = std::make_unique<ExternalMidiModule>();
    else if (name == "Voice Mixer")
        newProcessor = std::make_unique<VoiceMixerModule>();

    if (newProcessor) {
        auto& graph = audioEngine.getGraph();
        auto dropPos = content.getLocalPoint(this, dragSourceDetails.localPosition);

        if (undoManager) {
            // Use shared_ptr to make the lambda copyable (std::function requires it)
            auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(newProcessor));
            undoManager->recordStructuralChange(graph, [this, proc, dropPos] {
                if (*proc) {
                    auto node = audioEngine.getGraph().addNode(std::move(*proc));
                    if (node) {
                        node->properties.set("x", dropPos.x);
                        node->properties.set("y", dropPos.y);
                    }
                }
                updateComponents();
            });
        } else {
            auto node = graph.addNode(std::move(newProcessor));
            if (node) {
                node->properties.set("x", dropPos.x);
                node->properties.set("y", dropPos.y);
                updateComponents();
            }
        }
    }
}

void GraphEditor::savePreset(juce::File file) {
    auto json = gsynth::AIStateMapper::graphToJSON(audioEngine.getGraph());
    file.replaceWithText(juce::JSON::toString(json));
}

void GraphEditor::loadPreset(juce::File file) {
    auto json = juce::JSON::parse(file);
    if (!json.isObject()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed",
                                               "Could not parse preset file.");
        return;
    }
    if (gsynth::AIStateMapper::applyJSONToGraph(json, audioEngine.getGraph(), true)) {
        updateComponents();
    } else {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed",
                                               "Could not apply preset to graph.");
    }
}
