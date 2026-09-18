// Concern: FRO11 (P9-5) -- MixerColumnComponent's layout, param binding (pan/mute/solo -- fader
// binding lives in MixerFader itself) and the column-click-selects-macro gesture.
#include "MixerColumnComponent.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Mixer/PeakMeterLatch.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// FRO146: widened from the pre-meters-rework 10 px so two dB-scale bars + tick labels are legible
// (docs/mixer.md meters section) -- see MixerMeterScale.h's kBarsAreaWidth/kLabelMinWidth for the
// exact pixel budget this must cover. Kept as narrow as that budget allows so the column itself
// doesn't grow wider than necessary.
constexpr int kMeterWidth = 32;
constexpr int kMeterReadoutHeight = 12;

juce::AudioParameterFloat* findFloatParam(juce::AudioProcessor& processor, const juce::String& paramId) {
    for (auto* param : processor.getParameters())
        if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param);
            floatParam != nullptr && floatParam->paramID == paramId)
            return floatParam;
    return nullptr;
}
} // namespace

MixerColumnComponent::MixerColumnComponent() {
    addAndMakeVisible(header_);
    header_.onHeaderClicked = [this] {
        if (onColumnClicked)
            onColumnClicked();
    };
    addAndMakeVisible(sourceLineLabel_);
    sourceLineLabel_.setFont(juce::Font(juce::FontOptions(10.0f)));
    sourceLineLabel_.setJustificationType(juce::Justification::centredLeft);
    sourceLineLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff8A93A0));

    addAndMakeVisible(insertList_);
    insertList_.onEditOnCanvas = [this](const juce::String& uuid) {
        if (onEditOnCanvas)
            onEditOnCanvas(uuid);
    };
    insertList_.onMutated = [this] {
        if (onMutated)
            onMutated();
    };
    // FRO16 UAF fix: a live "Remove" from the row menu frees the removed node's processor
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

    addAndMakeVisible(sendList_);
    sendList_.onMutated = [this] {
        if (onMutated)
            onMutated();
    };

    panSlider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    panSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    // FRO18: MixerPanelComponent is the single focusable leaf -- every child control gives up
    // keyboard focus so Up/Down/Enter/M/S/R always reach the panel's own keyPressed() (the T160
    // trap docs/control/shortcuts.md documents: a focused Slider eats Up/Down, a focused TextButton eats
    // Return/Space).
    panSlider_.setWantsKeyboardFocus(false);
    panSlider_.textFromValueFunction = [](double pan) {
        if (std::abs(pan) < 0.005)
            return juce::String("Center");
        const int percent = (int)std::round(std::abs(pan) * 100.0);
        return juce::String(percent) + (pan < 0.0 ? "% left" : "% right");
    };
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
                                     synth::MacroSet& macros, GraphEditor& graphEditor, AudioEngine& audioEngine) {
    graph_ = &graph;
    undoManager_ = &undoManager;
    audioEngine_ = &audioEngine;
    insertList_.configure(graph, undoManager, macros, graphEditor);
    sendList_.configure(graph, undoManager, macros, graphEditor);
}

void MixerColumnComponent::setColumn(const synth::MixerColumn& column, const juce::String& sourceLine) {
    nodeId_ = column.nodeId;
    uuid_ = column.uuid;
    sourceLine_ = sourceLine;

    header_.setColour(column.colour);
    header_.setDisplayName(column.name);
    header_.setLinkedBadgeVisible(column.linkedToTrack);
    header_.setBusBadgeVisible(column.kind == synth::MixerColumn::Kind::Bus);

    // Doubles as §5.10's "the tracks that play into it" row -- `sourceLine` is the caller-resolved
    // (comma-joined) names of column.feedingTracks, the same tracks a "source line" names for a
    // single-source column.
    sourceLineLabel_.setText(sourceLine_, juce::dontSendNotification);
    insertList_.setEntries(column.inserts, column.insertChainIsLinear, column.editOnCanvasTargetUuid,
                           column.sourceNodeId, column.nodeId);
    sendList_.setEntries(column.sends, column.nodeId);

    // FRO16 (P9-10): the first Parametric EQ in signal order gets the column's curve thumbnail --
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

    // FRO18: accessible names -- "<name> fader"/"pan", e.g. "Lead 1 fader, -3.0 dB" (JUCE speaks
    // the minus sign as "minus"). setTitle() on `this` is what createAccessibilityHandler()'s
    // group role announces for the column itself.
    setTitle(column.name);
    fader_.setChannelName(column.name);
    panSlider_.setTitle(column.name + " pan");
    meter_.setTitle(column.name + " meter");

    rebindControls();
    resized();
}

void MixerColumnComponent::rebindControls() {
    panAttachment_.reset();
    fader_.unbind();

    if (graph_ == nullptr)
        return;
    auto* node = graph_->getNodeForId(nodeId_);
    auto* processor = node != nullptr ? node->getProcessor() : nullptr;
    if (processor == nullptr)
        return;

    if (auto* gainParam = findFloatParam(*processor, "gain"); gainParam != nullptr && undoManager_ != nullptr)
        fader_.bind(*graph_, *undoManager_, *gainParam);
    if (auto* panParam = findFloatParam(*processor, "pan")) {
        const auto floatRange = panParam->getNormalisableRange();
        panSlider_.setNormalisableRange(juce::NormalisableRange<double>(
            (double)floatRange.start, (double)floatRange.end, (double)floatRange.interval, (double)floatRange.skew,
            floatRange.symmetricSkew));
        panAttachment_ = std::make_unique<juce::SliderParameterAttachment>(*panParam, panSlider_);
    }

    auto* module = dynamic_cast<ModuleBase*>(processor);
    muteButton_.onClick = [this] { toggleMuted(); };
    soloButton_.setVisible(dynamic_cast<ChannelStripModule*>(processor) != nullptr);
    soloButton_.onClick = [this] { toggleSoloed(); };
    refreshMuteSoloAccessibility(module, dynamic_cast<ChannelStripModule*>(processor));
    juce::ignoreUnused(module);
}

void MixerColumnComponent::refreshMuteSoloAccessibility(ModuleBase* module, ChannelStripModule* strip) {
    // FRO18: the M/S buttons are constructed with setClickingTogglesState(false) (this file's own
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
}

void MixerColumnComponent::unbindFromGraph() {
    panAttachment_.reset();
    fader_.unbind();
    // FRO15: the send rows hold SliderParameterAttachments onto the strip's own sendNLevel
    // parameters -- the same use-after-free-on-undo the fader/pan pair above exist to avoid.
    sendList_.unbindFromGraph();
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
            return strip->takeMeterPeak(synth::MeterReader::Mixer, leg);
        return 0.0f;
    };
    meter_.refresh(elapsedSeconds);
    meterReadout_.updatePeak(std::max(meter_.getDisplayedDbForTest(0), meter_.getDisplayedDbForTest(1)));
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
    // FRO18: role group -- the column's own name is what VoiceOver announces when Left/Right walks
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

void MixerColumnComponent::paintOverChildren(juce::Graphics& g) {
    // FRO18: the focused column's OWN outline, distinct from setSelected()'s reveal highlight --
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

void MixerColumnComponent::resized() {
    auto bounds = getLocalBounds().reduced(2);
    header_.setBounds(bounds.removeFromTop(24));
    sourceLineLabel_.setBounds(bounds.removeFromTop(14));
    insertList_.setBounds(bounds.removeFromTop(juce::jmin(bounds.getHeight() / 3, insertList_.getPreferredHeight())));
    sendList_.setBounds(bounds.removeFromTop(juce::jmin(bounds.getHeight() / 3, sendList_.getPreferredHeight())));

    // Reserves 0 px when there is no EQ to show -- MixerEqThumbnail::setEqModule(nullptr) already
    // hid it, this just keeps the layout from leaving a gap behind it.
    if (eqThumbnail_.isVisible())
        eqThumbnail_.setBounds(bounds.removeFromTop(28));

    auto controls = bounds;
    panSlider_.setBounds(controls.removeFromTop(28).reduced(8, 0));

    auto msRow = controls.removeFromBottom(20);
    muteButton_.setBounds(msRow.removeFromLeft(msRow.getWidth() / 2).reduced(2));
    soloButton_.setBounds(msRow.reduced(2));

    // FRO146: the clip readout sits directly above the meter+fader row it reports on (Cubase's own
    // "Meter Peak Level" placement).
    meterReadout_.setBounds(controls.removeFromTop(kMeterReadoutHeight));
    meter_.setBounds(controls.removeFromRight(kMeterWidth));
    controls.removeFromRight(2);
    fader_.setBounds(controls);
}

void MixerColumnComponent::mouseUp(const juce::MouseEvent&) {
    // See MixerColumnHeader::mouseUp's comment on why this isn't gated on mouseWasClicked().
    if (onColumnClicked)
        onColumnClicked();
}

} // namespace synth::ui
