// Concern: the modulator band's identity and controls -- what routing it stands for, its names, tooltip and
// accessible value, its picture, and the knob gestures (drag, Up/Down, double-click) while no amount lane
// exists. The proxy doc behind the curve editor lives in ModulatorBandEdits.cpp.
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorBand.h"
#include "UI/Layout/TooltipHelpHandler.h"

#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/TimelineBeatsPerBar.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <cmath>

namespace synth::ui {

namespace {
constexpr float kFocusRadius = 3.0f;

// What a screen reader reads and sets: the amount in -1..1, spoken as "+50%". Read-only while an amount
// lane exists, because the lane, not the knob, decides the amount then.
class AmountValueInterface : public juce::AccessibilityValueInterface {
public:
    explicit AmountValueInterface(ModulatorBand& band)
        : band_(band) {}
    bool isReadOnly() const override { return band_.amountLane() != nullptr; }
    double getCurrentValue() const override { return band_.currentAmount(); }
    juce::String getCurrentValueAsString() const override { return amountText(band_.currentAmount()); }
    void setValue(double value) override { band_.setKnobAmount(value); }
    void setValueAsString(const juce::String& text) override {
        setValue(text.retainCharacters("0123456789.-").getDoubleValue() / 100.0);
    }
    AccessibleValueRange getRange() const override { return {{-1.0, 1.0}, ModulatorBand::kNudgeStep}; }

private:
    ModulatorBand& band_;
};
} // namespace

ModulatorBand::ModulatorBand(TimelineViewState& viewState)
    : viewState_(viewState) {
    setComponentID("modulatorBand");
    setInterceptsMouseClicks(false, false);
    setAccessible(false);
    proxy_.addListener(this);
}

ModulatorBand::~ModulatorBand() {
    cancelPendingUpdate();
    editor_.reset();
    proxy_.removeListener(this);
}

void ModulatorBand::setTransport(synth::TransportService* transport) {
    transport_ = transport;
    if (editor_ != nullptr)
        editor_->setTransport(transport);
}

void ModulatorBand::setEditTool(EditTool tool) {
    tool_ = tool;
    if (editor_ != nullptr)
        editor_->setEditTool(tool);
    updateClickRouting();
}

void ModulatorBand::setDrawShape(DrawShape shape) {
    drawShape_ = shape;
    if (editor_ != nullptr)
        editor_->setDrawShape(shape);
}

// A routing with an Attenuverter gets the curve editor over its proxy lane and becomes a Tab stop; a direct
// cable has no amount to edit, so its band is decoration the row in the header column already names, and
// takes no clicks so the clip lanes underneath still decide.
void ModulatorBand::setModulator(const ModulatorInfo& info, synth::LaneId ownerLane,
                                 const juce::String& parameterName) {
    info_ = info;
    ownerLane_ = ownerLane;
    parameterName_ = parameterName;
    if (isEditable() && editor_ == nullptr) {
        editor_ = std::make_unique<AutomationLaneEditor>(viewState_);
        editor_->setTimelineDoc(&proxy_);
        editor_->setTransport(transport_);
        editor_->setEditTool(tool_);
        editor_->setDrawShape(drawShape_);
        // The band is the one Tab stop and the one accessible node: the editor's press hands focus up to it.
        editor_->setWantsKeyboardFocus(false);
        editor_->onLaneMenuRequested = [this](const juce::PopupMenu::Options& options) {
            if (onMenuRequested)
                onMenuRequested(options);
        };
        editor_->setAccessible(false);
        addAndMakeVisible(*editor_);
        editor_->setBounds(getLocalBounds());
    } else if (!isEditable()) {
        editor_.reset();
    }
    if (isAccessible() != isEditable()) {
        setWantsKeyboardFocus(isEditable());
        setAccessible(isEditable());
    }
    if (const auto* lane = amountLane())
        readout_ = laneValueAt(*lane, 0.0);
    else
        readout_ = knobAmount();
    refreshFromDoc();
}

void ModulatorBand::refreshFromDoc() {
    if (!committing_)
        syncProxy();
    updateClickRouting();
    applyNames();
    repaint();
}

void ModulatorBand::setTrackColour(juce::Colour colour) {
    if (editor_ != nullptr)
        editor_->setCurveColour(colour);
}

void ModulatorBand::setAmountReadout(double amount) {
    const bool changed = std::abs(amount - readout_) > 1.0e-6;
    readout_ = amount;
    if (!committing_)
        syncProxy(); // the flat line follows the knob when it is moved on the canvas card
    if (changed) {
        applyNames();
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    }
}

const synth::AutomationLane* ModulatorBand::amountLane() const {
    return doc_ != nullptr ? amountLaneFor(*doc_, info_.attenuverterUuid) : nullptr;
}

double ModulatorBand::knobAmount() const {
    return host_ != nullptr && isEditable() ? (double)host_->getNodeParameter(info_.attenuverterUuid, kAmountParamId)
                                            : 0.0;
}

synth::TrackId ModulatorBand::ownerTrack() const {
    const auto* track = doc_ != nullptr ? doc_->getTrackForLane(ownerLane_) : nullptr;
    return track != nullptr ? track->id : synth::TrackId();
}

// The editor takes the press whenever it has something to do: a lane to edit, or a stroke that will create
// one. With no lane and a pointer tool the band itself takes it, as the knob drag.
void ModulatorBand::updateClickRouting() {
    setInterceptsMouseClicks(isEditable(), isEditable());
    if (editor_ != nullptr)
        editor_->setInterceptsMouseClicks(amountLane() != nullptr || tool_ == EditTool::Draw, false);
}

// "<Parameter> <modulator> amount", e.g. "Cutoff LFO 1 amount", valued "+50%".
void ModulatorBand::applyNames() {
    if (!isEditable())
        return;
    setTitle(parameterName_ + " " + info_.sourceTitle + " amount");
    setDescription(amountText(readout_));
}

juce::String ModulatorBand::getTooltip() {
    if (!isEditable())
        return {};
    const auto who = info_.sourceTitle;
    if (amountLane() != nullptr)
        return "Draw to change how much " + who + " moves " + parameterName_ +
               " over time; erase every point to go back to one amount";
    return "Drag to set how much " + who + " moves " + parameterName_ +
           "; draw to change it over time. Up/Down nudges it (Shift: by 10%)";
}

std::unique_ptr<juce::AccessibilityHandler> ModulatorBand::createAccessibilityHandler() {
    if (!isEditable())
        return juce::Component::createAccessibilityHandler();
    // TooltipHelpHandler, so VoiceOver reads the tooltip as the band's help text.
    return std::make_unique<TooltipHelpHandler>(
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<AmountValueInterface>(*this)});
}

void ModulatorBand::resized() {
    if (editor_ != nullptr)
        editor_->setBounds(getLocalBounds());
}

// A direct cable: the faint "it modulates here" fill. An editable band is painted by its editor.
void ModulatorBand::paint(juce::Graphics& g) {
    if (!isEditable())
        g.fillAll(info_.colour.withAlpha(kBandAlpha));
}

void ModulatorBand::paintOverChildren(juce::Graphics& g) {
    if (isEditable())
        paintFocusRing(g, getLocalBounds().toFloat(), *this, kFocusRadius);
}

//==============================================================================
// The knob: while no amount lane exists, the flat line IS the Attenuverter's knob value.

void ModulatorBand::setKnobAmount(double amount) {
    if (host_ == nullptr || !isEditable() || amountLane() != nullptr)
        return;
    host_->setNodeParameter(info_.attenuverterUuid, kAmountParamId, (float)juce::jlimit(-1.0, 1.0, amount),
                            ParameterEditPhase::Once);
    setAmountReadout(knobAmount());
}

// A right-click opens the modulator row's menu at the pointer and starts no knob drag.
void ModulatorBand::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        if (onMenuRequested)
            onMenuRequested(contextMenuOptionsAtPoint(e.getScreenPosition()));
        return;
    }
    grabKeyboardFocus();
    knobDragging_ = false;
    knobDragStart_ = knobAmount();
}

bool ModulatorBand::showContextMenuForKeyboardFocus() {
    if (!onMenuRequested)
        return false;
    onMenuRequested(contextMenuOptionsAtPoint(getScreenBounds().getPosition()));
    return true;
}

// Relative, like a knob: the band's full height is the whole -100%..+100% span. The undo step opens on the
// first real move, so a plain click records nothing.
void ModulatorBand::mouseDrag(const juce::MouseEvent& e) {
    if (host_ == nullptr || !isEditable() || amountLane() != nullptr || tool_ == EditTool::Erase ||
        tool_ == EditTool::Draw || !e.mods.isLeftButtonDown() || getHeight() <= 0)
        return;
    const double value =
        juce::jlimit(-1.0, 1.0, knobDragStart_ - (double)e.getDistanceFromDragStartY() * 2.0 / (double)getHeight());
    const auto phase = knobDragging_ ? ParameterEditPhase::Change : ParameterEditPhase::Begin;
    knobDragging_ = true;
    host_->setNodeParameter(info_.attenuverterUuid, kAmountParamId, (float)value, phase);
    setAmountReadout(value);
}

void ModulatorBand::mouseUp(const juce::MouseEvent&) {
    if (!knobDragging_)
        return;
    knobDragging_ = false;
    if (host_ != nullptr)
        host_->setNodeParameter(info_.attenuverterUuid, kAmountParamId, (float)knobAmount(), ParameterEditPhase::End);
}

// A double-click with a pointer tool starts the lane with one point where it landed, as a double-click on any
// lane adds a point.
void ModulatorBand::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!isEditable() || amountLane() != nullptr || tool_ == EditTool::Erase || editor_ == nullptr)
        return;
    const double beat = std::max(0.0, viewState_.snapBeat(viewState_.xToBeat((double)e.x), beatsPerBarFor(transport_)));
    createLaneWithPoint(beat, juce::jlimit(-1.0, 1.0, editor_->yToValue((double)e.y)));
}

bool ModulatorBand::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && editor_ != nullptr)
        return editor_->keyPressed(key); // the editor has no focus of its own: an Escape mid-stroke is its
    if (!isEditable() || amountLane() != nullptr)
        return false;
    const int code = key.getKeyCode();
    if (code != juce::KeyPress::upKey && code != juce::KeyPress::downKey)
        return false;
    const double step = key.getModifiers().isShiftDown() ? kLargeNudgeStep : kNudgeStep;
    setKnobAmount(knobAmount() + (code == juce::KeyPress::upKey ? step : -step));
    return true;
}

} // namespace synth::ui
