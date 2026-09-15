// Concern: FRO15 (P9-9) -- MixerSendList's paint, row menus, level-knob attachments and the four
// mutations (add / remove / retarget / pre-post), each ONE recordGraphAndMacroChange around
// synth::MixerSends' Core flows.
#include "MixerSendList.h"

#include "AppUndoManager.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

// Menu ids. Target ids start above the fixed items so one callback can tell them apart.
constexpr int kNewBusItemId = 1;
constexpr int kFirstTargetItemId = 100;

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}
} // namespace

MixerSendList::MixerSendList() { setInterceptsMouseClicks(true, true); }

MixerSendList::~MixerSendList() = default;

void MixerSendList::configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                              GraphEditor& graphEditor) {
    graph_ = &graph;
    undoManager_ = &undoManager;
    macros_ = &macros;
    graphEditor_ = &graphEditor;
}

void MixerSendList::setEntries(const std::vector<synth::MixerSendEntry>& entries, NodeID stripNodeId) {
    entries_ = entries;
    stripNodeId_ = stripNodeId;
    rebuildKnobs();
    resized();
    repaint();
}

void MixerSendList::rebuildKnobs() {
    rows_.clear();
    if (graph_ == nullptr)
        return;
    auto* strip = stripAt(*graph_, stripNodeId_);
    if (strip == nullptr)
        return;

    for (const auto& entry : entries_) {
        Row row;
        row.knob = std::make_unique<juce::Slider>();
        row.knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        row.knob->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        addAndMakeVisible(*row.knob);
        if (auto* param = strip->getSendLevelParameter(entry.slot)) {
            const auto range = param->getNormalisableRange();
            row.knob->setNormalisableRange(juce::NormalisableRange<double>((double)range.start, (double)range.end,
                                                                           (double)range.interval, (double)range.skew,
                                                                           range.symmetricSkew));
            row.attachment = std::make_unique<juce::SliderParameterAttachment>(*param, *row.knob);
        }
        rows_.push_back(std::move(row));
    }
}

void MixerSendList::unbindFromGraph() {
    for (auto& row : rows_)
        row.attachment.reset();
    graph_ = nullptr;
    undoManager_ = nullptr;
    macros_ = nullptr;
    graphEditor_ = nullptr;
}

bool MixerSendList::isAttachedForTest(int rowIndex) const {
    return rowIndex >= 0 && rowIndex < (int)rows_.size() && rows_[(size_t)rowIndex].attachment != nullptr;
}

int MixerSendList::getPreferredHeight() const noexcept {
    return ((int)entries_.size() + (canAddSend() ? 1 : 0)) * kRowHeight;
}

bool MixerSendList::canAddSend() const {
    if (graph_ == nullptr)
        return false;
    auto* strip = stripAt(*graph_, stripNodeId_);
    return strip != nullptr && strip->getActiveSendCount() < ChannelStripModule::kMaxSends;
}

int MixerSendList::rowIndexAt(juce::Point<int> position) const {
    if (position.y < 0)
        return -1;
    const int index = position.y / kRowHeight;
    return index >= 0 && index < (int)entries_.size() ? index : -1;
}

juce::String MixerSendList::targetNameFor(NodeID target) const {
    if (graph_ == nullptr || target == NodeID{})
        return "No target";
    auto* node = graph_->getNodeForId(target);
    if (node == nullptr)
        return "No target";
    if (macros_ != nullptr)
        if (const auto* macro = macros_->findByMember(node->properties["uuid"].toString()))
            return macro->name;
    return synth::isBusStrip(*graph_, target) ? synth::busFallbackName(*graph_, target) : juce::String("Channel");
}

void MixerSendList::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto text = laf != nullptr ? laf->getTheme().colors.textPrimary : juce::Colour(0xffEAEEF3);
    const auto muted = laf != nullptr ? laf->getTheme().colors.textMuted : juce::Colour(0xff8A93A0);
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);

    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    for (int i = 0; i < (int)entries_.size(); ++i) {
        const auto& entry = entries_[(size_t)i];
        auto row = getLocalBounds().withY(i * kRowHeight).withHeight(kRowHeight);

        auto remove = row.removeFromRight(kRemoveWidth);
        g.setColour(muted);
        g.drawText("x", remove, juce::Justification::centred, false);

        auto toggle = row.removeFromRight(kToggleWidth);
        g.setColour(entry.preFader ? accent : muted);
        g.drawText(entry.preFader ? "PRE" : "POST", toggle, juce::Justification::centred, false);

        row.removeFromRight(kKnobWidth); // the knob is a real child component -- see resized()

        g.setColour(entry.targetNodeId == juce::AudioProcessorGraph::NodeID{} ? muted : text);
        g.drawText(entry.targetName, row.reduced(2, 0), juce::Justification::centredLeft, true);
    }

    if (canAddSend()) {
        auto addRow = getLocalBounds().withY((int)entries_.size() * kRowHeight).withHeight(kRowHeight);
        g.setColour(accent);
        g.drawText("+ Send", addRow.reduced(2, 0), juce::Justification::centredLeft, false);
    }
}

void MixerSendList::resized() {
    for (int i = 0; i < (int)rows_.size(); ++i) {
        if (rows_[(size_t)i].knob == nullptr)
            continue;
        auto row = getLocalBounds().withY(i * kRowHeight).withHeight(kRowHeight);
        row.removeFromRight(kRemoveWidth + kToggleWidth);
        rows_[(size_t)i].knob->setBounds(row.removeFromRight(kKnobWidth).reduced(1));
    }
}

void MixerSendList::mouseDown(const juce::MouseEvent& event) {
    const int row = rowIndexAt(event.getPosition());
    if (row < 0) {
        if (canAddSend() && event.y >= (int)entries_.size() * kRowHeight)
            showAddMenu();
        return;
    }

    const int fromRight = getWidth() - event.x;
    if (fromRight <= kRemoveWidth)
        removeRow(row);
    else if (fromRight <= kRemoveWidth + kToggleWidth)
        togglePreFaderForRow(row);
    else if (fromRight > kRemoveWidth + kToggleWidth + kKnobWidth)
        showTargetMenu(row); // the knob's own bounds are its child component's business
}

std::vector<NodeID> MixerSendList::availableTargets() const {
    if (graph_ == nullptr)
        return {};
    return synth::enumerateSendTargets(*graph_, stripNodeId_);
}

void MixerSendList::showTargetMenu(int rowIndex) {
    const auto targets = availableTargets();
    juce::PopupMenu menu;
    menu.addItem(kNewBusItemId, "New bus...", createBus != nullptr);
    menu.addSeparator();
    for (int i = 0; i < (int)targets.size(); ++i)
        menu.addItem(kFirstTargetItemId + i, targetNameFor(targets[(size_t)i]));

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, rowIndex, targets](int result) {
        if (result == kNewBusItemId && createBus != nullptr) {
            if (const auto bus = createBus(); bus != NodeID{})
                retargetRow(rowIndex, bus);
        } else if (result >= kFirstTargetItemId && result - kFirstTargetItemId < (int)targets.size()) {
            retargetRow(rowIndex, targets[(size_t)(result - kFirstTargetItemId)]);
        }
    });
}

void MixerSendList::showAddMenu() {
    const auto targets = availableTargets();
    juce::PopupMenu menu;
    menu.addItem(kNewBusItemId, "New bus...", createBus != nullptr);
    menu.addSeparator();
    for (int i = 0; i < (int)targets.size(); ++i)
        menu.addItem(kFirstTargetItemId + i, targetNameFor(targets[(size_t)i]));

    menu.showMenuAsync(juce::PopupMenu::Options(), [this, targets](int result) {
        if (result == kNewBusItemId && createBus != nullptr) {
            if (const auto bus = createBus(); bus != NodeID{})
                addSendTo(bus);
        } else if (result >= kFirstTargetItemId && result - kFirstTargetItemId < (int)targets.size()) {
            addSendTo(targets[(size_t)(result - kFirstTargetItemId)]);
        }
    });
}

void MixerSendList::mutateAndNotify(const std::function<bool()>& mutation) {
    if (graph_ == nullptr || undoManager_ == nullptr || macros_ == nullptr || graphEditor_ == nullptr)
        return;
    bool changed = false;
    undoManager_->recordGraphAndMacroChange(*graph_, *macros_, [&] {
        changed = mutation();
        graphEditor_->updateComponents();
    });
    if (changed && onMutated)
        onMutated();
}

void MixerSendList::addSendTo(NodeID target) {
    mutateAndNotify([&] { return graph_ != nullptr && synth::addSend(*graph_, stripNodeId_, target) >= 0; });
}

void MixerSendList::removeRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const int slot = entries_[(size_t)rowIndex].slot;
    mutateAndNotify([&] { return synth::removeSend(*graph_, stripNodeId_, slot); });
}

void MixerSendList::retargetRow(int rowIndex, NodeID target) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const int slot = entries_[(size_t)rowIndex].slot;
    mutateAndNotify([&] { return synth::retargetSend(*graph_, stripNodeId_, slot, target); });
}

void MixerSendList::togglePreFaderForRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const int slot = entries_[(size_t)rowIndex].slot;
    mutateAndNotify([&] {
        auto* strip = stripAt(*graph_, stripNodeId_);
        if (strip == nullptr)
            return false;
        strip->setSendPreFader(slot, !strip->isSendPreFader(slot));
        return true;
    });
}

} // namespace synth::ui
