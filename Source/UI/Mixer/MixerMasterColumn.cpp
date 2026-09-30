// Concern: MixerMasterColumn's param binding and layout. The right-click MIDI
// Learn on the fader lives here too (not a separate MixerMasterColumnMidiLearn.cpp unit): Master
// has exactly one learnable control, so the whole feature is a handful of lines -- see
// MixerColumnMidiLearn.cpp's own file comment for the design this mirrors.
#include "MixerMasterColumn.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/PeakMeterLatch.h"
#include "Modules/MasterModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace synth::ui {

namespace {
juce::AudioParameterFloat* findFloatParam(juce::AudioProcessor& processor, const juce::String& paramId) {
    for (auto* param : processor.getParameters())
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param);
            floatParam != nullptr && floatParam->paramID == paramId)
            return floatParam;
    return nullptr;
}
} // namespace

void MixerMasterColumn::setHeaderContextMenu(std::function<void(const juce::MouseEvent&)> callback) {
    header_.onContextMenu = std::move(callback);
}

MixerMasterColumn::MixerMasterColumn() {
    // Without this, Master's own group AccessibilityHandler (the default, unspecified-role
    // one Component provides) reads an empty title -- MixerDirectColumn's ctor sets the same "own
    // component" title for the identical reason (that class's own comment on setTitle("Direct")).
    setTitle("Master");
    addAndMakeVisible(header_);
    header_.setDisplayName("Master");
    header_.setRenameEnabled(false); // Master is a MasterModule, not a ChannelStripModule -- no strip name field
    insertViewport_.setList(insertList_);
    addAndMakeVisible(insertViewport_);
    for (size_t i = 0; i < dividers_.size(); ++i)
        addAndMakeVisible(dividers_[i]);
    addChildComponent(insertsCollapsed_);
    ownSectionLayout_.onGeometryChanged = [this] { resized(); };
    ownSectionLayout_.onAppearanceChanged = [this] { repaintSectionDividers(); };
    setSectionLayout(ownSectionLayout_);
    insertList_.onEditOnCanvas = [this](const juce::String& uuid) {
        if (onEditOnCanvas)
            onEditOnCanvas(uuid);
    };
    insertList_.onMutated = [this] {
        if (onMutated)
            onMutated();
    };
    addAndMakeVisible(fader_);
    fader_.setChannelName("Master");
    addAndMakeVisible(meter_);
    meter_.setTitle("Master meter");
    addAndMakeVisible(meterReadout_);
    meterReadout_.onResetAllRequested = [this] {
        if (onResetAllMetersRequested)
            onResetAllMetersRequested();
    };
    addAndMakeVisible(muteButton_);
    muteButton_.setClickingTogglesState(false);
    // MixerPanelComponent is the single focusable leaf -- see
    // MixerColumnComponent.cpp's ctor comment for why every child control does this.
    muteButton_.setWantsKeyboardFocus(false);
    // The mixer has no other project-settings surface, so the project's pan law lives here
    // -- see the class comment and docs/mixer/mixer.md#pan-law.
    addAndMakeVisible(panLawButton_);
    panLawButton_.setClickingTogglesState(false);
    panLawButton_.setWantsKeyboardFocus(false);
    panLawButton_.onClick = [this] { showPanLawMenu(); };
    // Added once here (the fader is a persistent member, never recreated) rather than per
    // setNodeId() -- mirrors MixerColumnComponent's own "register the listener once, rebuild the
    // param binding on every rebind" split.
    fader_.getSlider().addMouseListener(this, false);
}

void MixerMasterColumn::configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager,
                                  synth::MacroSet& macros, GraphEditor& graphEditor, AudioEngine& audioEngine,
                                  synth::MeterReader meterReader) {
    graph_ = &graph;
    undoManager_ = &undoManager;
    graphEditor_ = &graphEditor;
    audioEngine_ = &audioEngine;
    meterReader_ = meterReader;
    insertList_.configure(graph, undoManager, macros, graphEditor);
    refreshPanLawButton();
}

void MixerMasterColumn::setColumn(const synth::MixerColumn& column) {
    setNodeId(column.nodeId);
    // Master's chain hangs off Master's own output (column.sourceNodeId == column.nodeId) and feeds the Rec Tap /
    // Audio Output (column.chainEndNodeId) -- see MixerModelInserts.cpp's buildMasterInsertsForColumn.
    insertList_.setEntries(column.inserts, column.insertChainIsLinear, column.editOnCanvasTargetUuid,
                           column.sourceNodeId, column.chainEndNodeId);
    const bool hasInserts = !column.inserts.empty();
    insertsCollapsed_.setSummary(mixerSectionCountSummary((int)column.inserts.size(), "insert", "inserts"));
    if (hasInserts != hasInserts_) {
        hasInserts_ = hasInserts;
        // Both sources are consume-on-read latches that keep the loudest peak since their last read, and only the
        // active one is ever read -- so the one we just switched TO holds everything since the last time it was
        // active, which would flash as a bogus clip. Read it once and drop the value.
        for (int leg = 0; leg < 2; ++leg)
            takeMeterPeak(leg);
    }
    // A rebuild is the ONE thing every path that changes the engine's pan law directly --
    // New Patch's factory-default Compensated write (MainComponent::clearTimelineForNewPatch's
    // caller), Load, undo/redo -- reaches unconditionally, so re-reading it here is what keeps this
    // label from going stale indefinitely rather than only on the next menu pick. Without this, a fresh mirror open()
    // (which DOES call refreshPanLawButton() from configure()) could show the CORRECT current law while an already-open
    // dock, rebuilt since its own last configure()/menu-pick, still showed a stale one.
    refreshPanLawButton();
    resized();
}

float MixerMasterColumn::takeMeterPeak(int leg) {
    // With inserts the meter shows what LEAVES the chain, like every DAW's master meter; with none it is
    // MasterModule's own pre-insert latch (see docs/mixer/meters.md).
    if (hasInserts_ && outputPeakProvider)
        return outputPeakProvider(leg);
    if (graph_ == nullptr)
        return 0.0f;
    auto* node = graph_->getNodeForId(nodeId_);
    if (auto* master = dynamic_cast<MasterModule*>(node != nullptr ? node->getProcessor() : nullptr))
        return master->takeMeterPeak(meterReader_, leg);
    return 0.0f;
}

void MixerMasterColumn::setNodeId(juce::AudioProcessorGraph::NodeID nodeId) {
    nodeId_ = nodeId;
    fader_.unbind();
    midiLearnableFaderParam_ = nullptr;
    if (graph_ == nullptr || undoManager_ == nullptr)
        return;
    auto* node = graph_->getNodeForId(nodeId_);
    auto* processor = node != nullptr ? node->getProcessor() : nullptr;
    if (processor == nullptr)
        return;
    if (auto* gainParam = findFloatParam(*processor, "gain")) {
        fader_.bind(*graph_, *undoManager_, *gainParam);
        midiLearnableFaderParam_ = gainParam;
    }

    muteButton_.onClick = [this] { toggleMuted(); };
    refreshMuteAccessibility();
}

void MixerMasterColumn::refreshMuteAccessibility() {
    auto* n = graph_ != nullptr ? graph_->getNodeForId(nodeId_) : nullptr;
    auto* m = n != nullptr ? dynamic_cast<ModuleBase*>(n->getProcessor()) : nullptr;
    const bool muted = m != nullptr && m->isMuted();
    muteButton_.setToggleState(muted, juce::dontSendNotification);
    muteButton_.setTitle(juce::String("Master mute, ") + (muted ? "on" : "off"));
}

void MixerMasterColumn::toggleMuted() {
    if (graph_ == nullptr || undoManager_ == nullptr)
        return;
    auto* n = graph_->getNodeForId(nodeId_);
    auto* m = n != nullptr ? dynamic_cast<ModuleBase*>(n->getProcessor()) : nullptr;
    if (m == nullptr)
        return;
    undoManager_->captureBeforeState(*graph_);
    m->setMuted(!m->isMuted());
    undoManager_->pushSnapshotFromCapture(*graph_);
    refreshMuteAccessibility();
    repaint();
    if (onLiveStateChanged)
        onLiveStateChanged();
}

// Labels the button by the engine's CURRENT law -- called once from configure(), again after every
// menu pick (setPanLaw(), below), and from refreshLiveVisuals() when a sibling live view (the mixer
// mirror, docs/mixer/panel.md) reports its own pan-law change via onLiveStateChanged. An undo/redo
// of the setting still only reaches this button the next time refreshMeter()'s tick or a rebuild
// calls it -- see MixerPanLawAction in AppUndoManager.cpp, which has no restore-notification hook
// at all (see docs/mixer/mixer.md#pan-law).
void MixerMasterColumn::refreshPanLawButton() {
    if (audioEngine_ == nullptr)
        return;
    const bool compensated = audioEngine_->getMixerPanLaw() == synth::MixerPanLaw::Compensated;
    panLawButton_.setButtonText(compensated ? "Pan: Comp." : "Pan: Bal.");
    panLawButton_.setTitle(juce::String("Mixer pan law (this project): ") + (compensated ? "Compensated" : "Balance"));
}

void MixerMasterColumn::showPanLawMenu() {
    if (audioEngine_ == nullptr || undoManager_ == nullptr)
        return;
    const auto current = audioEngine_->getMixerPanLaw();

    juce::PopupMenu menu;
    menu.addItem(-1, "Mixer pan law (this project)", false, false); // disabled title row
    juce::Component::SafePointer<MixerMasterColumn> safeThis(this);
    // Per-item callbacks, same idiom as appendMidiLearnMenuItems -- the menu drives the pick, not a
    // showMenuAsync completion id, so the test seam below can drive an item's own callback directly
    // with no real async popup loop.
    menu.addItem("Balance (legacy)", true, current == synth::MixerPanLaw::Balance, [safeThis] {
        if (safeThis != nullptr)
            safeThis->setPanLaw(synth::MixerPanLaw::Balance);
    });
    menu.addItem("Compensated (constant-power, +3 dB at the extremes)", true,
                 current == synth::MixerPanLaw::Compensated, [safeThis] {
                     if (safeThis != nullptr)
                         safeThis->setPanLaw(synth::MixerPanLaw::Compensated);
                 });

    showPanLawMenuHook_(menu);
}

void MixerMasterColumn::setPanLaw(synth::MixerPanLaw law) {
    if (audioEngine_ == nullptr || undoManager_ == nullptr)
        return;
    const auto before = audioEngine_->getMixerPanLaw();
    if (before == law)
        return;
    audioEngine_->setMixerPanLaw(law);
    // recordMixerPanLawChange is what actually dirties the document (the edit serial is the
    // project's one dirty-state funnel, docs/architecture/project-bundle.md#dirty-state-and-the-unsaved-changes-guard)
    // and makes the change undoable, same as every other mixer control.
    undoManager_->recordMixerPanLawChange([this](synth::MixerPanLaw l) { audioEngine_->setMixerPanLaw(l); }, before,
                                          law);
    refreshPanLawButton();
    if (onLiveStateChanged)
        onLiveStateChanged();
}

// The cheap per-strip refresh a sibling live view's onLiveStateChanged drives this instance
// with -- mute + pan-law only, no rebuild; see MixerPanelComponent::refreshLiveMixerVisuals().
void MixerMasterColumn::refreshLiveVisuals() {
    refreshMuteAccessibility();
    refreshPanLawButton();
}

void MixerMasterColumn::setKeyboardFocused(bool focused) {
    if (keyboardFocused_ == focused)
        return;
    keyboardFocused_ = focused;
    repaint();
}

void MixerMasterColumn::unbindFromGraph() {
    fader_.unbind();
    // Same use-after-free-on-undo hazard as fader_.unbind() above -- see MidiLearnMenu.h.
    midiLearnableFaderParam_ = nullptr;
    muteButton_.onClick = nullptr;
    meter_.peakProvider = nullptr;
}

void MixerMasterColumn::refreshMeter(float elapsedSeconds) {
    meter_.peakProvider = [this](int leg) { return takeMeterPeak(leg); };
    meter_.refresh(elapsedSeconds);
    meterReadout_.updatePeak(std::max(meter_.getDisplayedDbForTest(0), meter_.getDisplayedDbForTest(1)));
    refreshMidiLearnBadges();
    // Same tick also keeps the armed breathing outline animating -- see
    // repaintArmedMidiLearnOutline()'s own comment.
    repaintArmedMidiLearnOutline();
}

// ============================================================================
// MIDI Learn
// ============================================================================

void MixerMasterColumn::mouseDown(const juce::MouseEvent& e) {
    if (e.eventComponent != &fader_.getSlider() || !e.mods.isPopupMenu())
        return;
    if (midiLearnableFaderParam_ == nullptr || graphEditor_ == nullptr || !graphEditor_->onMidiLearnRequested)
        return;

    juce::String label;
    if (graphEditor_->onQueryMidiMappingsForNode) {
        const auto mappings = graphEditor_->onQueryMidiMappingsForNode(nodeId_);
        const auto found = mappings.find(midiLearnableFaderParam_->paramID);
        if (found != mappings.end())
            label = found->second;
    }

    juce::Component::SafePointer<MixerMasterColumn> safeThis(this);
    const juce::String paramId = midiLearnableFaderParam_->paramID;
    GraphEditor* graphEditor = graphEditor_;

    synth::ui::midilearn::MenuContent content;
    content.targetName = midiLearnableFaderParam_->getName(100);
    content.mappingLabel = label;
    content.learn = [safeThis, graphEditor, paramId] {
        if (safeThis != nullptr && graphEditor->onMidiLearnRequested)
            graphEditor->onMidiLearnRequested(safeThis->nodeId_, paramId);
    };
    content.forget = [safeThis, graphEditor, paramId] {
        if (safeThis != nullptr && graphEditor->onMidiForgetRequested)
            graphEditor->onMidiForgetRequested(safeThis->nodeId_, paramId);
    };
    if (graphEditor_->onEditMidiAssignmentRequested) {
        content.editAssignment = [safeThis, graphEditor, paramId] {
            if (safeThis != nullptr && graphEditor->onEditMidiAssignmentRequested)
                graphEditor->onEditMidiAssignmentRequested(safeThis->nodeId_, paramId);
        };
    }

    juce::PopupMenu menu;
    synth::ui::midilearn::appendMidiLearnMenuItems(menu, content);
    if (menu.getNumItems() == 0)
        return;
    showContextMenuHook_(menu);
}

juce::RangedAudioParameter* MixerMasterColumn::findMidiLearnableParamForTest(const juce::Component* component) const {
    return component == &fader_.getSlider() ? midiLearnableFaderParam_ : nullptr;
}

void MixerMasterColumn::collectPickCandidates(std::vector<PickCandidate>& out) const {
    if (midiLearnableFaderParam_ != nullptr)
        out.push_back({static_cast<juce::Component*>(const_cast<juce::Slider*>(&fader_.getSlider())),
                       synth::midi::PickTarget::parameter(nodeId_, midiLearnableFaderParam_->paramID)});
}

bool MixerMasterColumn::isMidiLearnBadgeMappedForTest(const juce::Component* component) const {
    return component == &fader_.getSlider() && midiLearnBadgeMapped_;
}

void MixerMasterColumn::setMidiLearnArmedParam(const juce::String& paramId) {
    if (midiLearnArmedParamId_ == paramId)
        return;
    midiLearnArmedParamId_ = paramId;
    midiLearnArmedSinceMs_ = juce::Time::getMillisecondCounterHiRes();
    repaint(fader_.getSlider().getBounds().expanded(2));
}

void MixerMasterColumn::refreshMidiLearnBadges() {
    if (graphEditor_ == nullptr || !graphEditor_->onQueryMidiMappingsForNode || midiLearnableFaderParam_ == nullptr)
        return;
    const auto mappings = graphEditor_->onQueryMidiMappingsForNode(nodeId_);
    const bool mapped = mappings.find(midiLearnableFaderParam_->paramID) != mappings.end();
    if (mapped == midiLearnBadgeMapped_)
        return;
    midiLearnBadgeMapped_ = mapped;
    repaint();
}

// Called from refreshMeter()'s existing 10 Hz tick -- see
// MixerColumnComponent::repaintArmedMidiLearnOutline's own comment for why this needs to exist at
// all (paintMidiLearnArmedOutline() recomputes alpha from wall time on every paint(), so nothing
// visibly breathes unless something keeps asking for a repaint while armed).
void MixerMasterColumn::repaintArmedMidiLearnOutline() {
    if (midiLearnArmedParamId_.isEmpty() || midiLearnableFaderParam_ == nullptr ||
        midiLearnableFaderParam_->paramID != midiLearnArmedParamId_)
        return;
    repaint(getLocalArea(&fader_.getSlider(), fader_.getSlider().getLocalBounds()).expanded(2));
    ++midiLearnArmedRepaintCount_;
}

void MixerMasterColumn::paintMidiLearnOverlays(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    // The slider is nested inside fader_ (a MixerFader, itself a direct child of this
    // column), so its bounds must be walked up TWO levels into this column's own coordinate frame
    // -- getLocalArea(&slider, slider's own local bounds) does that regardless of depth; the
    // pre-fix code passed the slider's PARENT as the source but the slider's own local bounds
    // (0,0,w,h) as the area, mixing two different coordinate frames and landing the badge/outline
    // at this column's own top-left corner instead of on the fader (see MixerColumnMidiLearn.cpp's
    // matching fix for the same mistake).
    const auto bounds = getLocalArea(&fader_.getSlider(), fader_.getSlider().getLocalBounds());

    if (midiLearnBadgeMapped_) {
        const juce::Colour badgeColour = lf != nullptr ? lf->getTheme().colors.midiMapped : juce::Colour(0xffB48EF5);
        synth::ui::midilearn::paintMidiMappedBadge(g, bounds, badgeColour);
    }

    if (midiLearnArmedParamId_.isEmpty() || midiLearnableFaderParam_ == nullptr ||
        midiLearnableFaderParam_->paramID != midiLearnArmedParamId_)
        return;
    const juce::Colour armedColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    synth::ui::midilearn::paintMidiLearnArmedOutline(g, bounds, armedColour, midiLearnArmedSinceMs_);
}

void MixerMasterColumn::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto bg = laf != nullptr ? laf->getTheme().colors.surfaceHi : juce::Colour(0xff232833);
    const auto border = laf != nullptr ? laf->getTheme().colors.border : juce::Colour(0xff2A2F38);
    g.setColour(bg);
    g.fillRect(getLocalBounds());
    g.setColour(border);
    g.drawRect(getLocalBounds(), 1);
}

void MixerMasterColumn::paintOverChildren(juce::Graphics& g) {
    paintMidiLearnOverlays(g);

    if (!keyboardFocused_)
        return;
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    g.setColour(accent.withAlpha(0.85f));
    g.drawRect(getLocalBounds(), 2);
}

// Master lays out against the SAME shared MixerSectionLayout geometry as every strip column, so its
// Inserts row and its fader line up with theirs: the source-line slot under the header stays empty,
// and so do the Sends, EQ and pan rows Master has no content for.
void MixerMasterColumn::resized() {
    // Same meter width as MixerColumnComponent -- the Master column stays visually consistent with
    // every strip column's meter (docs/mixer/mixer.md meters section).
    constexpr int kMeterWidth = 32;

    const auto geometry = sectionLayout_->resolve(getHeight());
    const auto inner = getLocalBounds().reduced(MixerSectionLayout::kColumnInset);
    header_.setBounds(inner.withHeight(MixerSectionLayout::kHeaderHeight));
    auto row = [&inner](int y, int height) { return juce::Rectangle<int>(inner.getX(), y, inner.getWidth(), height); };

    const size_t inserts = (size_t)MixerSection::Inserts;
    const bool insertsHidden = sectionLayout_->isHidden(MixerSection::Inserts);
    const auto insertsArea = row(geometry.sectionTop[inserts], geometry.sectionHeight[inserts]);
    insertViewport_.setVisible(!insertsHidden);
    insertViewport_.setBounds(insertsArea);
    insertViewport_.setContentHeight(insertList_.getPreferredHeight());
    insertsCollapsed_.setVisible(insertsHidden);
    insertsCollapsed_.setBounds(insertsArea);
    for (size_t i = 0; i < dividers_.size(); ++i)
        dividers_[i].setBounds(row(geometry.dividerTop[i], MixerSectionLayout::kDividerHeight));

    // Shares the mute row -- mute on the left, the pan-law control on the right.
    auto bottomRow = row(geometry.msTop, MixerSectionLayout::kMsRowHeight).reduced(2);
    muteButton_.setBounds(bottomRow.removeFromLeft(bottomRow.getWidth() / 3));
    panLawButton_.setBounds(bottomRow);
    meterReadout_.setBounds(row(geometry.readoutTop, MixerSectionLayout::kMeterReadoutHeight));
    auto faderRow = row(geometry.faderTop, geometry.faderHeight);
    meter_.setBounds(faderRow.removeFromRight(kMeterWidth));
    faderRow.removeFromRight(2);
    fader_.setBounds(faderRow);
}

void MixerMasterColumn::setSectionLayout(MixerSectionLayout& layout) {
    sectionLayout_ = &layout;
    for (size_t i = 0; i < dividers_.size(); ++i)
        dividers_[i].setLayout(sectionLayout_, (MixerSection)(int)i);
    insertsCollapsed_.setLayout(sectionLayout_, MixerSection::Inserts);
    resized();
}

void MixerMasterColumn::repaintSectionDividers() {
    for (auto& divider : dividers_)
        divider.repaint();
}

} // namespace synth::ui
