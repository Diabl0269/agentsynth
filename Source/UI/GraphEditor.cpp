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
#include "../Modules/NoiseModule.h"
#include "../Modules/OscillatorModule.h"
#include "../Modules/PolyMidiModule.h"
#include "../Modules/PolySequencerModule.h"
#include "../Modules/SequencerModule.h"
#include "../Modules/VCAModule.h"
#include "../Modules/VoiceMixerModule.h"
#include "../PresetManager.h"
#include "LayoutUtil.h"
#include "ModuleComponent.h"
#include "Theme/AppLookAndFeel.h"
#include <set>
#include <tuple>
#include <unordered_map>

// Returns an estimated (w, h) footprint for a module type name.
// Used when the component does not yet exist (e.g. on drag-drop before layout).
// Heights match the real component sizes so the library-drag ghost preview is accurate.
// The final drop placement uses the real component size via finalizeModuleDrag() — the
// estimate is only used for the live ghost preview.
static juce::Point<int> estimateModuleSize(const juce::String& typeName) {
    if (typeName == "Oscillator")
        return {280, 530};
    if (typeName == "Filter")
        return {280, 570};
    if (typeName == "LFO")
        return {280, 440};
    if (typeName == "VCA")
        return {280, 200};
    if (typeName == "ADSR" || typeName == "Amp Env" || typeName == "Filter Env")
        return {280, 180};
    if (typeName.containsIgnoreCase("Sequencer") && !typeName.containsIgnoreCase("Poly"))
        return {synth::LayoutUtil::kDoubleWidth, 380};
    if (typeName.containsIgnoreCase("Poly") && typeName.containsIgnoreCase("Sequencer"))
        return {synth::LayoutUtil::kDoubleWidth, 380};
    if (typeName.containsIgnoreCase("MidiKeyboard") || typeName.containsIgnoreCase("Midi Keyboard") ||
        typeName.containsIgnoreCase("MIDI Keyboard"))
        return {synth::LayoutUtil::kDoubleWidth, 150};
    if (typeName == "Poly MIDI" || typeName == "PolyMidi")
        return {280, 100};
    if (typeName == "Distortion")
        return {280, 350};
    if (typeName == "Delay")
        return {280, 220};
    if (typeName == "Reverb")
        return {280, 300};
    if (typeName == "AudioInput" || typeName == "AudioOutput")
        return {280, 100};
    if (typeName == "Attenuverter")
        return {280, 120};
    if (typeName == "Noise")
        return {280, 250};
    if (typeName == "Envelope Follower")
        return {280, 250};
    return {280, 360};
}

GraphEditor::GraphEditor(AudioEngine& engine, AppUndoManager* undoMgr)
    : audioEngine(engine)
    , content(*this)
    , modMatrix(engine, undoMgr)
    , undoManager(undoMgr) {
    addAndMakeVisible(content);
    addAndMakeVisible(modMatrix);
    content.setInterceptsMouseClicks(false, true); // Fallback clicks to parent

    // Tooltips on GraphEditor-owned affordances.
    // The canvas itself hints at pan/zoom. Double-click on an attenuverter knob removes it.
    setTooltip(synth::ui::formatShortcutHint("Patch canvas - drag modules here to build your patch",
                                             "Scroll to zoom | Drag to pan | Double-click mod knob to remove"));

    startTimerHz(30);
}

GraphEditor::~GraphEditor() { stopTimer(); }

GraphEditor::GraphContentComponent::GraphContentComponent(GraphEditor& ed)
    : editor(ed) {
    // The canvas fills its whole bounds opaquely (bg1 + grid), so tell JUCE not to repaint
    // whatever is behind it on every frame — a real win for zoom/pan and the 30Hz wire animation.
    setOpaque(true);
}

void GraphEditor::GraphContentComponent::paint(juce::Graphics& g) {
    // Resolve the themed LookAndFeel once. In headless tests the default JUCE LnF is
    // installed, so the cast returns null and we fall back to plain fills/lines.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    if (lf != nullptr)
        lf->fillThemedBackground(g, getLocalBounds().toFloat(), /*isCanvas*/ true);
    else
        g.fillAll(juce::Colours::darkgrey);

    // ---- Drag-preview grid dots (only while a module is being dragged) ----
    // Draw subtle dots at kGridSize*5 = 40px spacing over the VISIBLE canvas region only.
    // This stays cheap: we compute the visible clip in canvas coords and skip everything outside.
    if (editor.dragPreviewActive) {
        // The content component's transform maps canvas -> screen. The clip rect of g is
        // already in canvas coords (paint runs in local/canvas space), so getClipBounds()
        // gives us the visible region for free.
        auto clip = g.getClipBounds();

        // Dot colour: textPrimary at ~8% alpha for a gentle, non-distracting grid.
        const juce::Colour textPrimaryColourForGrid =
            lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
        g.setColour(textPrimaryColourForGrid.withAlpha(0.08f));

        constexpr int kMajorGrid = synth::LayoutUtil::kGridSize * 5; // 40px
        int startX = (clip.getX() / kMajorGrid) * kMajorGrid;
        int startY = (clip.getY() / kMajorGrid) * kMajorGrid;

        for (int gx = startX; gx <= clip.getRight(); gx += kMajorGrid) {
            for (int gy = startY; gy <= clip.getBottom(); gy += kMajorGrid) {
                g.fillEllipse((float)gx - 1.2f, (float)gy - 1.2f, 2.4f, 2.4f);
            }
        }
    }
    // ---- End drag-preview grid dots ----

    // Theme color tokens (fall back to legacy literals when unthemed).
    const juce::Colour audioWireColour = lf != nullptr ? lf->getTheme().colors.audioWire : juce::Colours::white;
    const juce::Colour modWireColour = lf != nullptr ? lf->getTheme().colors.modWire : juce::Colours::yellow;
    const juce::Colour polyBusWireColour = lf != nullptr ? lf->getTheme().colors.polyBusWire : juce::Colour(0xff00e5ff);
    const juce::Colour pitchWireColour = lf != nullptr ? lf->getTheme().colors.pitchWire : juce::Colour(0xffaad4ff);
    const juce::Colour gateWireColour = lf != nullptr ? lf->getTheme().colors.gateWire : juce::Colour(0xffffa500);
    const juce::Colour surfaceColour = lf != nullptr ? lf->getTheme().colors.surface : juce::Colours::darkgrey;
    const juce::Colour knobPointerColour = lf != nullptr ? lf->getTheme().colors.knobPointer : juce::Colours::white;
    const juce::Colour textPrimaryColour = lf != nullptr ? lf->getTheme().colors.textPrimary : juce::Colours::white;
    const juce::Colour textMutedColour = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::white;

    // Build the cubic-bezier wire path (identical to AppLookAndFeel::drawConnectionWire's
    // default curve) so the animated dots can ride the EXACT same curve as the drawn wire instead
    // of cutting straight across it.
    auto buildWirePath = [](juce::Point<float> p1, juce::Point<float> p2) {
        juce::Path wp;
        const float dx = p2.x - p1.x;
        wp.startNewSubPath(p1);
        wp.cubicTo(p1.x + dx * 0.5f, p1.y, p2.x - dx * 0.5f, p2.y, p2.x, p2.y);
        return wp;
    };

    // Stroke a wire via the LnF helper (themed) or a curved fallback, and RETURN the path so the
    // caller can place the animated dots along it.
    auto strokeWire = [&](juce::Point<float> p1, juce::Point<float> p2, juce::Colour colour, bool isModulation,
                          float activity, float fallbackWidth) -> juce::Path {
        juce::Path wire = buildWirePath(p1, p2);
        if (lf != nullptr) {
            lf->drawConnectionWire(g, p1, p2, wire, colour, isModulation, activity, /*hovered*/ false);
        } else {
            float brightness = juce::jlimit(0.5f, 1.0f, 0.5f + activity * 0.5f);
            g.setColour(colour.withMultipliedBrightness(brightness));
            g.strokePath(
                wire, juce::PathStrokeType(fallbackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        return wire;
    };

    // Draw the 3 animated signal-flow dots evenly spaced along a wire path so they follow the curve.
    auto drawWireDots = [&](const juce::Path& wire, juce::Colour colour) {
        const float len = wire.getLength();
        for (int d = 0; d < 3; ++d) {
            float t = std::fmod(connectionAnimPhase + (float)d / 3.0f, 1.0f);
            auto pt = wire.getPointAlongPath(t * len);
            g.setColour(colour.withAlpha(0.7f));
            g.fillEllipse(pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
        }
    };

    // Draw connections
    auto& graph = editor.audioEngine.getGraph();
    g.setColour(modWireColour);

    // Build a set of raw edges that Pass-2 (PolyBus / DirectCV wires) will draw,
    // so Pass-1 does not render them a second time in a conflicting generic style.
    // AttenuverterChain edges are intentionally excluded — they are handled by the
    // existing attenuverter branch inside Pass-1.
    using EdgeKey = std::tuple<uint32_t, int, uint32_t, int>;
    std::set<EdgeKey> pass2HandledEdges;
    for (const auto& routing : editor.cachedModRoutings) {
        if (routing.kind == AudioEngine::RoutingKind::DirectCV) {
            pass2HandledEdges.emplace(routing.sourceNodeID.uid, routing.sourceChannelIndex, routing.destNodeID.uid,
                                      routing.destChannelIndex);
        } else if (routing.kind == AudioEngine::RoutingKind::PolyBus) {
            for (int i = 0; i < routing.voiceCount; ++i)
                pass2HandledEdges.emplace(routing.sourceNodeID.uid, routing.sourceChannelIndex + i,
                                          routing.destNodeID.uid, routing.destChannelIndex + i);
        }
    }

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

        // Skip raw edges that Pass-2 (PolyBus / DirectCV) will draw to avoid
        // double-rendering in conflicting styles.
        if (connection.source.channelIndex != juce::AudioProcessorGraph::midiChannelIndex &&
            connection.destination.channelIndex != juce::AudioProcessorGraph::midiChannelIndex) {
            EdgeKey ek{connection.source.nodeID.uid, connection.source.channelIndex, connection.destination.nodeID.uid,
                       connection.destination.channelIndex};
            if (pass2HandledEdges.count(ek))
                continue;
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
                    // Attenuverter chain renders as a modulation wire.
                    auto wirePath = strokeWire(p1.toFloat(), p2.toFloat(), modWireColour, /*isModulation*/ true,
                                               modPeak, /*fallbackWidth*/ 2.0f + modPeak * 2.0f);

                    float amt = 0.0f;
                    if (auto* p = node2->getProcessor()->getParameters()[1]) {
                        amt = p->getValue();
                        amt = amt * 2.0f - 1.0f; // 0 to 1 back to -1.0 to 1.0
                    }

                    auto mid = (p1 + p2) / 2;
                    juce::Rectangle<float> knobArea(mid.toFloat().x - 10, mid.toFloat().y - 10, 20, 20);
                    g.setColour(surfaceColour);
                    g.fillEllipse(knobArea);
                    g.setColour(knobPointerColour);
                    g.drawEllipse(knobArea, 1.0f);

                    float angle = juce::jmap(amt, -1.0f, 1.0f, -juce::MathConstants<float>::pi * 0.75f,
                                             juce::MathConstants<float>::pi * 0.75f);
                    float dx = std::sin(angle) * 8.0f;
                    float dy = -std::cos(angle) * 8.0f;
                    g.drawLine(mid.toFloat().x, mid.toFloat().y, mid.toFloat().x + dx, mid.toFloat().y + dy, 2.0f);

                    // Animated dots ride the bezier wire path.
                    drawWireDots(wirePath, modWireColour);
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
            // Audio signal wire (solid) + dots that follow the curve.
            auto wirePath = strokeWire(p1.toFloat(), p2.toFloat(), audioWireColour, /*isModulation*/ false,
                                       /*activity*/ 0.0f, /*fallbackWidth*/ 2.0f);
            drawWireDots(wirePath, audioWireColour);
        }
    }

    // ---- Draw PolyBus / DirectCV modulation wires ----
    // Build a nodeID -> ModuleComponent* lookup map once for O(1) per routing.
    std::unordered_map<uint32_t, ModuleComponent*> nodeCompMap;
    for (auto* comp : moduleComponents) {
        if (comp == nullptr)
            continue;
        // Match against graph nodes by processor pointer.
        for (auto* node : graph.getNodes()) {
            if (node->getProcessor() == comp->getModule()) {
                nodeCompMap[node->nodeID.uid] = comp;
                break;
            }
        }
    }

    for (const auto& routing : editor.cachedModRoutings) {
        if (routing.kind == AudioEngine::RoutingKind::AttenuverterChain)
            continue; // Already rendered by the existing loop above.

        auto srcIt = nodeCompMap.find(routing.sourceNodeID.uid);
        auto dstIt = nodeCompMap.find(routing.destNodeID.uid);
        if (srcIt == nodeCompMap.end() || dstIt == nodeCompMap.end())
            continue;

        auto* srcComp = srcIt->second;
        auto* dstComp = dstIt->second;
        if (srcComp == nullptr || dstComp == nullptr)
            continue;

        // getPortCenter now clamps out-of-range indices to the last visible jack,
        // so these points always land on a real rendered port.
        juce::Point<int> p1 =
            srcComp->getBounds().getPosition() + srcComp->getPortCenter(routing.sourceVisibleJack, /*isInput=*/false);
        juce::Point<int> p2 =
            dstComp->getBounds().getPosition() + dstComp->getPortCenter(routing.destVisibleJack, /*isInput=*/true);

        // Brightness / width scaled by signal activity (mirrors attenuverter line ~lines 107-110).
        float modPeak = routing.modSignalPeak;
        float lineWidth = 2.5f + modPeak * 2.0f;

        // Color by role from theme tokens: ModCV = poly-bus or direct (modWire), Pitch, Gate.
        juce::Colour wireColour;
        if (routing.role == PortRole::Pitch) {
            wireColour = pitchWireColour; // pitch fan
        } else if (routing.role == PortRole::Gate) {
            wireColour = gateWireColour; // gate fan
        } else {
            wireColour = (routing.kind == AudioEngine::RoutingKind::PolyBus) ? polyBusWireColour // ModCV poly bus
                                                                             : modWireColour;    // ModCV direct CV
        }

        if (routing.isBypassed)
            wireColour = wireColour.withAlpha(0.3f);

        // Mod-bus wires render as modulation wires (DirectCV/PolyBus/Pitch/Gate are all CV).
        auto wirePath = strokeWire(p1.toFloat(), p2.toFloat(), wireColour, /*isModulation*/ true, modPeak,
                                   /*fallbackWidth*/ lineWidth);
        drawWireDots(wirePath, wireColour);

        // For poly buses, label the midpoint with "x{voiceCount}" so the bundle is visible.
        // Drawn as textPrimary on a surface pill.
        if (routing.kind == AudioEngine::RoutingKind::PolyBus && routing.voiceCount > 1) {
            auto mid = ((p1 + p2) / 2).toFloat();
            juce::String badge = "x" + juce::String(routing.voiceCount);
            g.setFont(11.0f);
            int textW = (int)g.getCurrentFont().getStringWidthFloat(badge) + 8;
            juce::Rectangle<float> pill((float)((int)mid.x + 5), mid.y - 8.0f, (float)textW, 16.0f);
            g.setColour(surfaceColour);
            const float smallRadius = lf != nullptr ? lf->getTheme().metrics.cornerRadiusSmall : 4.0f;
            g.fillRoundedRectangle(pill, smallRadius);
            g.setColour(textPrimaryColour);
            g.drawText(badge, pill, juce::Justification::centred, false);
        }
    }
    // ---- End PolyBus / DirectCV wires ----

    // Draw Line being dragged
    if (editor.isDraggingConnection) {
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

            // In-progress drag wire uses the audio-wire token via the LnF helper.
            strokeWire(posInContent.toFloat(), mouseLocal.toFloat(), audioWireColour, /*isModulation*/ false,
                       /*activity*/ 0.0f, /*fallbackWidth*/ 3.0f);
        }
    }
}

void GraphEditor::GraphContentComponent::resized() {}

void GraphEditor::GraphContentComponent::paintOverChildren(juce::Graphics& g) {
    // ---- Drag-preview landing ghost (on top of module cards) ----
    // Draw a translucent rounded rect at the exact snapped+anti-overlapped landing position.
    if (editor.dragPreviewActive && !editor.dragPreviewGhost.isEmpty()) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour accentColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
        const auto& m = lf != nullptr ? lf->getTheme().metrics : synth::theme::Metrics{};
        const float cornerRadius = m.cornerRadius;

        auto ghostF = editor.dragPreviewGhost.toFloat();

        // Fill: accent colour at ~18% alpha
        g.setColour(accentColour.withAlpha(0.18f));
        g.fillRoundedRectangle(ghostF, cornerRadius);

        // Outline: accent colour at ~70% alpha, 1.5px
        g.setColour(accentColour.withAlpha(0.70f));
        const float guideLineWidth = lf != nullptr ? lf->getTheme().metrics.guideLineWidth : 1.5f;
        g.drawRoundedRectangle(ghostF, cornerRadius, guideLineWidth);
    }

    // ---- Alignment guides (UI Phase 7 - Item 4) ----
    // Draw aligned edges when hovering near other modules (Figma-style)
    if (editor.dragPreviewActive && !editor.alignmentGuides.empty() && editor.alignmentGuidesEnabled) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour guideColour = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::white;

        // Solid lines, ~70% opacity for visibility without distraction
        const float guideAlpha = lf != nullptr ? lf->getTheme().metrics.guideAlpha : 0.7f;
        g.setColour(guideColour.withAlpha(guideAlpha));
        for (const auto& guide : editor.alignmentGuides) {
            const float dx = guide.end.x - guide.start.x;
            const float dy = guide.end.y - guide.start.y;

            if (std::abs(dx) > std::abs(dy)) { // Horizontal line
                g.drawHorizontalLine((int)guide.start.y, guide.start.x, guide.end.x);
            } else { // Vertical line
                g.drawVerticalLine((int)guide.start.x, guide.start.y, guide.end.y);
            }
        }
    }
}

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
            // Fallback: no stored position — resolve a non-overlapping slot.
            // Desired origin strides by 300px to reduce clustering; resolvePlacement
            // then snaps and spirals clear of any previously placed components.
            auto desired = juce::Point<int>(synth::LayoutUtil::kArrangeOriginX + moduleIndex * 300, 600);
            auto clear = resolvePlacement(desired, existingComp->getWidth(), existingComp->getHeight(), node->nodeID);
            existingComp->setTopLeftPosition(clear);
            // Persist the resolved position so subsequent loads don't need to re-resolve
            node->properties.set("x", clear.x);
            node->properties.set("y", clear.y);
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

void GraphEditor::paintOverChildren(juce::Graphics& g) {
    // ---- Empty-canvas first-run hint ----
    // Drawn here (OUTER, untransformed GraphEditor local coordinates) so it is ALWAYS
    // centred in the visible viewport regardless of pan/zoom on the inner canvas.
    // The inner GraphContentComponent runs in a transformed (pan+zoom) space over a
    // ~10000x10000 virtual canvas — any rect drawn there would land off-screen once the
    // user pans or zooms. Drawing here, in getLocalBounds(), guarantees centre alignment.
    //
    // Gate: only when canvas is empty. Show/hide is driven by the existing updateComponents()
    // repaint path — no extra timer or per-tick repaint is added.
    if (!GraphEditor::isCanvasEmpty(static_cast<int>(content.getModules().size())))
        return;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    // textMuted token at ~60% alpha — tasteful, non-distracting.
    const juce::Colour textMutedColour = lf != nullptr ? lf->getTheme().colors.textMuted : juce::Colours::white;
    g.setColour(textMutedColour.withAlpha(0.6f));

    // Use the theme h1 font (~18pt) for comfortable legibility; fall back to 16pt headless.
    juce::Font hintFont;
    if (lf != nullptr)
        hintFont = juce::Font(juce::FontOptions(lf->getTheme().type.h1)).withStyle(juce::Font::plain);
    else
        hintFont = juce::Font(16.0f);
    g.setFont(hintFont);

    // Centre in the GraphEditor's visible local bounds (untransformed viewport coordinates).
    g.drawFittedText("Drag modules here to build your patch", getLocalBounds(), juce::Justification::centred, 1);
    // ---- End empty-canvas hint ----
}

void GraphEditor::resized() {
    // Only set the mod-matrix bounds when we are not in the middle of an animated show/hide.
    // If an animation is running, it owns the bounds until it completes; we update the target
    // but don't interrupt the tween.
    if (!modMatrixAnim.isRunning()) {
        if (isMatrixVisible) {
            modMatrix.setBounds(getWidth() - 600, 0, 600, getHeight());
        }
    } else {
        // Update the stored target so the onComplete callback uses the updated size.
        modMatrixTargetBounds = isMatrixVisible ? juce::Rectangle<int>(getWidth() - 600, 0, 600, getHeight())
                                                : juce::Rectangle<int>(getWidth(), 0, 600, getHeight());
    }
    updateTransform();
}

void GraphEditor::toggleModMatrixVisibility() {
    isMatrixVisible = !isMatrixVisible;

    // Always make it visible before the animation so it paints during the tween.
    // On hide we keep it visible until the animation completes, then hide it.
    modMatrix.setVisible(true);

    const int panelW = 600;
    const int editorW = getWidth();
    const int editorH = getHeight();

    // From-bounds: current position (either fully shown or fully hidden off-screen right).
    juce::Rectangle<int> fromBounds = modMatrix.getBounds();
    // If the component has never been laid out, give it a sensible off-screen start.
    if (fromBounds.isEmpty())
        fromBounds = {editorW, 0, panelW, editorH};

    // To-bounds: target position.
    juce::Rectangle<int> toBounds = isMatrixVisible ? juce::Rectangle<int>(editorW - panelW, 0, panelW, editorH)
                                                    : juce::Rectangle<int>(editorW, 0, panelW, editorH);

    modMatrixTargetBounds = toBounds;

    // Start a 220 ms easeOutCubic tween on modMatrix bounds.
    const bool hidingAfterAnim = !isMatrixVisible;
    juce::Component::SafePointer<GraphEditor> safeThis(this);
    modMatrixAnim.start(
        vblankUpdater, 220.0, synth::ui::easeOutCubic,
        [safeThis, fromBounds, toBounds](float t) {
            if (auto* self = safeThis.getComponent())
                self->modMatrix.setBounds(synth::ui::AnimationDriver::lerpBounds(fromBounds, toBounds, t));
        },
        [safeThis, hidingAfterAnim, toBounds]() {
            if (auto* self = safeThis.getComponent()) {
                self->modMatrix.setBounds(toBounds);
                if (hidingAfterAnim)
                    self->modMatrix.setVisible(false);
            }
        });
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

    // Resolve NodeID then delegate to the single removal path.
    requestDeleteModule(nodeId);
}

void GraphEditor::requestDeleteModule(juce::AudioProcessorGraph::NodeID nodeId) {
    if (nodeId.uid == 0)
        return;

    auto& graph = audioEngine.getGraph();

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
        auto newProcessor = synth::AIStateMapper::createModule(newModuleTypeCopy);
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
    // Compute the routing traversal once; derive display info from the same snapshot.
    cachedModRoutings = audioEngine.getModulationRoutings();
    cachedModDisplayInfo = audioEngine.getModulationDisplayInfo(cachedModRoutings);
    content.connectionAnimPhase += 0.02f;
    if (content.connectionAnimPhase >= 1.0f)
        content.connectionAnimPhase -= 1.0f;
    content.repaint();
}

// ============================================================================
// Drag-preview API
// ============================================================================

void GraphEditor::beginDragPreview(int w, int h, juce::AudioProcessorGraph::NodeID selfId) {
    dragPreviewActive = true;
    dragPreviewW = w;
    dragPreviewH = h;
    dragPreviewSelfId = selfId;
    dragPreviewGhost = {};
    alignmentGuides.clear();
    content.repaint();
}

void GraphEditor::updateDragPreview(juce::Point<int> desiredTopLeftCanvas) {
    if (!dragPreviewActive)
        return;
    auto resolved = resolvePlacement(desiredTopLeftCanvas, dragPreviewW, dragPreviewH, dragPreviewSelfId);
    dragPreviewGhost = juce::Rectangle<int>(resolved.x, resolved.y, dragPreviewW, dragPreviewH);

    // ---- Alignment guides (UI Phase 7 - Item 4) ----
    // Scan existing modules and compute alignment guides for closest edges
    alignmentGuides.clear();
    if (dragPreviewGhost.isEmpty())
        return;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto& m = lf != nullptr ? lf->getTheme().metrics : synth::theme::Metrics{};
    const float snapThreshold = static_cast<float>(m.gridSize); // kGridSize

    // Build list of existing module boxes (excluding self)
    std::vector<synth::LayoutUtil::Box> existingBoxes;
    for (auto* comp : content.getModules()) {
        if (comp->getNodeId() == dragPreviewSelfId)
            continue; // Skip self
        juce::Rectangle<int> rect = comp->getBounds();
        existingBoxes.push_back({comp->getNodeId(), rect});
    }

    if (existingBoxes.empty()) {
        content.repaint(); // Still need to clear any old guides
        return;
    }

    auto ghostRect = dragPreviewGhost.toFloat();
    float left = ghostRect.getX();
    float right = ghostRect.getRight();
    float top = ghostRect.getY();
    float bottom = ghostRect.getBottom();
    float centerX = ghostRect.getX() + ghostRect.getWidth() * 0.5f;
    float centerY = ghostRect.getY() + ghostRect.getHeight() * 0.5f;

    // Track best alignment candidates (edge-to-edge snap within threshold)
    struct Candidate {
        int type;   // 0=left,1=right,2=top,3=bottom,4=centerX,5=centerY
        float dist; // absolute distance to snap target
        juce::Point<float> start;
        juce::Point<float> end;
    };
    std::vector<Candidate> candidates;

    // Check each existing module for alignments
    for (const auto& box : existingBoxes) {
        float l = box.rect.getX();
        float r = box.rect.getRight();
        float t = box.rect.getY();
        float b = box.rect.getBottom();
        float cx = box.rect.getX() + box.rect.getWidth() * 0.5f;
        float cy = box.rect.getY() + box.rect.getHeight() * 0.5f;

        // Edge-to-edge alignments (left/right/top/bottom)
        struct Edge {
            float alignPos;  // position we're aligning (ghost edge)
            float targetPos; // target edge position
            int type;        // guide type enum
            bool horizontal; // true if horizontal line, false if vertical
        };

        Edge edges[] = {
            {left, l, 0, false},  // left-to-left
            {right, r, 1, false}, // right-to-right
            {top, t, 2, true},    // top-to-top
            {bottom, b, 3, true}  // bottom-to-bottom
        };

        for (const auto& edge : edges) {
            float dist = std::abs(edge.alignPos - edge.targetPos);
            if (dist <= snapThreshold) {
                Candidate c;
                c.type = edge.type;
                c.dist = dist;
                // Line spans the overlapping range
                if (edge.horizontal) {
                    float startX = std::min(left, l);
                    float endX = std::max(right, r);
                    c.start = {startX, edge.targetPos};
                    c.end = {endX, edge.targetPos};
                } else {
                    float startY = std::min(top, t);
                    float endY = std::max(bottom, b);
                    c.start = {edge.targetPos, startY};
                    c.end = {edge.targetPos, endY};
                }
                candidates.push_back(c);
            }
        }

        // Center alignment (X and Y)
        float cxDist = std::abs(centerX - cx);
        if (cxDist <= snapThreshold) {
            Candidate c;
            c.type = 4; // centerX
            c.dist = cxDist;
            c.start = {cx, std::min(top, t)};
            c.end = {cx, std::max(bottom, b)};
            candidates.push_back(c);
        }

        float cyDist = std::abs(centerY - cy);
        if (cyDist <= snapThreshold) {
            Candidate c;
            c.type = 5; // centerY
            c.dist = cyDist;
            c.start = {std::min(left, l), cy};
            c.end = {std::max(right, r), cy};
            candidates.push_back(c);
        }
    }

    // Deduplicate: keep only the closest guide for each type (left/right/top/bottom/centerX/centerY)
    std::vector<Candidate> bestCandidates;
    float minDist[] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    int bestIdx[] = {-1, -1, -1, -1, -1, -1};

    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        if (c.dist <= snapThreshold) {
            if (c.dist < minDist[c.type]) {
                minDist[c.type] = c.dist;
                bestIdx[c.type] = (int)i;
            }
        }
    }

    for (int i = 0; i < 6; ++i) {
        if (bestIdx[i] != -1)
            bestCandidates.push_back(candidates[bestIdx[i]]);
    }

    // Convert to AlignmentGuide and store
    alignmentGuides.clear();
    for (const auto& c : bestCandidates) {
        alignmentGuides.push_back({c.start, c.end, c.type});
    }

    content.repaint();
}

void GraphEditor::endDragPreview() {
    dragPreviewActive = false;
    dragPreviewGhost = {};
    content.repaint();
}

bool GraphEditor::isInterestedInDragSource(const SourceDetails& dragSourceDetails) { return true; }

void GraphEditor::itemDragEnter(const SourceDetails& dragSourceDetails) {
    juce::String name = dragSourceDetails.description.toString();
    auto estSize = estimateModuleSize(name);
    beginDragPreview(estSize.x, estSize.y, juce::AudioProcessorGraph::NodeID{});
    // Compute initial ghost position
    auto canvasPos = content.getLocalPoint(this, dragSourceDetails.localPosition).roundToInt();
    updateDragPreview(canvasPos);
}

void GraphEditor::itemDragMove(const SourceDetails& dragSourceDetails) {
    auto canvasPos = content.getLocalPoint(this, dragSourceDetails.localPosition).roundToInt();
    updateDragPreview(canvasPos);
}

void GraphEditor::itemDragExit(const SourceDetails& dragSourceDetails) {
    juce::ignoreUnused(dragSourceDetails);
    endDragPreview();
}

void GraphEditor::itemDropped(const SourceDetails& dragSourceDetails) {
    juce::String name = dragSourceDetails.description.toString();
    auto newProcessor = synth::AIStateMapper::createModule(name);

    if (newProcessor) {
        auto& graph = audioEngine.getGraph();
        auto dropPos = content.getLocalPoint(this, dragSourceDetails.localPosition).roundToInt();
        // Use estimate for an initial snapped position; finalizeModuleDrag will re-resolve
        // using the real component size after updateComponents() creates the component.
        auto estSize = estimateModuleSize(name);
        auto initialPlaced = resolvePlacement(dropPos, estSize.x, estSize.y, juce::AudioProcessorGraph::NodeID{});

        // finalizeNewDrop: locate the newly created ModuleComponent, compute its
        // real final position (snapped + anti-overlapped using actual dimensions),
        // then animate it from the raw drop point to the settled position.
        auto finalizeNewDrop = [this, initialPlaced](juce::AudioProcessorGraph::NodeID newNodeId) {
            ModuleComponent* newComp = nullptr;
            for (auto* comp : content.getModules()) {
                if (comp != nullptr && comp->getNodeId() == newNodeId) {
                    newComp = comp;
                    break;
                }
            }
            if (newComp == nullptr)
                return;

            // Compute final position using the real component size.
            auto toPos = resolvePlacement(newComp->getPosition(), newComp->getWidth(), newComp->getHeight(),
                                          newComp->getNodeId());

            // Animate from the estimated initial-placed position to the real final position.
            // If they are identical, animateDropLanding is a no-op (just settles in place).
            animateDropLanding(newComp, initialPlaced, toPos);

            // Persist the final position immediately so it survives reload even if the
            // animation is still in-flight when the user saves.
            updateModulePosition(newComp);
        };

        if (undoManager) {
            // Use shared_ptr to make the lambda copyable (std::function requires it)
            auto proc = std::make_shared<std::unique_ptr<juce::AudioProcessor>>(std::move(newProcessor));
            undoManager->recordStructuralChange(graph, [this, proc, initialPlaced, finalizeNewDrop] {
                if (*proc) {
                    auto node = audioEngine.getGraph().addNode(std::move(*proc));
                    if (node) {
                        node->properties.set("x", initialPlaced.x);
                        node->properties.set("y", initialPlaced.y);
                        auto newNodeId = node->nodeID;
                        updateComponents();
                        finalizeNewDrop(newNodeId);
                    }
                }
            });
        } else {
            auto node = graph.addNode(std::move(newProcessor));
            if (node) {
                node->properties.set("x", initialPlaced.x);
                node->properties.set("y", initialPlaced.y);
                auto newNodeId = node->nodeID;
                updateComponents();
                finalizeNewDrop(newNodeId);
            }
        }
    }

    endDragPreview();
}

juce::Point<int> GraphEditor::resolvePlacement(juce::Point<int> desired, int w, int h,
                                               juce::AudioProcessorGraph::NodeID selfId) {
    std::vector<synth::LayoutUtil::Box> boxes;
    for (auto* comp : content.getModules()) {
        if (comp == nullptr)
            continue;
        boxes.push_back({comp->getNodeId(), comp->getBounds()});
    }
    auto snapped = synth::LayoutUtil::snap(desired);
    return synth::LayoutUtil::findFreeSlot(snapped, w, h, boxes, selfId);
}

// static
juce::Point<int> GraphEditor::computeDropFinalPosition(juce::Point<int> dropPoint, int w, int h,
                                                       const std::vector<synth::LayoutUtil::Box>& existingBoxes,
                                                       synth::LayoutUtil::NodeID selfId) {
    auto snapped = synth::LayoutUtil::snap(dropPoint);
    return synth::LayoutUtil::findFreeSlot(snapped, w, h, existingBoxes, selfId);
}

void GraphEditor::finalizeModuleDrag(ModuleComponent* module) {
    if (module == nullptr)
        return;
    auto clear = resolvePlacement(module->getPosition(), module->getWidth(), module->getHeight(), module->getNodeId());
    module->setTopLeftPosition(clear);
    // Persist the snapped/cleared position to graph node properties so it survives reload.
    updateModulePosition(module);
    content.repaint();
}

void GraphEditor::animateDropLanding(ModuleComponent* module, juce::Point<int> fromPos, juce::Point<int> toPos) {
    if (module == nullptr)
        return;

    // If from == to nothing to animate — just ensure the module is at the final position.
    if (fromPos == toPos)
        return;

    // Place module at fromPos immediately so the first frame starts correctly.
    module->setTopLeftPosition(fromPos);

    juce::Component::SafePointer<GraphEditor> safeEditor(this);
    juce::Component::SafePointer<ModuleComponent> safeModule(module);

    dropLandingAnim.start(
        vblankUpdater,
        180.0, // ms — within the 150-220 ms spec
        synth::ui::easeOutCubic,
        [safeEditor, safeModule, fromPos, toPos](float t) {
            if (safeEditor == nullptr || safeModule == nullptr)
                return;
            auto pos = synth::ui::AnimationDriver::lerpBounds(juce::Rectangle<int>(fromPos.x, fromPos.y, 0, 0),
                                                              juce::Rectangle<int>(toPos.x, toPos.y, 0, 0), t)
                           .getTopLeft();
            safeModule->setTopLeftPosition(pos);
            safeEditor->content.repaint();
        },
        [safeEditor, safeModule, toPos]() {
            // Settle at exact final position and persist to graph properties.
            if (safeEditor == nullptr || safeModule == nullptr)
                return;
            safeModule->setTopLeftPosition(toPos);
            safeEditor->updateModulePosition(safeModule);
            safeEditor->content.repaint();
        });
}

void GraphEditor::autoArrange() {
    auto& graph = audioEngine.getGraph();
    if (undoManager)
        undoManager->captureBeforeState(graph);

    auto sizeOf = [this](synth::LayoutUtil::NodeID id) -> juce::Point<int> {
        for (auto* c : content.getModules()) {
            if (c != nullptr && c->getNodeId() == id)
                return {c->getWidth(), c->getHeight()};
        }
        return {synth::LayoutUtil::kSingleWidth, 300};
    };

    std::vector<std::pair<synth::LayoutUtil::NodeID, synth::LayoutUtil::NodeID>> extra;
    for (const auto& r : audioEngine.getModulationRoutings()) {
        if (r.hasSource && r.hasDest)
            extra.push_back({r.sourceNodeID, r.destNodeID});
    }

    auto layout = synth::LayoutUtil::computeAutoArrange(graph, sizeOf, extra);
    for (const auto& a : layout) {
        if (auto* n = graph.getNodeForId(a.id)) {
            n->properties.set("x", a.pos.x);
            n->properties.set("y", a.pos.y);
        }
    }

    updateComponents();

    if (undoManager)
        undoManager->pushSnapshotFromCapture(graph);
}

void GraphEditor::savePreset(juce::File file) {
    auto json = synth::AIStateMapper::graphToJSON(audioEngine.getGraph());
    file.replaceWithText(juce::JSON::toString(json));
}

bool GraphEditor::loadFactoryPreset(int index) {
    // Tear down existing module components (which stops their ScopeComponent timers) BEFORE
    // PresetManager clears the graph and frees the old VisualBuffers — otherwise a scope timer can
    // fire and read a freed buffer (use-after-free). Mirrors the safe order used by the test harness.
    detachAllModuleComponents();
    bool loaded = synth::PresetManager::loadPreset(index, audioEngine.getGraph());
    updateComponents();
    return loaded;
}

void GraphEditor::newPatch() {
    auto& graph = audioEngine.getGraph();

    auto doClear = [this, &graph] {
        // Detach BEFORE clearing — same ordering as loadFactoryPreset — so no ScopeComponent
        // timer fires against a freed VisualBuffer after graph.clear().
        detachAllModuleComponents();
        graph.clear();
        updateComponents(); // reconciles the now-empty view; the empty-canvas hint will show
    };

    if (undoManager) {
        undoManager->recordStructuralChange(graph, doClear);
    } else {
        doClear();
    }
    repaint();
}

void GraphEditor::loadPreset(juce::File file) {
    auto json = juce::JSON::parse(file);
    if (!json.isObject()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed",
                                               "Could not parse preset file.");
        return;
    }
    // Detach before applyJSONToGraph clears the graph (see loadFactoryPreset — avoids scope-timer UAF).
    detachAllModuleComponents();
    // The user picked this file from their own filesystem — trusted, unlike an AI-authored patch.
    if (synth::AIStateMapper::applyJSONToGraph(json, audioEngine.getGraph(), true, /*trusted=*/true)) {
        updateComponents();
    } else {
        updateComponents(); // reconcile view to whatever state the graph is in after a failed apply
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Failed",
                                               "Could not apply preset to graph.");
    }
}
