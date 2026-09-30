#include "ModMatrixComponent.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/MacroInletModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <functional>
#include <map>
#include <optional>

namespace {
// MacroInletModule deliberately declares NO getModulationTargets() — GraphEditor::connectPorts()
// relies on that empty list to keep a plain cable drop onto a Macro In's jack a plain connection,
// never auto-wrapped in a fresh attenuverter (Tests/Macros/MacroPortFlowTests.cpp's drop-a-cable tests
// pin that). A modulation cable through an attenuverter
// (docs/macros/auto-ports.md#a-modulation-cable-through-an-attenuverter) can still
// splice a MacroInletModule in as the DESTINATION of an EXISTING AttenuverterChain crossing a macro boundary, so this
// row's destination combo needs something to show and match against — without changing what the module declares
// globally (which would resurrect the auto-wrap problem for ordinary drops). Display-only: this never becomes a real
// ModulationTarget the module advertises anywhere else. A spliced port is always Mono with its one active raw channel
// at 0 (docs/macros/ports.md#a-port-shape-is-chosen-at-creation-and-then-fixed /
// docs/macros/auto-ports.md#a-modulation-cable-through-an-attenuverter — the internal jack an AttenuverterChain lands
// on is never poly-fanned), so channel 0 is the only candidate.
std::vector<ModulationTarget> destinationCandidatesForCombo(ModuleBase* module) {
    auto targets = module->getModulationTargets();
    if (targets.empty() && dynamic_cast<MacroInletModule*>(module) != nullptr)
        targets.push_back({"In", 0});
    return targets;
}

using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;

/** One end of a routing: a node and the channel the attenuverter's edge lands on or leaves from. */
struct Endpoint {
    NodeID node;
    int channel = 0;
    bool valid() const noexcept { return node.uid != 0; }
};

/** The attenuverter's channel-0 edge in (incoming) or out, if it has one. */
std::optional<Connection> attenuverterEdge(juce::AudioProcessorGraph& graph, NodeID atten, bool incoming) {
    for (const auto& c : graph.getConnections())
        if (incoming ? (c.destination.nodeID == atten && c.destination.channelIndex == 0)
                     : (c.source.nodeID == atten && c.source.channelIndex == 0))
            return c;
    return std::nullopt;
}

/** The edges landing on and leaving `node`. */
void edgesAround(juce::AudioProcessorGraph& graph, NodeID node, std::vector<Connection>& in,
                 std::vector<Connection>& out) {
    for (const auto& c : graph.getConnections()) {
        if (c.destination.nodeID == node)
            in.push_back(c);
        if (c.source.nodeID == node)
            out.push_back(c);
    }
}

/** The real module at one end of the attenuverter's routing: the far end of its edge, looking through
 *  every macro port that routing alone uses (one edge in, one out). Invalid when the edge is absent. */
Endpoint realEndpointBehindPorts(juce::AudioProcessorGraph& graph, NodeID atten, bool incoming,
                                 const std::function<bool(NodeID)>& isPort) {
    auto edge = attenuverterEdge(graph, atten, incoming);
    if (!edge)
        return {};
    for (;;) {
        const NodeID far = incoming ? edge->source.nodeID : edge->destination.nodeID;
        if (!isPort(far))
            break;
        std::vector<Connection> in, out;
        edgesAround(graph, far, in, out);
        if (in.size() != 1 || out.size() != 1)
            break; // shared with another routing: it stays where it is
        edge = incoming ? in.front() : out.front();
    }
    return incoming ? Endpoint{edge->source.nodeID, edge->source.channelIndex}
                    : Endpoint{edge->destination.nodeID, edge->destination.channelIndex};
}

/** Moves the attenuverter downstream of every macro inlet its output feeds, so a routing entering a macro
 *  reads source -> inlet -> attenuverter -> member: the shape a dragged cable builds, and the one that
 *  keeps the real destination (not the port) on the matrix row. The crossing-plan splice the programmatic
 *  seam reuses puts the port after the attenuverter instead (the grouping-time shape). */
void slideAttenuverterPastInlets(juce::AudioProcessorGraph& graph, NodeID atten) {
    for (;;) {
        const auto in = attenuverterEdge(graph, atten, true);
        const auto out = attenuverterEdge(graph, atten, false);
        if (!in || !out)
            return;
        auto* portNode = graph.getNodeForId(out->destination.nodeID);
        if (portNode == nullptr || dynamic_cast<MacroInletModule*>(portNode->getProcessor()) == nullptr)
            return;
        std::vector<Connection> portIn, portOut;
        edgesAround(graph, portNode->nodeID, portIn, portOut);
        if (portIn.size() != 1 || portOut.size() != 1)
            return;
        const Connection next = portOut.front();
        graph.removeConnection(*in);
        graph.removeConnection(*out);
        graph.removeConnection(next);
        graph.addConnection({in->source, out->destination});
        graph.addConnection({next.source, {atten, 0}});
        graph.addConnection({{atten, 0}, next.destination});
    }
}

/** A hash of every module title the combos list. Cheap enough for the 10 Hz tick, and it changes when a
 *  card is renamed or the auto-numbering shifts, neither of which changes the node count. */
size_t moduleTitlesSignature(juce::AudioProcessorGraph& graph) {
    juce::uint64 hash = 1469598103934665603ull;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<ModuleBase*>(node->getProcessor()) == nullptr)
            continue;
        hash = (hash ^ node->nodeID.uid) * 1099511628211ull;
        hash = (hash ^ (juce::uint64)synth::moduleTitle(*node).hashCode64()) * 1099511628211ull;
    }
    return (size_t)hash;
}

/** A combo that shows its closed label as usual but opens the searchable picker instead of a menu. With no
 *  picker hook wired it falls back to the stock menu. Reachable by keyboard (Return/Space open it, like any
 *  combo) and outlined in the accent colour while focused. */
class PickerComboBox : public juce::ComboBox {
public:
    std::function<void()> onShowPicker;

    void showPopup() override {
        if (onShowPicker)
            onShowPicker();
        else
            juce::ComboBox::showPopup();
    }
    void paintOverChildren(juce::Graphics& g) override { synth::ui::paintFocusRegionOutline(*this, g); }
};
} // namespace

struct ModMatrixComponent::ModRow
    : public juce::Component
    , public juce::ComboBox::Listener
    , public juce::AudioProcessorParameter::Listener {
    ModRow(ModMatrixComponent& owner, juce::AudioProcessorGraph::NodeID id);

    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    ~ModRow() override;

    void setRowIndex(int index) {
        rowIndex = index;
        repaint();
    }
    int rowIndex = 0;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void comboBoxChanged(juce::ComboBox* comboBox) override;
    void lookAndFeelChanged() override;

    // Re-applies the themed bypass/delete icons; called from the constructor and again from
    // lookAndFeelChanged() on every theme switch (mirrors ModuleComponent::applyHeaderButtonIcons).
    void applyButtonIcons();

    ModMatrixComponent& owner;
    juce::AudioProcessorGraph::NodeID attenuverterId;

    // Keeps the attenuverter's processor alive for at least as long as this row holds parameter
    // attachments into it. juce::ParameterAttachment's destructor unconditionally calls
    // parameter.removeListener() on the reference it captured at construction, so the processor
    // MUST outlive amountAttachment/bypassAttachment — including when the node has already been
    // removed from the graph (removeModRouting) before updateRowsFromGraph() erases this row.
    // Graph nodes are reference counted; removeNode() drops the node from the processing list
    // immediately, and holding this Ptr only defers destruction of the object itself.
    juce::AudioProcessorGraph::Node::Ptr attenuverterNode;

    PickerComboBox sourceCombo;
    PickerComboBox destCombo;
    // What each picker lists, rebuilt with the combos; and the picker currently open for each, if any.
    std::vector<synth::ui::ModMatrixPicker::Item> sourceItems;
    std::vector<synth::ui::ModMatrixPicker::Item> destItems;
    juce::Component::SafePointer<juce::Component> sourcePicker;
    juce::Component::SafePointer<juce::Component> destPicker;
    juce::Slider amountSlider;
    juce::Label amountValueLabel;
    std::unique_ptr<juce::DrawableButton> bypassToggle;
    std::unique_ptr<juce::DrawableButton> deleteButton;

    std::unique_ptr<juce::SliderParameterAttachment> amountAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;

    std::map<int, float> gestureStartValues;

    bool isPopupOpen() const {
        return sourceCombo.isPopupActive() || destCombo.isPopupActive() || sourcePicker != nullptr ||
               destPicker != nullptr;
    }
    void showPicker(bool forSource);

    void detach();
    void refresh(const ModRoutingInfo& info);
    void populateCombos();

    // Re-points the attenuverter's edges and runs the change as ONE undo step.
    void reroute(bool sourceChanged, Endpoint source, Endpoint dest);
    // Runs graph mutations, in order, through the macro-port seam (or plain, in a bare panel) as ONE undo step.
    void applyRoutingChange(const std::vector<std::function<bool()>>& mutations, bool slideAttenuverter);
};

ModMatrixComponent::ModMatrixComponent(AudioEngine& engine, AppUndoManager* undoMgr, GraphEditor* editor)
    : audioEngine(engine)
    , undoManager(undoMgr)
    , graphEditor(editor) {
    // Makes grabKeyboardFocus() on THIS component (the "modMatrix" focus region's root)
    // succeed deterministically rather than depending on JUCE's position-ordered descent into
    // children finding a focus-wanting one (see the identical comment in ModuleLibraryComponent's
    // ctor). Also matters here because this component nests INSIDE the "canvas" region (it is a
    // child of GraphEditor) — see FocusRegionRegistry::regionContaining's nesting note.
    setWantsKeyboardFocus(true);

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

void ModMatrixComponent::clearRows() {
    rows.clear();
    repaint();
}

juce::String ModMatrixComponent::getRowSourceComboTextForTest(int rowIndex) const {
    if (rowIndex < 0 || rowIndex >= (int)rows.size())
        return {};
    return rows[(size_t)rowIndex]->sourceCombo.getText();
}

juce::String ModMatrixComponent::getRowDestComboTextForTest(int rowIndex) const {
    if (rowIndex < 0 || rowIndex >= (int)rows.size())
        return {};
    return rows[(size_t)rowIndex]->destCombo.getText();
}

juce::ComboBox* ModMatrixComponent::getRowSourceComboForTest(int rowIndex) {
    return rowIndex >= 0 && rowIndex < (int)rows.size() ? &rows[(size_t)rowIndex]->sourceCombo : nullptr;
}

juce::ComboBox* ModMatrixComponent::getRowDestComboForTest(int rowIndex) {
    return rowIndex >= 0 && rowIndex < (int)rows.size() ? &rows[(size_t)rowIndex]->destCombo : nullptr;
}

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
    const juce::Colour borderColour =
        lf != nullptr ? lf->getTheme().colors.border : juce::Colours::white.withAlpha(0.08f);

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

    const float rowNumColW = (float)kRowNumColW;
    const float w = (float)area.getWidth();
    const float sourceColW = (w - rowNumColW) * kSourceColFrac;
    const float destColW = (w - rowNumColW) * kDestColFrac;
    // Headers offset by kRowNumColW for the row numbers; the AMOUNT column deliberately stops
    // short of the row's bypass/delete icon columns, leaving that header cell blank.
    g.drawText("SOURCE", juce::Rectangle<float>(rowNumColW, (float)headerArea.getY(), sourceColW, 30.0f),
               juce::Justification::centred, true);
    g.drawText("DESTINATION",
               juce::Rectangle<float>(rowNumColW + sourceColW, (float)headerArea.getY(), destColW, 30.0f),
               juce::Justification::centred, true);
    g.drawText("AMOUNT",
               juce::Rectangle<float>(rowNumColW + sourceColW + destColW, (float)headerArea.getY(),
                                      (w - rowNumColW) * 0.25f, 30.0f),
               juce::Justification::centred, true);

    // Footer separator: hairline above the add/flat-toggle controls, matching the header band's
    // painted treatment so the row list reads as visually distinct from the footer chrome.
    auto footerArea = area.removeFromBottom(40);
    g.setColour(borderColour);
    g.fillRect(footerArea.getX(), footerArea.getY(), footerArea.getWidth(), 1);

    if (rows.empty()) {
        g.setColour(textDisabledColour);
        g.setFont(juce::Font(14.0f, juce::Font::italic));
        g.drawText("No modulations active.\nClick '+ Add Modulation' to start.", area, juce::Justification::centred,
                   true);
    }
}

// Focus-region outline (Source/UI/Layout/FocusRegion.h) -- see the paintOverChildren declaration's
// comment in the header for why this can't just be tacked onto the end of paint() above.
void ModMatrixComponent::paintOverChildren(juce::Graphics& g) { synth::ui::paintFocusRegionOutline(*this, g); }

void ModMatrixComponent::resized() {
    auto area = getLocalBounds();
    auto titleArea = area.removeFromTop(40); // Title
    area.removeFromTop(30);                  // Column Headers

    auto footer = area.removeFromBottom(40);
    addButton.setBounds(footer.removeFromRight(150).reduced(kGutter));
    flatToggle.setBounds(footer.removeFromLeft(120).reduced(kGutter));

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

    // Refresh combo items if the number of nodes in graph changed, or a module was renamed. This must
    // happen BEFORE the refresh pass below: populateCombos() clears the combo boxes (deselecting them),
    // so a selection applied first would be wiped and every row's label would go blank until the next
    // update call. A popup a person has open is left alone (clearing under it would pull its items
    // away); the change is picked up on the first tick after it closes, since nothing is recorded as
    // seen until then.
    const int currentNodeCount = audioEngine.getGraph().getNumNodes();
    const bool countChanged = currentNodeCount != lastNodeCount;
    if (countChanged)
        audioEngine.updateModuleNames();
    const auto namesSignature = moduleTitlesSignature(audioEngine.getGraph());
    if ((countChanged || namesSignature != lastNamesSignature) && !anyPopupOpen()) {
        for (auto& row : rows)
            row->populateCombos();
        lastNodeCount = currentNodeCount;
        lastNamesSignature = namesSignature;
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

    if (componentsChanged) {
        setHoveredRow(-1);
        resized();
        repaint();
    }
}

bool ModMatrixComponent::anyPopupOpen() const {
    return std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row->isPopupOpen(); });
}

void ModMatrixComponent::addModulation() {
    // Just add an unconnected attenuverter node to create an "empty" row
    auto add = [this] { audioEngine.addEmptyModRouting(); };
    if (undoManager)
        undoManager->recordStructuralChange(audioEngine.getGraph(), add);
    else
        add();
    updateRowsFromGraph();
}

// --- ModRow Implementation ---

ModMatrixComponent::ModRow::ModRow(ModMatrixComponent& o, juce::AudioProcessorGraph::NodeID id)
    : owner(o)
    , attenuverterId(id) {
    addAndMakeVisible(sourceCombo);
    addAndMakeVisible(destCombo);
    addAndMakeVisible(amountSlider);
    addAndMakeVisible(amountValueLabel);

    bypassToggle = std::make_unique<juce::DrawableButton>("Bypass", juce::DrawableButton::ImageFitted);
    deleteButton = std::make_unique<juce::DrawableButton>("Delete", juce::DrawableButton::ImageFitted);
    addAndMakeVisible(*bypassToggle);
    addAndMakeVisible(*deleteButton);

    amountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    amountSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    amountSlider.setRange(-1.0, 1.0);
    amountSlider.onValueChange = [this] {
        amountValueLabel.setText(juce::String(amountSlider.getValue(), 2), juce::dontSendNotification);
    };

    // Slider colours from theme tokens when the themed LnF is installed; otherwise leave the
    // ColourId defaults (the LnF maps them in applyTheme(), so explicit literals are not needed).
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&owner.getLookAndFeel())) {
        const auto& colors = lf->getTheme().colors;
        amountSlider.setColour(juce::Slider::thumbColourId, colors.knobPointer);
        amountSlider.setColour(juce::Slider::trackColourId, colors.accent);
        amountSlider.setColour(juce::Slider::backgroundColourId, colors.surface);
        amountValueLabel.setColour(juce::Label::textColourId, colors.textPrimary);
    }
    amountValueLabel.setJustificationType(juce::Justification::centredRight);
    amountValueLabel.setFont(12.0f);

    sourceCombo.addListener(this);
    destCombo.addListener(this);
    sourceCombo.onShowPicker = [this] { showPicker(true); };
    destCombo.onShowPicker = [this] { showPicker(false); };
    sourceCombo.setTitle("Modulation source");
    destCombo.setTitle("Modulation destination");
    sourceCombo.setTooltip("Modulation source. Click to search the modules that can drive this routing.");
    destCombo.setTooltip("Modulation destination. Click to search the parameters this routing can drive.");
    deleteButton->onClick = [this] {
        // Through the seam so the ports only this routing used go with it.
        applyRoutingChange({[this] {
                               owner.audioEngine.removeModRouting(attenuverterId);
                               return true;
                           }},
                           /*slideAttenuverter=*/false);
    };

    bypassToggle->setClickingTogglesState(true);
    bypassToggle->setTooltip("Bypass modulation");
    deleteButton->setTooltip("Remove this modulation routing");
    // Keep only the semantic on-colour from the theme (bypass-active = warning). The off-state
    // and base colours re-skin via the LnF ColourId mapping. When unthemed, leave defaults.
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&owner.getLookAndFeel())) {
        const auto& colors = lf->getTheme().colors;
        bypassToggle->setColour(juce::DrawableButton::backgroundOnColourId, colors.warning.withAlpha(0.35f));
    }
    bypassToggle->setComponentID("modBypass");
    deleteButton->setComponentID("modDelete");
    bypassToggle->setTitle("Bypass modulation");
    deleteButton->setTitle("Remove modulation routing");
    amountSlider.setTitle("Modulation amount");
    amountSlider.setTooltip("Modulation amount, from -1 to +1.");
    applyButtonIcons();

    // Attach to attenuverter params
    if (auto* node = owner.audioEngine.getGraph().getNodeForId(attenuverterId)) {
        // Retain the node so the processor (and therefore the parameters the attachments below
        // reference) cannot be freed out from under us while this row is still alive.
        attenuverterNode = node;

        const auto& params = node->getProcessor()->getParameters();
        if (params.size() > 0) {
            if (auto* bParam = dynamic_cast<juce::AudioParameterBool*>(params[0])) {
                bypassAttachment = std::make_unique<juce::ButtonParameterAttachment>(*bParam, *bypassToggle);
            }
        }
        if (params.size() > 1) {
            if (auto* param = dynamic_cast<juce::AudioParameterFloat*>(params[1])) {
                amountAttachment = std::make_unique<juce::SliderParameterAttachment>(*param, amountSlider);
                // SliderParameterAttachment's constructor moves the slider to the current value but
                // does not fire onValueChange itself, so the readout would start blank without this.
                amountValueLabel.setText(juce::String(amountSlider.getValue(), 2), juce::dontSendNotification);
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
    const int gutter = ModMatrixComponent::kGutter;
    auto area = getLocalBounds().reduced(gutter / 2);

    // Space for row number — matches ModMatrixComponent::paint()'s header column offset.
    area.removeFromLeft(ModMatrixComponent::kRowNumColW);

    // Proportional layout — fractions shared with the header labels in ModMatrixComponent::paint().
    int totalWidth = area.getWidth();
    sourceCombo.setBounds(
        area.removeFromLeft((int)(totalWidth * ModMatrixComponent::kSourceColFrac)).reduced(gutter / 2));
    destCombo.setBounds(area.removeFromLeft((int)(totalWidth * ModMatrixComponent::kDestColFrac)).reduced(gutter / 2));
    deleteButton->setBounds(area.removeFromRight(30).reduced(gutter / 2));
    bypassToggle->setBounds(area.removeFromRight(30).reduced(gutter / 2));
    amountValueLabel.setBounds(area.removeFromRight(36).reduced(gutter / 2));
    amountSlider.setBounds(area.reduced(gutter / 2));
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
    g.drawText(juce::String(rowIndex + 1), 0, 0, ModMatrixComponent::kRowNumColW, getHeight(),
               juce::Justification::centred);

    // --- Bottom hairline separator ---
    g.setColour(lf != nullptr ? lf->getTheme().colors.border : juce::Colours::white.withAlpha(0.08f));
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void ModMatrixComponent::ModRow::lookAndFeelChanged() { applyButtonIcons(); }

void ModMatrixComponent::ModRow::applyButtonIcons() {
    // Headless-safe: when our themed LnF is not installed (unit tests), buttons remain blank
    // (no image set). The DrawableButton still exists and functions correctly without an image.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&owner.getLookAndFeel());
    if (lf == nullptr)
        return;

    if (bypassToggle) {
        if (auto d = lf->getIcon(synth::theme::Icon::ModuleBypass))
            bypassToggle->setImages(d.get());
    }
    if (deleteButton) {
        if (auto d = lf->getIcon(synth::theme::Icon::ModuleDelete))
            deleteButton->setImages(d.get());
    }
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
    sourceItems.clear();
    destItems.clear();

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
                juce::String displayName = synth::moduleTitle(*node);

                for (int i = 0; i < module->getTotalNumOutputChannels(); ++i) {
                    int itemId = (int)((node->nodeID.uid << 8) | (uint32_t)i);
                    juce::String label = displayName;
                    if (module->getTotalNumOutputChannels() > 1)
                        label += " Out " + juce::String(i + 1);
                    sourceCombo.addItem(label, itemId);
                    sourceItems.push_back({itemId, {}, label});
                }

                auto targets = destinationCandidatesForCombo(module);
                for (const auto& target : targets) {
                    int itemId = (int)((node->nodeID.uid << 8) | (uint32_t)target.channelIndex);
                    destCombo.addItem(displayName + ": " + target.name, itemId);
                    destItems.push_back({itemId, {}, displayName + ": " + target.name});
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
            int sourceCount = 0;
            int destCount = 0;

            for (auto* node : modules) {
                auto* module = static_cast<ModuleBase*>(node->getProcessor());
                juce::String displayName = synth::moduleTitle(*node);

                if (module->getTotalNumOutputChannels() > 0) {
                    if (module->getTotalNumOutputChannels() == 1) {
                        int itemId = (int)((node->nodeID.uid << 8) | 0);
                        catSourceSub.addItem(itemId, displayName);
                        sourceCombo.addItem(displayName, itemId);
                        sourceItems.push_back({itemId, categoryNames[cat], displayName});
                    } else {
                        juce::PopupMenu instSourceSub;
                        for (int i = 0; i < module->getTotalNumOutputChannels(); ++i) {
                            int itemId = (int)((node->nodeID.uid << 8) | (uint32_t)i);
                            // The closed-combobox label is resolved purely from this leaf item's own
                            // text (JUCE never concatenates ancestor submenu titles), so the module
                            // name must live here too, or a multi-output module's box shows a bare
                            // "Out N" with no way to tell which module it is.
                            instSourceSub.addItem(itemId, displayName + " - Out " + juce::String(i + 1));
                            sourceCombo.addItem(displayName + " Out " + juce::String(i + 1), itemId);
                            sourceItems.push_back(
                                {itemId, categoryNames[cat], displayName + " - Out " + juce::String(i + 1)});
                        }
                        catSourceSub.addSubMenu(displayName, instSourceSub);
                    }
                    sourceCount++;
                }

                auto targets = destinationCandidatesForCombo(module);
                if (!targets.empty()) {
                    juce::PopupMenu instDestSub;
                    for (const auto& target : targets) {
                        int itemId = (int)((node->nodeID.uid << 8) | (uint32_t)target.channelIndex);
                        // Same fix as the source leaves above: the closed combo box's label comes
                        // only from this item's own text, never from the submenu title, so the
                        // module name has to be baked in here too.
                        instDestSub.addItem(itemId, displayName + " - " + target.name);
                        destCombo.addItem(displayName + ": " + target.name, itemId);
                        destItems.push_back({itemId, categoryNames[cat], displayName + " - " + target.name});
                    }
                    catDestSub.addSubMenu(displayName, instDestSub);
                    destCount++;
                }
            }

            if (sourceCount > 0)
                sourceMenu.addSubMenu(categoryNames[cat], catSourceSub);
            if (destCount > 0)
                destMenu.addSubMenu(categoryNames[cat], catDestSub);
        }

        *sourceCombo.getRootMenu() = sourceMenu;
        *destCombo.getRootMenu() = destMenu;
    }
}

// Opens the searchable picker over the row's combo. A pick selects the combo's id with a synchronous
// notification, so it lands in comboBoxChanged exactly like a menu choice would (and through the same
// macro-port routing). One picker per combo at a time.
void ModMatrixComponent::ModRow::showPicker(bool forSource) {
    auto& combo = forSource ? sourceCombo : destCombo;
    auto& open = forSource ? sourcePicker : destPicker;
    if (open != nullptr)
        return;

    juce::Component::SafePointer<juce::ComboBox> safeCombo(&combo);
    auto picker = std::make_unique<synth::ui::ModMatrixPicker>(
        forSource ? "source" : "destination", forSource ? sourceItems : destItems, combo.getSelectedId(),
        [safeCombo](int id) {
            if (safeCombo != nullptr)
                safeCombo->setSelectedId(id, juce::sendNotificationSync);
        });
    open = picker.get();
    if (owner.pickerLauncher)
        owner.pickerLauncher(std::move(picker), combo.getScreenBounds());
    else
        juce::CallOutBox::launchAsynchronously(std::move(picker), combo.getScreenBounds(), nullptr);
}

// A routing that crosses a macro border runs through ports only it uses; the row names the real modules
// behind them (the LFO, not "In 1"), so what a person picked is what they read back and can search for.
void ModMatrixComponent::ModRow::refresh(const AudioEngine::ModRoutingInfo& info) {
    Endpoint source{info.sourceNodeID, info.sourceChannelIndex};
    Endpoint dest{info.destNodeID, info.destChannelIndex};
    if (auto* editor = owner.graphEditor) {
        auto& graph = owner.audioEngine.getGraph();
        const auto isPort = [editor](NodeID id) { return editor->getMacroController().nodeIsMacroPort(id); };
        if (const auto real = realEndpointBehindPorts(graph, attenuverterId, /*incoming=*/true, isPort); real.valid())
            source = real;
        if (const auto real = realEndpointBehindPorts(graph, attenuverterId, /*incoming=*/false, isPort); real.valid())
            dest = real;
    }
    sourceCombo.setSelectedId((int)((source.node.uid << 8) | (uint32_t)source.channel), juce::dontSendNotification);
    destCombo.setSelectedId((int)((dest.node.uid << 8) | (uint32_t)dest.channel), juce::dontSendNotification);
}

void ModMatrixComponent::ModRow::comboBoxChanged(juce::ComboBox* comboBox) {
    if (comboBox != &sourceCombo && comboBox != &destCombo)
        return;
    const auto decode = [](const juce::ComboBox& combo) {
        const auto encoded = (juce::uint32)combo.getSelectedId();
        return Endpoint{NodeID(encoded >> 8), (int)(encoded & 0xFF)};
    };
    const auto source = decode(sourceCombo);
    const auto dest = decode(destCombo);
    if (source.valid() || dest.valid())
        reroute(comboBox == &sourceCombo, source, dest);
}

void ModMatrixComponent::ModRow::reroute(bool sourceChanged, Endpoint source, Endpoint dest) {
    auto& graph = owner.audioEngine.getGraph();
    auto* editor = owner.graphEditor;
    const bool throughPorts = editor != nullptr && editor->getAutoCreateMacroPortsOnDragEnabled();

    if (throughPorts) {
        // The side the user did not touch may sit behind a port this routing alone uses, and the
        // combo shows that port. Re-point from the real module behind it, so the old port goes when it
        // is no longer needed and a fresh one is minted where the new path needs it.
        const auto isPort = [editor](NodeID id) { return editor->getMacroController().nodeIsMacroPort(id); };
        const auto kept = realEndpointBehindPorts(graph, attenuverterId, /*incoming=*/!sourceChanged, isPort);
        if (kept.valid())
            (sourceChanged ? dest : source) = kept;
    }

    // Two steps, because the seam only routes edges that are NEW across the call: tearing the old
    // edges down first lets it sweep the ports they leave idle, and re-adding both ends afterwards
    // makes the untouched end a fresh edge too, so it gets a port if the new path needs one.
    const auto removeEdges = [this, &graph] {
        if (auto edge = attenuverterEdge(graph, attenuverterId, true))
            graph.removeConnection(*edge);
        if (auto edge = attenuverterEdge(graph, attenuverterId, false))
            graph.removeConnection(*edge);
        return true;
    };
    const auto addEdges = [this, &graph, source, dest] {
        if (source.valid())
            graph.addConnection({{source.node, source.channel}, {attenuverterId, 0}});
        if (dest.valid())
            graph.addConnection({{attenuverterId, 0}, {dest.node, dest.channel}});
        return true;
    };
    applyRoutingChange({removeEdges, addEdges}, /*slideAttenuverter=*/throughPorts);
}

// With a canvas behind the panel, the edit goes through the macro-port seam the mixer sends use: the edge
// and every port it mints or strands are ONE undo step, and the canvas relays out afterwards. A bare panel
// (headless unit tests) has no macros to honour and does a plain graph edit.
void ModMatrixComponent::ModRow::applyRoutingChange(const std::vector<std::function<bool()>>& mutations,
                                                    bool slideAttenuverter) {
    auto& graph = owner.audioEngine.getGraph();
    auto* editor = owner.graphEditor;
    if (editor == nullptr) {
        const auto runAll = [&] {
            for (const auto& mutation : mutations)
                mutation();
        };
        if (owner.undoManager)
            owner.undoManager->recordStructuralChange(graph, runAll);
        else
            runAll();
        return;
    }

    const bool autoPorts = editor->getAutoCreateMacroPortsOnDragEnabled();
    const auto id = attenuverterId;
    auto step = [&] {
        for (const auto& mutation : mutations)
            editor->getMacroController().applyProgrammaticConnectionChange(autoPorts, mutation);
        if (slideAttenuverter)
            slideAttenuverterPastInlets(graph, id);
        editor->updateComponents();
    };
    if (owner.undoManager)
        owner.undoManager->recordGraphAndMacroChange(graph, editor->getMacros(), step);
    else
        step();
}
