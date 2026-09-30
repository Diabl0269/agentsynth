// Concern: MixerColumnComponent's layout, param binding (pan/mute/solo -- fader
// binding lives in MixerFader itself) and the column-click-selects-macro gesture.
#include "MixerColumnComponent.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/ChannelMacroLookup.h"
#include "Mixer/PeakMeterLatch.h"
#include "MixerPanAccessibilityText.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// Widened from the pre-meters-rework 10 px so two dB-scale bars + tick labels are legible
// (docs/mixer/mixer.md meters section) -- see MixerMeterScale.h's kBarsAreaWidth/kLabelMinWidth for the
// exact pixel budget this must cover. Kept as narrow as that budget allows so the column itself
// doesn't grow wider than necessary.
constexpr int kMeterWidth = 32;

// applyPanAccessibilityText moved to MixerPanAccessibilityText.h so MixerSendList's
// own per-send pan knobs can share the exact same "Center"/"50% left"/"50% right" phrasing instead
// of a second copy -- see that header's own comment for why every caller must reapply it AFTER
// constructing its SliderParameterAttachment.

juce::AudioParameterFloat* findFloatParam(juce::AudioProcessor& processor, const juce::String& paramId) {
    for (auto* param : processor.getParameters())
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param);
            floatParam != nullptr && floatParam->paramID == paramId)
            return floatParam;
    return nullptr;
}

// ModuleBase's own mutedParam (paramID "muted", Source/Modules/ModuleBase.h) is private
// with no accessor of its own -- mirrors findFloatParam's identity-scan idiom rather than adding
// one, since every other param lookup in this file already goes through getParameters() this way.
juce::AudioParameterBool* findBoolParam(juce::AudioProcessor& processor, const juce::String& paramId) {
    for (auto* param : processor.getParameters())
        if (auto* boolParam = dynamic_cast<juce::AudioParameterBool*>(param);
            boolParam != nullptr && boolParam->paramID == paramId)
            return boolParam;
    return nullptr;
}

} // namespace

MixerColumnComponent::MixerColumnComponent() {
    addAndMakeVisible(header_);
    header_.onHeaderClicked = [this] {
        if (onColumnClicked)
            onColumnClicked();
    };
    header_.onNameEdited = [this](const juce::String& newName) { commitHeaderRename(newName); };
    addAndMakeVisible(sourceLineLabel_);
    header_.adoptHandleLabel(sourceLineLabel_); // the source line is part of the drag handle
    sourceLineLabel_.setFont(juce::Font(juce::FontOptions(10.0f)));
    sourceLineLabel_.setJustificationType(juce::Justification::centredLeft);
    sourceLineLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8A93A0));

    insertViewport_.setList(insertList_);
    addAndMakeVisible(insertViewport_);
    insertList_.onEditOnCanvas = [this](const juce::String& uuid) {
        if (onEditOnCanvas)
            onEditOnCanvas(uuid);
    };
    insertList_.onMutated = [this] {
        if (onMutated)
            onMutated();
    };
    // A live "Remove" from the row menu frees the removed node's processor
    // synchronously (see MixerInsertList::onBeforeNodeRemoved's own comment), before the eventual
    // MixerPanelComponent::rebuild() this column's own onMutated triggers gets a chance to destroy
    // eqThumbnail_ -- so unbind it here, synchronously, whenever the node being removed is the one
    // it's currently bound to. A no-op for every other row.
    insertList_.onBeforeNodeRemoved = [this](juce::AudioProcessorGraph::NodeID nodeId) {
        if (nodeId == eqNodeId_)
            eqThumbnail_.setEqModule(nullptr);
    };

    addChildComponent(eqThumbnail_); // hidden by default -- MixerEqThumbnail::setVisible(false)
    eqThumbnail_.onClicked = [this] {
        if (onEditOnCanvas)
            onEditOnCanvas(eqNodeUuid_);
    };

    sendViewport_.setList(sendList_);
    addAndMakeVisible(sendViewport_);
    sendList_.onMutated = [this] {
        if (onMutated)
            onMutated();
    };

    for (size_t i = 0; i < dividers_.size(); ++i) {
        addAndMakeVisible(dividers_[i]);
        addChildComponent(collapsed_[i]);
    }
    // A standalone column (no panel) still reacts to its own dividers and strips.
    ownSectionLayout_.onGeometryChanged = [this] { resized(); };
    ownSectionLayout_.onAppearanceChanged = [this] { repaintSectionDividers(); };
    setSectionLayout(ownSectionLayout_);

    panSlider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    // MixerPanelComponent is the single focusable leaf -- every child control gives up
    // keyboard focus so Up/Down/Enter/M/S/R always reach the panel's own keyPressed() (the keyboard-focus
    // trap docs/control/shortcuts.md documents: a focused Slider eats Up/Down, a focused TextButton eats
    // Return/Space).
    panSlider_.setWantsKeyboardFocus(false);
    applyPanAccessibilityText(panSlider_);
    addAndMakeVisible(panSlider_);

    addAndMakeVisible(fader_);
    addAndMakeVisible(meter_);
    addAndMakeVisible(meterReadout_);
    meterReadout_.onResetAllRequested = [this] {
        if (onResetAllMetersRequested)
            onResetAllMetersRequested();
    };

    addAndMakeVisible(muteButton_);
    muteButton_.setClickingTogglesState(false);
    muteButton_.setWantsKeyboardFocus(false);
    addAndMakeVisible(soloButton_);
    soloButton_.setClickingTogglesState(false);
    soloButton_.setWantsKeyboardFocus(false);
}

void MixerColumnComponent::configure(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager,
                                     synth::MacroSet& macros, GraphEditor& graphEditor, AudioEngine& audioEngine,
                                     synth::MeterReader meterReader) {
    graphEditor_ = &graphEditor;
    graph_ = &graph;
    undoManager_ = &undoManager;
    audioEngine_ = &audioEngine;
    macros_ = &macros;
    meterReader_ = meterReader;
    insertList_.configure(graph, undoManager, macros, graphEditor);
    sendList_.configure(graph, undoManager, macros, graphEditor);
    // Fires once per send row rebuilt inside sendList_ (MixerSendList::rebuildKnobs(),
    // itself triggered by setEntries() from setColumn() below) so this column's ONE MIDI-learn
    // registry covers send levels too, without MixerSendList needing a menu/registry of its own --
    // see MixerColumnMidiLearn.cpp.
    sendList_.onSendKnobBuilt = [this](juce::Slider& knob, juce::RangedAudioParameter* param) {
        registerMidiLearnable(knob, param);
    };
}

void MixerColumnComponent::setColumn(const synth::MixerColumn& column, const juce::String& sourceLine) {
    nodeId_ = column.nodeId;
    uuid_ = column.uuid;
    sourceLine_ = sourceLine;

    header_.setColour(column.colour);
    header_.setDisplayName(column.name);
    // A linked column has exactly one feeding track, so `sourceLine` is that track's name.
    header_.setLinkedTrack(column.linkedToTrack, column.linkedToTrack ? sourceLine : juce::String());
    header_.setBusBadgeVisible(column.kind == synth::MixerColumn::Kind::Bus);
    juce::StringArray receivesFrom;
    for (const auto& name : column.receivesFrom)
        receivesFrom.add(name);
    header_.setReceivesFrom(receivesFrom);

    // Doubles as docs/mixer/panel.md#what-the-mixer-shows's "the tracks that play into it" row -- `sourceLine` is the
    // caller-resolved (comma-joined) names of column.feedingTracks, the same tracks a "source line" names for a
    // single-source column.
    sourceLineLabel_.setText(sourceLine_, juce::dontSendNotification);
    insertList_.setEntries(column.inserts, column.insertChainIsLinear, column.editOnCanvasTargetUuid,
                           column.sourceNodeId, column.nodeId);

    // The first Parametric EQ in signal order gets the column's curve thumbnail --
    // Cubase's own single-slot idiom. A second EQ further down the chain stays reachable through
    // the insert list itself; this is a deliberate scope trim, not an oversight.
    ParametricEQModule* firstEq = nullptr;
    eqNodeUuid_.clear();
    eqNodeId_ = {};
    if (graph_ != nullptr) {
        for (const auto& entry : column.inserts) {
            auto* node = graph_->getNodeForId(entry.nodeId);
            auto* processor = node != nullptr ? node->getProcessor() : nullptr;
            if (auto* eq = dynamic_cast<ParametricEQModule*>(processor)) {
                firstEq = eq;
                eqNodeUuid_ = entry.uuid;
                eqNodeId_ = entry.nodeId;
                break;
            }
        }
    }
    eqThumbnail_.setEqModule(firstEq, graph_, eqNodeId_);
    refreshCollapsedSummaries((int)column.inserts.size(), (int)column.sends.size(), firstEq != nullptr);

    // Accessible names -- "<name> fader"/"pan", e.g. "Lead 1 fader, -3.0 dB" (JUCE speaks
    // the minus sign as "minus"). setTitle() on `this` is what createAccessibilityHandler()'s
    // group role announces for the column itself.
    setTitle(column.name);
    fader_.setChannelName(column.name);
    panSlider_.setTitle(column.name + " pan");
    meter_.setTitle(column.name + " meter");
    meterReadout_.setTitle(column.name + " peak");
    eqThumbnail_.setTitle(column.name + " EQ curve");
    eqThumbnail_.setDescription("Selects this channel's EQ on the canvas");

    rebindControls();
    // Must run AFTER rebindControls() -- it clears midiLearnableEntries_ and would wipe out
    // send-row entries registered by sendList_.onSendKnobBuilt (fired from rebuildKnobs() inside
    // setEntries()) if this ran first.
    sendList_.setEntries(column.sends, column.nodeId);
    resized();
}

// header_'s inline rename commits here. A boxed strip's column name already comes from its MACRO
// (stripColumnName's own priority, Source/Mixer/MixerModel/MixerModelSends.cpp) -- reusing the
// macro's own rename (MacroGroupController::renameMacro, the same path a macro card's own rename
// uses, and which itself keeps a linked track's name in the SAME undo step) is what keeps this to
// ONE name per column instead of a second, silently-losing one on ChannelStripModule that
// stripColumnName would just shadow. Only once the strip has no macro of its own does the strip's
// own persisted name (ChannelStripModule::setStripName) become the thing being edited at all
// (see docs/mixer/panel.md).
void MixerColumnComponent::commitHeaderRename(const juce::String& rawNewName) {
    const juce::String newName = rawNewName.trim();
    if (graph_ == nullptr || undoManager_ == nullptr) {
        header_.restoreDisplayName();
        return;
    }

    if (const auto* macro = macros_ != nullptr ? synth::nearestChannelMacro(*graph_, *macros_, uuid_) : nullptr) {
        if (graphEditor_ != nullptr && newName.isNotEmpty())
            graphEditor_->getMacroController().renameMacro(macro->id, newName);
        else
            header_.restoreDisplayName(); // empty commit or no editor -- never blank a macro
        if (onMutated)
            onMutated();
        return;
    }

    auto* node = graph_->getNodeForId(nodeId_);
    auto* strip = node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
    if (strip == nullptr) {
        header_.restoreDisplayName(); // Direct/Master never reach here (rename disabled), so this is only a
        return;                       // stale/detached column mid-unbind
    }

    const auto before = strip->getExtraState();
    strip->setStripName(newName); // empty clears it back to today's macro/track-walk/"Channel N" rule
    const auto after = strip->getExtraState();
    undoManager_->recordNodeExtraStateChange(*graph_, nodeId_, before, after);
    if (onMutated)
        onMutated(); // refreshes the column from a fresh buildMixerSnapshot, same as any other edit here
}

void MixerColumnComponent::rebindControls() {
    panAttachment_.reset();
    fader_.unbind();
    midiLearnableEntries_.clear();

    if (graph_ == nullptr)
        return;
    auto* node = graph_->getNodeForId(nodeId_);
    auto* processor = node != nullptr ? node->getProcessor() : nullptr;
    if (processor == nullptr)
        return;

    if (auto* gainParam = findFloatParam(*processor, "gain"); gainParam != nullptr && undoManager_ != nullptr) {
        fader_.bind(*graph_, *undoManager_, *gainParam);
        registerMidiLearnable(fader_.getSlider(), gainParam);
    }
    if (auto* panParam = findFloatParam(*processor, "pan")) {
        const auto floatRange = panParam->getNormalisableRange();
        panSlider_.setNormalisableRange(juce::NormalisableRange<double>(
            (double)floatRange.start, (double)floatRange.end, (double)floatRange.interval, (double)floatRange.skew,
            floatRange.symmetricSkew));
        panAttachment_ = std::make_unique<juce::SliderParameterAttachment>(*panParam, panSlider_);
        applyPanAccessibilityText(panSlider_); // see this file's own comment on applyPanAccessibilityText
        registerMidiLearnable(panSlider_, panParam);
    }
    if (auto* muteParam = findBoolParam(*processor, "muted"))
        registerMidiLearnable(muteButton_, muteParam);

    auto* module = dynamic_cast<ModuleBase*>(processor);
    muteButton_.onClick = [this] { toggleMuted(); };
    const bool isChannelStrip = dynamic_cast<ChannelStripModule*>(processor) != nullptr;
    soloButton_.setVisible(isChannelStrip);
    soloButton_.onClick = [this] { toggleSoloed(); };
    // Only a ChannelStrip column's Solo is MIDI-learnable (soloButton_ is hidden, and has
    // no node command to point at, on every other column kind).
    if (isChannelStrip)
        registerSoloMidiLearnable();
    refreshMuteSoloAccessibility(module, dynamic_cast<ChannelStripModule*>(processor));
    juce::ignoreUnused(module);
}

void MixerColumnComponent::refreshMuteSoloAccessibility(ModuleBase* module, ChannelStripModule* strip) {
    // The M/S buttons are constructed with setClickingTogglesState(false) (this file's own
    // ctor) so a click's onClick lambda stays the single writer of mute/solo state -- otherwise
    // the button would self-toggle its visual state independently of whether the mutation actually
    // applied. Mirroring the module's real state into setToggleState() here (called after every
    // toggle, and once from rebindControls()) is what makes the button's PAINTED pressed state
    // track reality at all -- it never showed pressed before this (a latent bug). The accessible
    // title carries the on/off state explicitly in words rather than leaning on JUCE's stock
    // Button accessibility role, which keys off getClickingTogglesState() (false here) and so
    // would report a plain button regardless of setToggleState().
    const juce::String name = header_.getDisplayName();
    const bool muted = module != nullptr && module->isMuted();
    muteButton_.setToggleState(muted, juce::dontSendNotification);
    muteButton_.setTitle(name + " mute, " + (muted ? "on" : "off"));
    if (strip != nullptr) {
        const bool soloed = strip->isSoloed();
        soloButton_.setToggleState(soloed, juce::dontSendNotification);
        soloButton_.setTitle(name + " solo, " + (soloed ? "on" : "off"));
    }
}

void MixerColumnComponent::toggleMuted() {
    if (graph_ == nullptr || undoManager_ == nullptr)
        return;
    auto* n = graph_->getNodeForId(nodeId_);
    auto* m = n != nullptr ? dynamic_cast<ModuleBase*>(n->getProcessor()) : nullptr;
    if (m == nullptr)
        return;
    undoManager_->captureBeforeState(*graph_);
    m->setMuted(!m->isMuted());
    undoManager_->pushSnapshotFromCapture(*graph_);
    refreshMuteSoloAccessibility(m, dynamic_cast<ChannelStripModule*>(n->getProcessor()));
    repaint();
    if (onLiveStateChanged)
        onLiveStateChanged();
}

void MixerColumnComponent::toggleSoloed() {
    if (graph_ == nullptr || undoManager_ == nullptr || audioEngine_ == nullptr)
        return;
    auto* n = graph_->getNodeForId(nodeId_);
    auto* strip = n != nullptr ? dynamic_cast<ChannelStripModule*>(n->getProcessor()) : nullptr;
    if (strip == nullptr)
        return;
    undoManager_->captureBeforeState(*graph_);
    // Never strip->setSoloed() directly -- the engine's soloed-strip count would go stale (root
    // CLAUDE.md / Source/CLAUDE.md).
    audioEngine_->setChannelStripSoloed(nodeId_, !strip->isSoloed());
    undoManager_->pushSnapshotFromCapture(*graph_);
    refreshMuteSoloAccessibility(dynamic_cast<ModuleBase*>(n->getProcessor()), strip);
    repaint();
    if (onLiveStateChanged)
        onLiveStateChanged();
}

// See this method's own header comment -- re-syncs the M/S visuals after something other
// than THIS column's own click flips solo (a MIDI Remote node-command press).
void MixerColumnComponent::refreshMuteSoloVisual() {
    if (graph_ == nullptr)
        return;
    auto* n = graph_->getNodeForId(nodeId_);
    auto* processor = n != nullptr ? n->getProcessor() : nullptr;
    if (processor == nullptr)
        return;
    refreshMuteSoloAccessibility(dynamic_cast<ModuleBase*>(processor), dynamic_cast<ChannelStripModule*>(processor));
    repaint();
}

void MixerColumnComponent::unbindFromGraph() {
    panAttachment_.reset();
    fader_.unbind();
    // The send rows hold SliderParameterAttachments onto the strip's own sendNLevel
    // parameters -- the same use-after-free-on-undo the fader/pan pair above exist to avoid.
    sendList_.unbindFromGraph();
    // Every entry's `param` is a raw pointer into a graph node's parameter -- same
    // use-after-free-on-undo hazard as panAttachment_/fader_/sendList_ above
    // (Source/UI/CLAUDE.md's mixer-unbind invariant). Rebuilt by the next rebindControls().
    midiLearnableEntries_.clear();
    muteButton_.onClick = nullptr;
    soloButton_.onClick = nullptr;
    meter_.peakProvider = nullptr;
    // Same pre-restore discipline as the fader: detach the thumbnail's parameter listeners before
    // the graph-replacing mutation frees the module they point at (rebuild()'s eventual
    // setColumn()/setEqModule() re-binds against the NEW graph afterwards).
    eqThumbnail_.setEqModule(nullptr);
    graph_ = nullptr;
    undoManager_ = nullptr;
    audioEngine_ = nullptr;
}

void MixerColumnComponent::refreshMeter(float elapsedSeconds) {
    meter_.peakProvider = [this](int leg) -> float {
        if (graph_ == nullptr)
            return 0.0f;
        auto* node = graph_->getNodeForId(nodeId_);
        auto* processor = node != nullptr ? node->getProcessor() : nullptr;
        if (auto* strip = dynamic_cast<ChannelStripModule*>(processor))
            return strip->takeMeterPeak(meterReader_, leg);
        return 0.0f;
    };
    meter_.refresh(elapsedSeconds);
    meterReadout_.updatePeak(std::max(meter_.getDisplayedDbForTest(0), meter_.getDisplayedDbForTest(1)));
    // No timer of its own -- rides the SAME 10 Hz tick every other per-column visual
    // already uses (Source/UI/CLAUDE.md's no-unconditional-repaint rule), mirroring
    // ModuleComponent's own gated-timer precedent for its badge refresh.
    refreshMidiLearnBadges();
    // Same tick also keeps the armed breathing outline animating -- see that method's own
    // comment for why nothing did before this.
    repaintArmedMidiLearnOutline();
}

void MixerColumnComponent::setSelected(bool selected) {
    if (selected_ == selected)
        return;
    selected_ = selected;
    repaint();
}

void MixerColumnComponent::setKeyboardFocused(bool focused) {
    if (keyboardFocused_ == focused)
        return;
    keyboardFocused_ = focused;
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> MixerColumnComponent::createAccessibilityHandler() {
    // Role group -- the column's own name is what VoiceOver announces when Left/Right walks
    // onto it; the fader/pan/M/S beneath it read as their own child handlers (plan (c)).
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

void MixerColumnComponent::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto bg = laf != nullptr ? laf->getTheme().colors.surface : juce::Colour(0xff1B1F26);
    const auto border = laf != nullptr ? laf->getTheme().colors.border : juce::Colour(0xff2A2F38);
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    g.setColour(bg);
    g.fillRect(getLocalBounds());
    g.setColour(selected_ ? accent : border);
    g.drawRect(getLocalBounds(), selected_ ? 2 : 1);
}

void MixerColumnComponent::setReorderHooks(MixerColumnHeader::ReorderHooks hooks) {
    header_.reorderHooks = std::move(hooks);
}

void MixerColumnComponent::setHeaderContextMenu(std::function<void(const juce::MouseEvent&)> callback) {
    header_.onContextMenu = std::move(callback);
}

// A no-op when the strength is unchanged, so the panel can call it every layout without repainting
// the column's controls.
void MixerColumnComponent::setLift(float lift) {
    if (lift == lift_)
        return;
    lift_ = lift;
    repaint();
}

void MixerColumnComponent::paintOverChildren(juce::Graphics& g) {
    // Badges/armed outline paint OVER every child control they annotate, same as
    // ModuleComponent's paintMidiLearnOverlays() -- must run before the keyboard-focus outline
    // below returns early so a mapped/armed control still shows its overlay on an unfocused column.
    paintMidiLearnOverlays(g);

    // A dragged column reads as raised: a faint light wash and an accent border, both fading with lift_.
    if (lift_ > 0.0f) {
        const auto* liftLaf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const auto liftAccent = liftLaf != nullptr ? liftLaf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
        g.setColour(juce::Colours::white.withAlpha(0.05f * lift_));
        g.fillRect(getLocalBounds());
        g.setColour(liftAccent.withMultipliedAlpha(lift_));
        g.drawRect(getLocalBounds(), 1);
    }

    // The focused column's OWN outline, distinct from setSelected()'s reveal highlight --
    // they may co-paint (a revealed column can also be the keyboard-focused one). Real
    // hasKeyboardFocus() is always false headless with no native peer (same accepted gap
    // TimelineTrackFocusTests documents), so this is a plain flag MixerPanelComponent drives, not
    // paintFocusRegionOutline (that paints the REGION root -- the panel itself).
    if (!keyboardFocused_)
        return;
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    g.setColour(accent.withAlpha(0.85f));
    g.drawRect(getLocalBounds(), 2);
}

// Every part's y comes from the panel's shared MixerSectionLayout, resolved against this column's
// height -- never from this column's own content -- so every column's sections, and so every fader,
// sit on the same lines (see MixerSectionLayout::resolve for the order parts give way in a short
// column; the fader keeps kMinFaderHeight).
void MixerColumnComponent::resized() {
    const auto geometry = sectionLayout_->resolve(getHeight());
    const auto inner = getLocalBounds().reduced(MixerSectionLayout::kColumnInset);
    auto top = inner;
    header_.setBounds(top.removeFromTop(MixerSectionLayout::kHeaderHeight));
    sourceLineLabel_.setBounds(top.removeFromTop(MixerSectionLayout::kSourceLineHeight));
    layoutSections(geometry, inner);

    auto row = [&inner](int y, int height) { return juce::Rectangle<int>(inner.getX(), y, inner.getWidth(), height); };
    panSlider_.setBounds(row(geometry.panTop, geometry.panHeight).reduced(8, 0));

    auto msRow = row(geometry.msTop, MixerSectionLayout::kMsRowHeight);
    muteButton_.setBounds(msRow.removeFromLeft(msRow.getWidth() / 2).reduced(2));
    soloButton_.setBounds(msRow.reduced(2));

    // The clip readout sits directly above the meter+fader row it reports on (Cubase's own
    // "Meter Peak Level" placement).
    meterReadout_.setBounds(row(geometry.readoutTop, MixerSectionLayout::kMeterReadoutHeight));
    auto faderRow = row(geometry.faderTop, geometry.faderHeight);
    meter_.setBounds(faderRow.removeFromRight(kMeterWidth));
    faderRow.removeFromRight(2);
    fader_.setBounds(faderRow);
}

// A hidden section swaps its content for the 14 px summary strip. The insert and send viewports are
// hidden with it (so their rows are neither clickable nor in the accessibility tree); the EQ
// thumbnail only gets empty bounds, because its isVisible() means "this column has an EQ".
void MixerColumnComponent::layoutSections(const MixerSectionLayout::Geometry& geometry, juce::Rectangle<int> inner) {
    for (size_t i = 0; i < dividers_.size(); ++i) {
        const auto section = (MixerSection)(int)i;
        const bool hidden = sectionLayout_->isHidden(section);
        const juce::Rectangle<int> area(inner.getX(), geometry.sectionTop[i], inner.getWidth(),
                                        geometry.sectionHeight[i]);
        collapsed_[i].setVisible(hidden);
        collapsed_[i].setBounds(area);
        dividers_[i].setBounds(inner.getX(), geometry.dividerTop[i], inner.getWidth(),
                               MixerSectionLayout::kDividerHeight);
        if (section == MixerSection::Eq) {
            eqThumbnail_.setBounds(hidden ? area.withHeight(0) : area);
            continue;
        }
        auto& viewport = section == MixerSection::Inserts ? insertViewport_ : sendViewport_;
        viewport.setVisible(!hidden);
        viewport.setBounds(area);
    }
    insertViewport_.setContentHeight(insertList_.getPreferredHeight());
    sendViewport_.setContentHeight(sendList_.getPreferredHeight());
}

void MixerColumnComponent::setSectionLayout(MixerSectionLayout& layout) {
    sectionLayout_ = &layout;
    for (size_t i = 0; i < dividers_.size(); ++i) {
        dividers_[i].setLayout(sectionLayout_, (MixerSection)(int)i);
        collapsed_[i].setLayout(sectionLayout_, (MixerSection)(int)i);
    }
    resized();
}

void MixerColumnComponent::repaintSectionDividers() {
    for (auto& divider : dividers_)
        divider.repaint();
}

MixerSectionViewport& MixerColumnComponent::getSectionViewportForTest(MixerSection section) noexcept {
    return section == MixerSection::Sends ? sendViewport_ : insertViewport_;
}

void MixerColumnComponent::refreshCollapsedSummaries(int insertCount, int sendCount, bool hasEq) {
    collapsed_[(size_t)MixerSection::Inserts].setSummary(mixerSectionCountSummary(insertCount, "insert", "inserts"));
    collapsed_[(size_t)MixerSection::Sends].setSummary(mixerSectionCountSummary(sendCount, "send", "sends"));
    collapsed_[(size_t)MixerSection::Eq].setSummary(hasEq ? "EQ" : "no EQ");
}

void MixerColumnComponent::mouseUp(const juce::MouseEvent&) {
    // See MixerColumnHeader::mouseUp's comment on why this isn't gated on mouseWasClicked().
    if (onColumnClicked)
        onColumnClicked();
}

} // namespace synth::ui
