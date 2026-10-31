#include "ModMatrixComponent.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/MacroInletModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModDot/ModSourceCatalog.h"
#include "UI/Graph/ModMatrixCurveColumn.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include "UI/Graph/ModMatrixKeyboard.h"
#include "UI/Graph/ModMatrixLists.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/PluginKnobPicker/HostedModTargetPicker.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using Connection = juce::AudioProcessorGraph::Connection;

using Endpoint = synth::ui::RoutingEndpoint;
using synth::ui::attenuverterEdge;
using synth::ui::edgesAround;
using synth::ui::realEndpointBehindPorts;

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
        if (onShowPicker) {
            // ComboBox::showPopupIfNotActive() raised the private "menu active" flag before calling
            // this, and only the stock menu's close (or hidePopup) lowers it. The picker replaces that
            // menu, so lower it here: left raised, every later click and Return is ignored as "already
            // open", and the panel stops refreshing because the row reads as having a popup up.
            juce::ComboBox::hidePopup();
            onShowPicker();
        } else
            juce::ComboBox::showPopup();
    }
    // The arrows belong to the matrix's grid navigation (ModMatrixKeyboard.h), not to stepping through
    // the list: a stock combo re-points the routing on every arrow press, one undo step each.
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey || key == juce::KeyPress::leftKey ||
            key == juce::KeyPress::rightKey)
            return false;
        if (key == juce::KeyPress::spaceKey) {
            showPopup();
            return true;
        }
        return juce::ComboBox::keyPressed(key);
    }
    void paintOverChildren(juce::Graphics& g) override { synth::ui::paintFocusRegionOutline(*this, g); }
};

/** The row's amount slider: Up/Down nudge the amount (as on a mixer fader), Left/Right are left to the
 *  matrix's grid navigation so the slider is never a dead end for the keyboard. */
class AmountSlider : public juce::Slider {
public:
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
            return false;
        return juce::Slider::keyPressed(key);
    }
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
        if (rowIndex != index)
            applyRowNames(index);
        if (rowIndex != index)
            repaint();
        rowIndex = index;
    }
    // Screen-reader names carry the row number, so VoiceOver says which routing a control belongs to.
    void applyRowNames(int index);
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
    // The picker currently open for each combo, if any. A combo holds only its selected row: the lists are the panel's.
    juce::Component::SafePointer<juce::Component> sourcePicker;
    juce::Component::SafePointer<juce::Component> destPicker;
    AmountSlider amountSlider;
    juce::Label amountValueLabel;
    synth::ui::ModCurveChip curveChip;
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
    void refresh(const ModRoutingInfo& info, const synth::ConnectionIndex& cables);

    // Re-points the attenuverter's edges and runs the change as ONE undo step.
    // `claimDest`, when given, picks the destination inside the same undo step; invalid leaves the routing as it was.
    void reroute(bool sourceChanged, Endpoint source, Endpoint dest, std::function<Endpoint()> claimDest = {});
    void pickHostedDestination(const synth::ui::HostedDestination& dest);
    // Runs graph mutations, in order, through the macro-port seam (or plain, in a bare panel) as ONE undo step.
    void applyRoutingChange(const std::vector<std::function<bool()>>& mutations, bool slideAttenuverter);
};

ModMatrixComponent::ModMatrixComponent(AudioEngine& engine, AppUndoManager* undoMgr, GraphEditor* editor)
    : audioEngine(engine)
    , undoManager(undoMgr)
    , graphEditor(editor)
    , lists_(std::make_unique<synth::ui::ModMatrixLists>(engine.getGraph())) {
    // Makes grabKeyboardFocus() on THIS component (the "modMatrix" focus region's root)
    // succeed deterministically rather than depending on JUCE's position-ordered descent into
    // children finding a focus-wanting one (see the identical comment in ModuleLibraryComponent's
    // ctor). Also matters here because this component nests INSIDE the "canvas" region (it is a
    // child of GraphEditor) — see FocusRegionRegistry::regionContaining's nesting note.
    setWantsKeyboardFocus(true);
    setComponentID("modMatrix");
    addKeyListener(&synth::ui::modMatrixArrowKeys());
    contentContainer.addKeyListener(&synth::ui::modMatrixArrowKeys());

    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentContainer);
    // The row list is a scroll container, not a control: the arrow keys move between its rows'
    // controls and scroll the focused one into view (ModMatrixKeyboard.cpp).
    viewport.setWantsKeyboardFocus(false);

    addAndMakeVisible(addButton);
    addButton.setComponentID(synth::ui::modmatrix_ids::kAdd);
    addButton.setTooltip("Add a modulation routing");
    addButton.onClick = [this] { addModulation(); };

    addAndMakeVisible(flatToggle);
    flatToggle.onClick = [this] { setFlatSourceMenu(flatToggle.getToggleState()); };
    flatToggle.setToggleState(isSourceMenuFlat, juce::dontSendNotification);
    flatToggle.setComponentID(synth::ui::modmatrix_ids::kFlat);
    flatToggle.setTooltip("List sources in one flat list instead of grouped by category");

    startTimerHz(10);
}

ModMatrixComponent::~ModMatrixComponent() {
    stopTimer();
    removeKeyListener(&synth::ui::modMatrixArrowKeys());
    contentContainer.removeKeyListener(&synth::ui::modMatrixArrowKeys());
}

// A routing change a row is running can splice out a port, and that path clears the panel; freeing the row
// there would free the code still running. The clear waits for the next refresh instead.
void ModMatrixComponent::clearRows() {
    if (routingChangeDepth > 0) {
        clearPending = true;
        return;
    }
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

std::vector<std::pair<int, juce::String>> ModMatrixComponent::getSourceListForTest() {
    std::vector<std::pair<int, juce::String>> list;
    for (const auto& item : lists_->sources())
        list.emplace_back(item.id, item.text);
    return list;
}

juce::ComboBox* ModMatrixComponent::getRowDestComboForTest(int rowIndex) {
    return rowIndex >= 0 && rowIndex < (int)rows.size() ? &rows[(size_t)rowIndex]->destCombo : nullptr;
}

void ModMatrixComponent::setFlatSourceMenu(bool shouldBeFlat) {
    if (isSourceMenuFlat != shouldBeFlat) {
        isSourceMenuFlat = shouldBeFlat;
        lists_->setFlat(shouldBeFlat);
        updateRowsFromGraph();
    }
}

void ModMatrixComponent::paint(juce::Graphics& g) {
    // Resolve the themed LookAndFeel; in headless tests the default JUCE LnF is installed and the
    // cast returns null, so we fall back to the legacy literals.
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    const juce::Colour bgColour = synth::theme::themeOf(*this).colors.bg1;
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
    g.drawText("Modulation Matrix", titleArea.reduced(10, 0), juce::Justification::centredLeft, true);

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
    // Headers offset by kRowNumColW for the row numbers; the Amount column deliberately stops
    // short of the row's bypass/delete icon columns, leaving that header cell blank.
    g.drawText("Source", juce::Rectangle<float>(rowNumColW, (float)headerArea.getY(), sourceColW, 30.0f),
               juce::Justification::centred, true);
    g.drawText("Destination",
               juce::Rectangle<float>(rowNumColW + sourceColW, (float)headerArea.getY(), destColW, 30.0f),
               juce::Justification::centred, true);
    g.drawText("Amount",
               juce::Rectangle<float>(rowNumColW + sourceColW + destColW, (float)headerArea.getY(),
                                      (w - rowNumColW) * 0.25f, 30.0f),
               juce::Justification::centred, true);
    synth::ui::mod_matrix_curve::drawHeader(
        g, juce::Rectangle<float>((float)area.getWidth() - 60.0f - (float)synth::ui::mod_matrix_curve::kColumnWidth -
                                      (float)kGutter * 0.5f,
                                  (float)headerArea.getY(), (float)synth::ui::mod_matrix_curve::kColumnWidth, 30.0f));

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

void ModMatrixComponent::timerCallback() { updateRowsIfOpen(); }

void ModMatrixComponent::updateRowsIfOpen() {
    if (!isVisible()) {
        staleWhileClosed_ = true;
        // The tick also numbers same-type modules app-wide ("LFO 2"); that cheap check runs while closed too.
        if (const int nodes = audioEngine.getGraph().getNumNodes(); nodes != namedNodeCount_) {
            audioEngine.updateModuleNames();
            namedNodeCount_ = nodes;
        }
        return;
    }
    updateRowsFromGraph();
}

void ModMatrixComponent::visibilityChanged() {
    if (isVisible() && std::exchange(staleWhileClosed_, false))
        updateRowsFromGraph();
}

void ModMatrixComponent::updateRowsFromGraph() {
    if (routingChangeDepth > 0)
        return; // the next tick picks the change up, once the row that made it has returned
    if (std::exchange(clearPending, false))
        clearRows();
    auto activeRoutings = audioEngine.getActiveModRoutings();

    // Stable sort by NodeID so row numbers are consistent
    std::sort(activeRoutings.begin(), activeRoutings.end(),
              [](const AudioEngine::ModRoutingInfo& a, const AudioEngine::ModRoutingInfo& b) {
                  return a.attenuverterNodeID.uid < b.attenuverterNodeID.uid;
              });

    bool componentsChanged = false;

    std::unordered_map<juce::uint32, const AudioEngine::ModRoutingInfo*> routingByAttenuverter;
    routingByAttenuverter.reserve(activeRoutings.size());
    for (const auto& routing : activeRoutings)
        routingByAttenuverter[routing.attenuverterNodeID.uid] = &routing;

    // Rows whose routing has gone leave; a routing with no row gets one.
    std::unordered_set<juce::uint32> hasRow;
    hasRow.reserve(rows.size());
    for (int i = (int)rows.size() - 1; i >= 0; --i) {
        if (routingByAttenuverter.count(rows[(size_t)i]->attenuverterId.uid) == 0) {
            rows.erase(rows.begin() + i);
            componentsChanged = true;
        } else {
            hasRow.insert(rows[(size_t)i]->attenuverterId.uid);
        }
    }
    for (const auto& routing : activeRoutings) {
        if (hasRow.count(routing.attenuverterNodeID.uid) == 0) {
            auto newRow = std::make_unique<ModRow>(*this, routing.attenuverterNodeID);
            contentContainer.addAndMakeVisible(*newRow);
            rows.push_back(std::move(newRow));
            componentsChanged = true;
        }
    }

    // A node added or removed, or a module renamed, refreshes the shared lists before the rows read their labels. A
    // popup a person has open is left alone: the change is picked up on the first tick after it closes, since nothing
    // is recorded as seen until then.
    const int currentNodeCount = audioEngine.getGraph().getNumNodes();
    const bool countChanged = currentNodeCount != lastNodeCount;
    if (countChanged)
        audioEngine.updateModuleNames();
    const auto namesSignature = moduleTitlesSignature(audioEngine.getGraph());
    if ((countChanged || namesSignature != lastNamesSignature) && !anyPopupOpen()) {
        lists_->invalidate();
        lastNodeCount = currentNodeCount;
        lastNamesSignature = namesSignature;
    }

    // One cable index for the whole tick; every row reads it.
    const synth::ui::ConnectionIndex cables(audioEngine.getGraph());
    for (int i = 0; i < (int)rows.size(); ++i) {
        rows[(size_t)i]->setRowIndex(i);
        rows[(size_t)i]->refresh(*routingByAttenuverter.at(rows[(size_t)i]->attenuverterId.uid), cables);
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
    addAndMakeVisible(curveChip);

    bypassToggle = std::make_unique<juce::DrawableButton>("Bypass", juce::DrawableButton::ImageFitted);
    deleteButton = std::make_unique<juce::DrawableButton>("Delete", juce::DrawableButton::ImageFitted);
    addAndMakeVisible(*bypassToggle);
    addAndMakeVisible(*deleteButton);

    amountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    amountSlider.setWantsKeyboardFocus(true); // juce::Slider turns it off; the amount is a keyboard stop
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
    sourceCombo.setComponentID(synth::ui::modmatrix_ids::kSource);
    destCombo.setComponentID(synth::ui::modmatrix_ids::kDest);
    amountSlider.setComponentID(synth::ui::modmatrix_ids::kAmount);
    curveChip.setComponentID(synth::ui::modmatrix_ids::kCurve);
    synth::ui::mod_matrix_curve::wireChip(curveChip, owner.graphEditor, attenuverterId);
    sourceCombo.setTooltip("Modulation source. Click to search the modules that can drive this routing.");
    destCombo.setTooltip("Modulation destination. Click to search the parameters this routing can drive.");
    deleteButton->onClick = [this] {
        // Through the seam so the ports only this routing used go with it; the chain's ports are read before the
        // cut and swept whatever the auto-delete preference says, as every canvas removal does. Captures by value:
        // the sweep can rebuild the panel under this row.
        applyRoutingChange({[&engine = owner.audioEngine, editor = owner.graphEditor, atten = attenuverterId] {
                               const auto ports = editor != nullptr ? editor->modulationChainPorts(atten)
                                                                    : std::vector<juce::AudioProcessorGraph::NodeID>{};
                               engine.removeModRouting(atten);
                               if (editor != nullptr)
                                   editor->getMacroController().sweepOneSidedMacroPorts(ports,
                                                                                        /*ignorePreference=*/true);
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
    bypassToggle->setComponentID(synth::ui::modmatrix_ids::kBypass);
    deleteButton->setComponentID(synth::ui::modmatrix_ids::kDelete);
    amountSlider.setTooltip("Modulation amount, from -1 to +1. Up and Down nudge it.");
    applyRowNames(0);
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
    synth::ui::mod_matrix_curve::layoutChip(curveChip, area);
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
        const juce::Colour accentColour = synth::theme::themeOf(*this).colors.accent;
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

void ModMatrixComponent::ModRow::applyRowNames(int index) {
    const auto prefix = "Routing " + juce::String(index + 1) + " ";
    sourceCombo.setTitle(prefix + "source");
    destCombo.setTitle(prefix + "destination");
    amountSlider.setTitle(prefix + "amount");
    bypassToggle->setTitle(prefix + "bypass");
    synth::ui::mod_matrix_curve::nameChip(curveChip, index);
    deleteButton->setTitle("Remove routing " + juce::String(index + 1));
}

namespace {
int selectedRawId(juce::ComboBox& combo) { return (int)combo.getSelectedIdAsValue().getValue(); }

// The combo holds one row, the selected one: its closed label and all a screen reader needs. Id 0 is no selection.
void showSelection(juce::ComboBox& combo, int id, const juce::String& label) {
    if (id == 0 || label.isEmpty()) {
        if (combo.getNumItems() > 0 || selectedRawId(combo) != 0)
            combo.clear(juce::dontSendNotification);
        return;
    }
    if (combo.getNumItems() == 1 && combo.getItemId(0) == id && combo.getItemText(0) == label &&
        selectedRawId(combo) == id)
        return;
    combo.clear(juce::dontSendNotification);
    combo.addItem(label, id);
    combo.setSelectedId(id, juce::dontSendNotification);
}
} // namespace

// Opens the searchable picker over the row's combo. A pick selects the combo's id with a synchronous
// notification, so it lands in comboBoxChanged exactly like a menu choice would (and through the same
// macro-port routing). One picker per combo at a time.
void ModMatrixComponent::ModRow::showPicker(bool forSource) {
    auto& combo = forSource ? sourceCombo : destCombo;
    auto& open = forSource ? sourcePicker : destPicker;
    if (open != nullptr)
        return;

    // Destinations also list every plugin parameter, modulated or not, always in the same section.
    auto items = forSource ? owner.lists_->sources() : owner.lists_->destinations();
    int selected = selectedRawId(combo);
    // A source no longer listed keeps the row its closed box shows (see refresh).
    if (forSource && selected != 0 && owner.lists_->sourceLabel(selected).isEmpty())
        items.push_back({selected, {}, combo.getText()});
    auto& graph = owner.audioEngine.getGraph();
    const auto hosted =
        forSource ? std::map<int, synth::ui::HostedDestination>{} : synth::ui::hostedDestinations(graph);
    for (const auto& [id, dest] : hosted) {
        items.push_back({id, "Plugins", dest.label, {}, dest.boundChannel >= 0 ? "Modulated" : ""});
        if (dest.boundChannel >= 0 && selected == (int)((dest.node.uid << 8) | (uint32_t)dest.boundChannel))
            selected = id;
    }
    juce::Component::SafePointer<ModRow> safeRow(this);
    juce::Component::SafePointer<juce::ComboBox> safeCombo(&combo);
    auto picker = std::make_unique<synth::ui::ModMatrixPicker>(
        forSource ? "source" : "destination", std::move(items), selected, [safeCombo, safeRow, hosted](int id) {
            if (safeCombo == nullptr || safeRow == nullptr)
                return;
            if (const auto it = hosted.find(id); it != hosted.end())
                safeRow->pickHostedDestination(it->second);
            else
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
void ModMatrixComponent::ModRow::refresh(const AudioEngine::ModRoutingInfo& info,
                                         const synth::ConnectionIndex& cables) {
    ++graph_editor_paint::workCounters().modRowRefreshes;
    Endpoint source{info.sourceNodeID, info.sourceChannelIndex};
    Endpoint dest{info.destNodeID, info.destChannelIndex};
    if (auto* editor = owner.graphEditor) {
        const auto isPort = [editor](NodeID id) { return editor->getMacroController().nodeIsMacroPort(id); };
        if (const auto real = realEndpointBehindPorts(cables, attenuverterId, /*incoming=*/true, isPort); real.valid())
            source = real;
        if (const auto real = realEndpointBehindPorts(cables, attenuverterId, /*incoming=*/false, isPort); real.valid())
            dest = real;
    }
    // A routing saved from a raw channel that is no longer listed (an LFO's hidden pass-through, in a
    // project from before the list followed the card's jacks) keeps a row of its own, so the box never
    // goes blank and the routing stays editable.
    const int sourceId = source.valid() ? (int)((source.node.uid << 8) | (uint32_t)source.channel) : 0;
    auto sourceLabel = owner.lists_->sourceLabel(sourceId);
    if (sourceLabel.isEmpty() && source.valid())
        if (auto* node = owner.audioEngine.getGraph().getNodeForId(source.node))
            sourceLabel = synth::moduleTitle(*node) + " - Out " + juce::String(source.channel + 1);
    showSelection(sourceCombo, sourceId, sourceLabel);
    const int destId = dest.valid() ? (int)((dest.node.uid << 8) | (uint32_t)dest.channel) : 0;
    showSelection(destCombo, destId, owner.lists_->destinationLabel(destId));
    curveChip.refresh();
}

void ModMatrixComponent::ModRow::comboBoxChanged(juce::ComboBox* comboBox) {
    if (comboBox != &sourceCombo && comboBox != &destCombo)
        return;
    // By raw id: a pick is usually not the one row the combo holds yet.
    const auto decode = [](juce::ComboBox& combo) {
        const auto encoded = (juce::uint32)selectedRawId(combo);
        return Endpoint{NodeID(encoded >> 8), (int)(encoded & 0xFF)};
    };
    const auto source = decode(sourceCombo);
    const auto dest = decode(destCombo);
    const int picked = selectedRawId(*comboBox);
    if (const auto label =
            comboBox == &sourceCombo ? owner.lists_->sourceLabel(picked) : owner.lists_->destinationLabel(picked);
        label.isNotEmpty())
        showSelection(*comboBox, picked, label);
    if (source.valid() || dest.valid())
        reroute(comboBox == &sourceCombo, source, dest);
}

// The same binding a cable dropped on the card makes: the knob shows and the slot is claimed, with the routing, as one
// undo step.
void ModMatrixComponent::ModRow::pickHostedDestination(const synth::ui::HostedDestination& dest) {
    auto& graph = owner.audioEngine.getGraph();
    auto* editor = owner.graphEditor;
    if (!synth::ui::canClaimHostedDestination(graph, dest)) {
        if (editor != nullptr && editor->onStatusMessage)
            editor->onStatusMessage("This plugin has no free modulation slot");
        return;
    }
    auto landed = std::make_shared<Endpoint>();
    const auto claim = [&graph, editor, dest, landed] {
        const int channel = dest.paramId.isEmpty() ? dest.boundChannel
                            : editor != nullptr    ? synth::ui::bindHostedModTarget(*editor, dest.node, dest.paramId)
                                                   : synth::ui::claimHostedDestination(graph, dest);
        *landed = channel >= 0 ? Endpoint{dest.node, channel} : Endpoint{};
        return *landed;
    };
    const auto rawSource = (juce::uint32)selectedRawId(sourceCombo);
    juce::Component::SafePointer<ModRow> self(this);
    reroute(false, Endpoint{NodeID(rawSource >> 8), (int)(rawSource & 0xFF)}, Endpoint{}, claim);
    if (self != nullptr && landed->valid()) {
        const int id = (int)((landed->node.uid << 8) | (uint32_t)landed->channel);
        showSelection(self->destCombo, id, self->owner.lists_->destinationLabel(id));
    }
}

void ModMatrixComponent::ModRow::reroute(bool sourceChanged, Endpoint source, Endpoint dest,
                                         std::function<Endpoint()> claimDest) {
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
    // By value, never `this`: nothing here may reach back into the row once the change has started.
    const auto atten = attenuverterId;
    const bool claiming = (bool)claimDest;
    auto destCell = std::make_shared<Endpoint>(dest);
    const auto removeEdges = [atten, &graph, claiming, destCell] {
        if (claiming && !destCell->valid())
            return true;
        if (auto edge = attenuverterEdge(graph, atten, true))
            graph.removeConnection(*edge);
        if (auto edge = attenuverterEdge(graph, atten, false))
            graph.removeConnection(*edge);
        return true;
    };
    const auto addEdges = [atten, &graph, source, claiming, destCell] {
        if (claiming && !destCell->valid())
            return true;
        if (source.valid())
            graph.addConnection({{source.node, source.channel}, {atten, 0}});
        if (destCell->valid())
            graph.addConnection({{atten, 0}, {destCell->node, destCell->channel}});
        return true;
    };
    std::vector<std::function<bool()>> mutations;
    if (claiming)
        mutations.push_back([destCell, claimDest = std::move(claimDest)] {
            *destCell = claimDest();
            return destCell->valid();
        });
    mutations.push_back(removeEdges);
    mutations.push_back(addEdges);
    applyRoutingChange(mutations, /*slideAttenuverter=*/throughPorts);
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
    // The panel outlives its rows; this row may not (a port splice clears the panel), so the depth guard
    // lives on the panel and only locals are touched from here on.
    auto& panel = owner;
    ++panel.routingChangeDepth;
    const juce::ScopeGuard leave{[&panel] { --panel.routingChangeDepth; }};
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
