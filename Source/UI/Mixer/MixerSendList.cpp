// Concern: MixerSendList's paint, row menus, level-knob attachments and the four
// mutations (add / remove / retarget / pre-post), each ONE recordGraphAndMacroChange around
// synth::MixerSends' Core flows.
#include "MixerSendList.h"

#include "AppUndoManager.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "MixerDbAccessibilityText.h"
#include "MixerPanAccessibilityText.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Layout/ReorderDrag/ReorderLiftLook.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

ChannelStripModule* stripAt(juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}
} // namespace

MixerSendList::MixerSendList() {
    setInterceptsMouseClicks(true, true);
    addSendProxy_.setInterceptsMouseClicks(false, false);
    addSendProxy_.setTitle("Add send");
    addSendProxy_.setVisible(false);
    addAndMakeVisible(addSendProxy_);
}

MixerSendList::~MixerSendList() = default;

void MixerSendList::configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager, synth::MacroSet& macros,
                              GraphEditor& graphEditor) {
    graph_ = &graph;
    undoManager_ = &undoManager;
    macros_ = &macros;
    graphEditor_ = &graphEditor;
}

void MixerSendList::setEntries(const std::vector<synth::MixerSendEntry>& entries, NodeID stripNodeId) {
    pressedRow_ = -1;
    rowDrag_.discard(); // a lifted row cannot outlive the rows it was lifted from
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

    for (int i = 0; i < (int)entries_.size(); ++i) {
        const auto& entry = entries_[(size_t)i];
        Row row;

        // The M mute toggle. Same button convention as MixerColumnComponent's own strip
        // mute button (MixerColumnComponent.cpp) -- setClickingTogglesState(false) plus a manual
        // setToggleState kept in step by rebuildKnobs() re-running after every mutation, so its
        // paint (and AX toggle role) come from AppLookAndFeel's buttonOnColourId, not a bespoke
        // colour here.
        row.muteButton = std::make_unique<juce::TextButton>("M");
        row.muteButton->setClickingTogglesState(false);
        // JUCE's default text indent is ~5 px a side on an unconnected button, which leaves no room
        // for the "M" at this width (it rendered as a squashed "_"); connected edges halve it.
        row.muteButton->setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight |
                                          juce::Button::ConnectedOnTop | juce::Button::ConnectedOnBottom);
        row.muteButton->setToggleState(entry.muted, juce::dontSendNotification);
        row.muteButton->setTitle(entry.targetNodeId != juce::AudioProcessorGraph::NodeID{}
                                     ? "Mute send to " + entry.targetName
                                     : "Mute send " + juce::String(entry.slot + 1) + " (no target)");
        row.muteButton->setTooltip("Mute or unmute this send");
        const int rowIndex = i;
        row.muteButton->onClick = [this, rowIndex] { toggleMuteForRow(rowIndex); };
        addAndMakeVisible(*row.muteButton);

        // The bypass toggle: an icon button (the same MixerIconButton the insert rows use). Its title carries the
        // on/off state in words, like the M button's, since it is built with setClickingTogglesState(false).
        row.bypassButton = std::make_unique<MixerIconButton>("mixerSendBypass");
        row.bypassButton->setIcon(synth::theme::Icon::ModuleBypass);
        row.bypassButton->setToggleState(entry.bypassed, juce::dontSendNotification);
        row.bypassButton->setTitle(bypassTitle(entry));
        row.bypassButton->tooltipProvider = [this, rowIndex] { return bypassTooltip(rowIndex); };
        row.bypassButton->onClick = [this, rowIndex] { toggleBypassForRow(rowIndex); };
        addAndMakeVisible(*row.bypassButton);

        row.knob = std::make_unique<juce::Slider>();
        row.knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        row.knob->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        // The entry may have no valid target (its cable cut on the canvas), in which case
        // entry.targetName is already the "no target" placeholder -- name the knob by slot instead
        // of repeating that placeholder as if it were a real destination.
        row.knob->setTitle(entry.targetNodeId != juce::AudioProcessorGraph::NodeID{}
                               ? "Send to " + entry.targetName
                               : "Send " + juce::String(entry.slot + 1) + " (no target)");
        row.knob->setTooltip("Send level: drag to change");
        addAndMakeVisible(*row.knob);
        if (auto* param = strip->getSendLevelParameter(entry.slot)) {
            const auto range = param->getNormalisableRange();
            row.knob->setNormalisableRange(juce::NormalisableRange<double>((double)range.start, (double)range.end,
                                                                           (double)range.interval, (double)range.skew,
                                                                           range.symmetricSkew));
            row.attachment = std::make_unique<juce::SliderParameterAttachment>(*param, *row.knob);
            // juce::SliderParameterAttachment's own constructor unconditionally overwrites
            // textFromValueFunction with one built from the param's own getText() (a raw
            // "0.0000000", sendNLevel has no unit label) -- must be reapplied AFTER constructing
            // row.attachment or the ctor's own copy gets silently undone (see
            // MixerColumnComponent.cpp's applyPanAccessibilityText comment for the same fix).
            applyDbAccessibilityText(*row.knob);
            // Lets the owning MixerColumnComponent register this row's knob in its OWN
            // MIDI-learn registry -- this list stays free of a second registry/menu of its own
            // (see MixerColumnMidiLearn.cpp's file comment).
            if (onSendKnobBuilt)
                onSendKnobBuilt(*row.knob, param);
        }

        // The pan knob, same construction shape as the level knob above -- attached
        // straight onto sendNPan, so it is host-visible/automatable with no lane plumbing, and
        // registered in the SAME MIDI-learn registry via the SAME onSendKnobBuilt callback.
        row.panKnob = std::make_unique<juce::Slider>();
        row.panKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        row.panKnob->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        row.panKnob->setTitle((entry.targetNodeId != juce::AudioProcessorGraph::NodeID{}
                                   ? "Send pan to " + entry.targetName
                                   : "Send " + juce::String(entry.slot + 1) + " pan (no target)") +
                              (entry.mono ? " (mono)" : ""));
        row.panKnob->setTooltip("Send pan: drag to change");
        addAndMakeVisible(*row.panKnob);
        if (auto* panParam = strip->getSendPanParameter(entry.slot)) {
            const auto range = panParam->getNormalisableRange();
            row.panKnob->setNormalisableRange(juce::NormalisableRange<double>((double)range.start, (double)range.end,
                                                                              (double)range.interval,
                                                                              (double)range.skew, range.symmetricSkew));
            row.panAttachment = std::make_unique<juce::SliderParameterAttachment>(*panParam, *row.panKnob);
            // Same reapply-after-construction fix as the level knob's applyDbAccessibilityText call
            // above -- the attachment's own ctor overwrites textFromValueFunction unconditionally.
            applyPanAccessibilityText(*row.panKnob);
            if (onSendKnobBuilt)
                onSendKnobBuilt(*row.panKnob, panParam);
        }
        rows_.push_back(std::move(row));
        // A bypassed send reads greyed out: its knobs and M button stay enabled (they still edit the level it
        // comes back at) but paint at the shared dimmed alpha.
        applyBypassLook(rowIndex);
    }
}

juce::String MixerSendList::bypassTooltip(int rowIndex) const {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return {};
    const auto& entry = entries_[(size_t)rowIndex];
    const auto target = entry.targetNodeId != juce::AudioProcessorGraph::NodeID{}
                            ? "send to " + entry.targetName
                            : "send " + juce::String(entry.slot + 1) + " (no target)";
    const auto base = entry.bypassed ? "Turn " + target + " back on" : "Bypass " + target;
    return formatShortcutHint(base, bypassShortcutText ? bypassShortcutText() : juce::String());
}

int MixerSendList::liveUnbindCalls_ = 0;

void MixerSendList::unbindFromGraph() {
    for (auto& row : rows_) {
        if (row.attachment != nullptr) {
            row.attachment.reset();
            ++liveUnbindCalls_;
        }
        if (row.panAttachment != nullptr) {
            row.panAttachment.reset();
            ++liveUnbindCalls_;
        }
    }
    graph_ = nullptr;
    undoManager_ = nullptr;
    macros_ = nullptr;
    graphEditor_ = nullptr;
}

bool MixerSendList::isAttachedForTest(int rowIndex) const {
    return rowIndex >= 0 && rowIndex < (int)rows_.size() && rows_[(size_t)rowIndex].attachment != nullptr;
}

juce::Slider* MixerSendList::getKnobForTest(int rowIndex) const {
    return rowIndex >= 0 && rowIndex < (int)rows_.size() ? rows_[(size_t)rowIndex].knob.get() : nullptr;
}

juce::Slider* MixerSendList::getPanKnobForTest(int rowIndex) const {
    return rowIndex >= 0 && rowIndex < (int)rows_.size() ? rows_[(size_t)rowIndex].panKnob.get() : nullptr;
}

MixerIconButton* MixerSendList::getBypassButtonForTest(int rowIndex) const {
    return rowIndex >= 0 && rowIndex < (int)rows_.size() ? rows_[(size_t)rowIndex].bypassButton.get() : nullptr;
}

juce::Button* MixerSendList::getMuteButtonForTest(int rowIndex) const {
    return rowIndex >= 0 && rowIndex < (int)rows_.size() ? rows_[(size_t)rowIndex].muteButton.get() : nullptr;
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
    return graph_ != nullptr ? synth::sendTargetName(*graph_, macros_, target) : juce::String("No target");
}

juce::String MixerSendList::targetNameFor(const synth::SendTarget& target) const {
    return graph_ != nullptr ? synth::sendTargetName(*graph_, macros_, target) : juce::String("No target");
}

float MixerSendList::rowTop(int rowIndex) const {
    if (rowDrag_.isReordering() && rowIndex >= 0 && rowIndex < (int)entries_.size()) {
        const auto& animator = rowDrag_.animator();
        return rowIndex == animator.getDraggedKey() ? animator.getDraggedStart() : animator.getLayoutStart(rowIndex);
    }
    return (float)(rowIndex * kRowHeight);
}

// The painted parts of a row (name, PRE/POST, x, the mono dot) are drawn at rowTop() here; the
// real child controls are moved to the same place by placeRows(). A lifted row is drawn last, so it
// sits over the rows gliding past it.
void MixerSendList::paintRow(juce::Graphics& g, int rowIndex, float lift) {
    const auto text = synth::theme::themeOf(*this).colors.textPrimary;
    const auto muted = synth::theme::themeOf(*this).colors.textMuted;
    const auto accent = synth::theme::themeOf(*this).colors.accent;
    const auto surface = synth::theme::themeOf(*this).colors.surfaceHi;

    const auto& entry = entries_[(size_t)rowIndex];
    auto row = getLocalBounds().withY((int)std::lround(rowTop(rowIndex))).withHeight(kRowHeight);
    paintReorderLift(g, row.toFloat(), lift, surface, accent);

    auto remove = row.removeFromRight(kRemoveWidth);
    g.setColour(muted);
    g.drawText("x", remove, juce::Justification::centred, false);

    auto toggle = row.removeFromRight(kToggleWidth);
    g.setColour(entry.preFader ? accent : muted);
    g.drawText(entry.preFader ? "Pre" : "Post", toggle, juce::Justification::centred, false);

    row.removeFromRight(kMuteWidth);    // the M button is a real child component -- see placeRows()
    row.removeFromRight(kBypassWidth);  // the bypass button is a real child component -- see placeRows()
    row.removeFromRight(kKnobWidth);    // the level knob is a real child component -- see placeRows()
    row.removeFromRight(kPanKnobWidth); // the pan knob is a real child component -- see placeRows()

    // A small filled dot marks a mono send, painted rather than a new row button (the
    // row has no room for one). A dot, not a letter: an "M" here read as a second mute button
    // beside the real one. Screen readers get "(mono)" on the pan knob's title instead. Only a
    // mono row gives up name width, so a stereo row's name keeps the full budget.
    if (entry.mono) {
        const auto monoTag = row.removeFromLeft(kMonoMarkerWidth).toFloat();
        g.setColour(muted);
        g.fillEllipse(monoTag.withSizeKeepingCentre(4.0f, 4.0f));
    }

    // A bypassed send's name is dimmed like its knobs.
    const float nameAlpha = entry.bypassed ? synth::theme::AppLookAndFeel::kDisabledControlAlpha : 1.0f;
    g.setColour(
        (entry.targetNodeId == juce::AudioProcessorGraph::NodeID{} ? muted : text).withMultipliedAlpha(nameAlpha));
    g.drawText(entry.targetName, row.reduced(2, 0), juce::Justification::centredLeft, true);
}

void MixerSendList::paint(juce::Graphics& g) {
    const auto accent = synth::theme::themeOf(*this).colors.accent;

    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    const int lifted = rowDrag_.isReordering() ? rowDrag_.animator().getDraggedKey() : -1;
    for (int i = 0; i < (int)entries_.size(); ++i)
        if (i != lifted)
            paintRow(g, i, 0.0f);
    if (lifted >= 0 && lifted < (int)entries_.size())
        paintRow(g, lifted, rowDrag_.animator().getLift());

    if (canAddSend()) {
        auto addRow = getLocalBounds().withY((int)entries_.size() * kRowHeight).withHeight(kRowHeight);
        g.setColour(accent);
        g.drawText("+ Send", addRow.reduced(2, 0), juce::Justification::centredLeft, false);
    }
}

void MixerSendList::resized() {
    placeRows();

    // Same anchor paint()'s own addRow uses.
    const bool addVisible = canAddSend();
    addSendProxy_.setVisible(addVisible);
    if (addVisible)
        addSendProxy_.setBounds(getLocalBounds().withY((int)entries_.size() * kRowHeight).withHeight(kRowHeight));
}

// Called on every frame of a drag, so it only moves the controls (a setBounds to the same bounds is
// free) and repaints; a settled list is never asked to.
void MixerSendList::placeRows() {
    const int lifted = rowDrag_.isReordering() ? rowDrag_.animator().getDraggedKey() : -1;
    for (int i = 0; i < (int)rows_.size(); ++i) {
        auto& row = rows_[(size_t)i];
        auto bounds = getLocalBounds().withY((int)std::lround(rowTop(i))).withHeight(kRowHeight);
        bounds.removeFromRight(kRemoveWidth + kToggleWidth);
        if (row.muteButton != nullptr)
            row.muteButton->setBounds(bounds.removeFromRight(kMuteWidth).reduced(1));
        if (row.bypassButton != nullptr)
            row.bypassButton->setBounds(bounds.removeFromRight(kBypassWidth).reduced(1));
        if (row.knob != nullptr)
            row.knob->setBounds(bounds.removeFromRight(kKnobWidth).reduced(1));
        if (row.panKnob != nullptr)
            row.panKnob->setBounds(bounds.removeFromRight(kPanKnobWidth).reduced(1));
        if (i == lifted) {
            for (juce::Component* control :
                 {static_cast<juce::Component*>(row.muteButton.get()),
                  static_cast<juce::Component*>(row.bypassButton.get()), static_cast<juce::Component*>(row.knob.get()),
                  static_cast<juce::Component*>(row.panKnob.get())})
                if (control != nullptr)
                    control->toFront(false);
        }
    }
    repaint();
}

bool MixerSendList::isNameArea(juce::Point<int> position) const {
    if (rowIndexAt(position) < 0)
        return false;
    // The M button, the level knob and the pan knob are real child components between the PRE/POST
    // toggle and the target name, so a press never reaches this list on any of them -- only the
    // shape of the "everything past them is the name" test moves.
    const int fromRight = getWidth() - position.x;
    return fromRight > kRemoveWidth + kToggleWidth + kMuteWidth + kBypassWidth + kKnobWidth + kPanKnobWidth;
}

void MixerSendList::updateHoverCursor(juce::Point<int> position) {
    const bool grab = rowDrag_.animator().isDragging() || isNameArea(position);
    setMouseCursor(grab ? dragGrabCursor() : juce::MouseCursor(juce::MouseCursor::NormalCursor));
}

void MixerSendList::mouseMove(const juce::MouseEvent& event) { updateHoverCursor(event.getPosition()); }

void MixerSendList::mouseExit(const juce::MouseEvent&) {
    if (!rowDrag_.animator().isDragging())
        endDragCursor(*this);
}

// The slots are the rows' static positions. The pointer is converted into this list's own
// coordinates on every event (never getMouseDownPosition(), which JUCE re-derives in the moving
// component's space); the grab offset is captured once, here.
void MixerSendList::beginRowDrag(int rowIndex, const juce::MouseEvent& event) {
    std::vector<ReorderDragAnimator::Slot> slots;
    for (int i = 0; i < (int)entries_.size(); ++i)
        slots.push_back({(float)(i * kRowHeight), (float)kRowHeight});
    const float pointer = event.getEventRelativeTo(this).position.y;
    rowDrag_.begin(slots, rowIndex, pointer - slots[(size_t)rowIndex].start, pointer);
}

void MixerSendList::mouseDown(const juce::MouseEvent& event) {
    pressedRow_ = -1;
    rowDrag_.discard();

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
    else if (isNameArea(event.getPosition())) {
        // A press on the name area could be a plain click (open the target menu, as before) or the
        // start of a reorder drag -- deferred to mouseDrag/mouseUp's threshold check rather than
        // decided here, the same split TimelineTrackHeaderComponent's own row reorder uses.
        pressedRow_ = row;
        beginRowDrag(row, event);
    }
}

void MixerSendList::mouseDrag(const juce::MouseEvent& event) {
    if (pressedRow_ < 0)
        return;
    if (rowDrag_.dragTo(event.getEventRelativeTo(this).position.y)) {
        updateHoverCursor(event.getPosition());
        placeRows();
    }
}

void MixerSendList::mouseUp(const juce::MouseEvent& event) {
    if (pressedRow_ < 0)
        return;
    const int pressed = pressedRow_;
    pressedRow_ = -1;
    switch (rowDrag_.end()) {
    case ReorderDragSession::End::Click:
        showTargetMenu(pressed); // a click that never crossed the threshold -- today's behaviour
        break;
    case ReorderDragSession::End::Commit:
        commitRowDrag(pressed);
        return; // the commit may have rebuilt the mixer, this list included
    case ReorderDragSession::End::Cancelled:
    case ReorderDragSession::End::Nothing:
        break;
    }
    updateHoverCursor(event.getPosition());
}

// The rows glide into the order the drop produces, and the move goes out ONCE, last: moveRow() ends
// in onMutated(), which normally rebuilds the mixer and destroys this list.
void MixerSendList::commitRowDrag(int pressedRow) {
    const auto& animator = rowDrag_.animator();
    const int toRow = animator.getInsertionIndex();
    const auto newOrder = animator.getNewOrder();
    std::vector<float> finalStarts(newOrder.size(), 0.0f);
    for (size_t place = 0; place < newOrder.size(); ++place)
        finalStarts[(size_t)newOrder[place]] = (float)((int)place * kRowHeight);
    const float droppedAt = animator.getDraggedStart();
    rowDrag_.release(finalStarts);
    placeRows();
    if (toRow == pressedRow)
        return;
    if (onSettlePending)
        onSettlePending(toRow, droppedAt);
    moveRow(pressedRow, toRow);
}

// The list a drop rebuilt: rows already sit in the new order, so only the dropped row glides.
void MixerSendList::startSettleFrom(int rowIndex, float fromY) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    std::vector<ReorderDragAnimator::Slot> slots;
    for (int i = 0; i < (int)entries_.size(); ++i)
        slots.push_back({(float)(i * kRowHeight), (float)kRowHeight});
    lastSettle_ = {rowIndex, fromY};
    rowDrag_.settleInto(slots, rowIndex, fromY);
    placeRows();
}

std::vector<NodeID> MixerSendList::availableTargets() const {
    if (graph_ == nullptr)
        return {};
    return synth::enumerateSendTargets(*graph_, stripNodeId_);
}

std::vector<NodeID> MixerSendList::availableKeyTargets() const {
    if (graph_ == nullptr)
        return {};
    return synth::enumerateKeySendTargets(*graph_, stripNodeId_);
}

// Key targets go AFTER the bus/strip targets behind their own separator, so the everyday "send to a
// bus" list reads exactly as before and a Key entry is never mistaken for a channel. Cyclic modules
// (e.g. the Compressor on this strip's own chain) never appear -- enumerateKeySendTargets applies
// the same legality rule addSend does (see docs/mixer/sends-and-buses.md#sending-to-a-key-input).
void MixerSendList::appendKeyTargetItems(juce::PopupMenu& menu,
                                         const std::function<void(synth::SendTarget)>& choose) const {
    const auto keyTargets = availableKeyTargets();
    if (keyTargets.empty())
        return;
    menu.addSeparator();
    for (const auto module : keyTargets) {
        const synth::SendTarget target{module, true};
        menu.addItem(targetNameFor(target), true, false, [choose, target] { choose(target); });
    }
}

void MixerSendList::showTargetMenu(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const bool mono = entries_[(size_t)rowIndex].mono;
    const auto targets = availableTargets();
    juce::PopupMenu menu;
    // A ticked toggle, not a new row button -- the row has no width budget left for one
    // (see this file's own header comment and MixerSendList.h's kMonoMarkerWidth).
    menu.addItem("Mono", true, mono, [this, rowIndex] { toggleMonoForRow(rowIndex); });
    menu.addSeparator();
    menu.addItem("New bus...", createBus != nullptr, false, [this, rowIndex] {
        if (createBus != nullptr)
            if (const auto bus = createBus(); bus != NodeID{})
                retargetRow(rowIndex, bus);
    });
    menu.addSeparator();
    for (const auto target : targets)
        menu.addItem(targetNameFor(target), true, false, [this, rowIndex, target] { retargetRow(rowIndex, target); });
    appendKeyTargetItems(menu, [this, rowIndex](synth::SendTarget target) { retargetRow(rowIndex, target); });

    // A per-item action, not an id+results-callback dispatch, so this is the SAME hookable
    // shape MixerColumnComponent's own MIDI-learn menus use (setShowContextMenuHookForTest) -- a
    // test overrides showMenuHook_ to inspect the built menu (or invoke an item's action directly)
    // rather than the real, headless-incapable juce::PopupMenu::showMenuAsync.
    showMenuHook_(menu);
}

void MixerSendList::showAddMenu() {
    const auto targets = availableTargets();
    juce::PopupMenu menu;
    menu.addItem("New bus...", createBus != nullptr, false, [this] {
        if (createBus != nullptr)
            if (const auto bus = createBus(); bus != NodeID{})
                addSendTo(bus);
    });
    menu.addSeparator();
    for (const auto target : targets)
        menu.addItem(targetNameFor(target), true, false, [this, target] { addSendTo(target); });
    appendKeyTargetItems(menu, [this](synth::SendTarget target) { addSendTo(target); });

    showMenuHook_(menu);
}

void MixerSendList::mutateAndNotify(const std::function<bool()>& mutation) {
    if (graph_ == nullptr || undoManager_ == nullptr || macros_ == nullptr || graphEditor_ == nullptr)
        return;
    bool changed = false;
    // A send into (or out of) a strip that sits inside a macro enters through a macro port, like a
    // hand-drawn cable, and removing it takes the port it no longer needs -- all in this one step.
    undoManager_->recordGraphAndMacroChange(*graph_, *macros_, [&] {
        changed = graphEditor_->getMacroController().applyProgrammaticConnectionChange(
            graphEditor_->getAutoCreateMacroPortsOnDragEnabled(), mutation);
        graphEditor_->updateComponents();
    });
    if (changed && onMutated)
        onMutated();
}

void MixerSendList::addSendTo(NodeID target) { addSendTo(synth::SendTarget{target, false}); }

void MixerSendList::addSendTo(const synth::SendTarget& target) {
    mutateAndNotify([&] { return graph_ != nullptr && synth::addSend(*graph_, stripNodeId_, target) >= 0; });
}

void MixerSendList::removeRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const int slot = entries_[(size_t)rowIndex].slot;
    mutateAndNotify([&] { return synth::removeSend(*graph_, stripNodeId_, slot); });
}

void MixerSendList::retargetRow(int rowIndex, NodeID target) {
    retargetRow(rowIndex, synth::SendTarget{target, false});
}

void MixerSendList::retargetRow(int rowIndex, const synth::SendTarget& target) {
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

void MixerSendList::toggleMuteForRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const auto& entry = entries_[(size_t)rowIndex];
    const int slot = entry.slot;
    const bool newMuted = !entry.muted;
    mutateAndNotify([&] { return graph_ != nullptr && synth::setSendMuted(*graph_, stripNodeId_, slot, newMuted); });
}

// The send's bypass is its own persisted bit, never its level, so the row comes back at the level it left at.
void MixerSendList::toggleBypassForRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const auto& entry = entries_[(size_t)rowIndex];
    const int slot = entry.slot;
    const bool newBypassed = !entry.bypassed;
    mutateAndNotify([&] {
        if (graph_ == nullptr || !synth::setSendBypassed(*graph_, stripNodeId_, slot, newBypassed))
            return false;
        // The row reads right even where nothing rebuilds the list afterwards (a bare list in a test).
        entries_[(size_t)rowIndex].bypassed = newBypassed;
        applyBypassLook(rowIndex);
        return true;
    });
}

void MixerSendList::applyBypassLook(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)rows_.size() || rowIndex >= (int)entries_.size())
        return;
    const auto& entry = entries_[(size_t)rowIndex];
    auto& row = rows_[(size_t)rowIndex];
    row.bypassButton->setToggleState(entry.bypassed, juce::dontSendNotification);
    row.bypassButton->setTitle(bypassTitle(entry));
    for (juce::Component* control :
         {static_cast<juce::Component*>(row.muteButton.get()), static_cast<juce::Component*>(row.knob.get()),
          static_cast<juce::Component*>(row.panKnob.get())})
        control->getProperties().set(synth::theme::AppLookAndFeel::kDimmedProperty, entry.bypassed);
    repaint();
}

juce::String MixerSendList::bypassTitle(const synth::MixerSendEntry& entry) {
    const auto target = entry.targetNodeId != juce::AudioProcessorGraph::NodeID{}
                            ? "Bypass send to " + entry.targetName
                            : "Bypass send " + juce::String(entry.slot + 1) + " (no target)";
    return target + ", " + (entry.bypassed ? "on" : "off");
}

void MixerSendList::moveRow(int fromRow, int toRow) {
    if (moveSendRow == nullptr)
        return;
    // The whole drag is ONE undo step owned by MixerPanelComponent (graph + timeline + macro
    // together) -- this list only reports whether it happened, same as every other row mutation's
    // onMutated() call below.
    if (moveSendRow(stripNodeId_, fromRow, toRow) && onMutated)
        onMutated();
}

void MixerSendList::toggleMonoForRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)entries_.size())
        return;
    const auto& entry = entries_[(size_t)rowIndex];
    const int slot = entry.slot;
    const bool newMono = !entry.mono;
    mutateAndNotify([&] { return graph_ != nullptr && synth::setSendMono(*graph_, stripNodeId_, slot, newMono); });
}

} // namespace synth::ui
