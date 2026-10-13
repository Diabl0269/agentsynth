// Concern: the automation lane header's construction, doc refresh, value readout, layout and paint.
// The record-mode and lane menu actions live in AutomationLaneHeaderMenu.cpp, the parameter pickers in
// AutomationLaneHeaderParameterPicker.cpp.
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"

namespace synth::ui {

namespace {
constexpr int kPadding = 4;
constexpr int kComboWidth = 62;
constexpr float kStripeAlpha = 0.45f;
constexpr int kMarkerGap = 2; // between the marker and the name button, whose own padding makes the rest
constexpr int kNameButtonHeight = 20;
constexpr float kModuleFontSize = 9.5f;

struct LaneHeaderColours {
    juce::Colour surface;
    juce::Colour border;
    juce::Colour text;
    juce::Colour textMuted;
};

LaneHeaderColours coloursFor(const juce::Component& c) {
    const auto& t = synth::theme::themeOf(c).colors;
    return {t.surface, t.border, t.textPrimary, t.textMuted};
}
} // namespace

AutomationLaneHeaderComponent::AutomationLaneHeaderComponent(synth::TimelineDoc& doc, synth::LaneId lane,
                                                             TrackHeaderHost* host, AppUndoManager* undo)
    : doc_(doc)
    , laneId_(lane)
    , host_(host)
    , undo_(undo) {
    setComponentID("automationLaneHeader");
    // The row is a keyboard stop of its own (Up/Down walk the track list through it). A press does not grab focus (it
    // may become a reorder drag); a plain click on the row, not on a control, takes it on release (see mouseUp).
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);

    addAndMakeVisible(marker_);
    marker_.setTooltip("Automation lane: drawn changes over time");
    marker_.addMouseListener(this, false);
    addAndMakeVisible(nameButton_);
    nameButton_.onClick = [this] { openChangeParameterPicker(); };

    addAndMakeVisible(recordMode_);
    recordMode_.setComponentID("automationLaneRecordMode");
    recordMode_.addItem("Off", 1);
    recordMode_.addItem("Read", 2);
    recordMode_.addItem("Touch", 3);
    recordMode_.addItem("Latch", 4);
    recordMode_.addItem("Write", 5);
    // Tab reaches it; a click does not move focus off the clips, whose Cmd+X/C/V the app routes by
    // where real focus sits.
    recordMode_.setMouseClickGrabsKeyboardFocus(false);
    recordMode_.onChange = [this] { applyRecordModeChoice(recordMode_.getSelectedId()); };

    addAndMakeVisible(readout_);
    // The readout takes mouse clicks (for its tooltip), so a right-click on it is forwarded here to open the menu.
    readout_.addMouseListener(this, false);
    // Up/Down on the combo would change its selection, so the lane keys are offered before the combo sees them.
    recordMode_.addKeyListener(this);

    refreshFromDoc();
}

// Names, colour and record mode are document state, so every doc notification re-reads them; the
// combo is set without a notification because it is reflecting the doc, not editing it.
void AutomationLaneHeaderComponent::refreshFromDoc() {
    const auto* lane = doc_.getLane(laneId_);
    if (lane == nullptr)
        return;
    const auto labels = laneLabelsFor(*lane, host_);
    parameterName_ = labels.parameter;
    moduleName_ = labels.module;
    setTitle(parameterName_ + " automation lane");
    setDescription(moduleName_);
    nameButton_.setName(parameterName_);
    nameButton_.setActionText("Change parameter of " + parameterName_, "Pick a different parameter");
    readout_.setParameterName(parameterName_);
    trackColour_ = laneColourFor(doc_, laneId_, coloursFor(*this).textMuted);

    recordMode_.setSelectedId(lane->recordMode + 1, juce::dontSendNotification);
    recordMode_.setTitle(parameterName_ + " record mode");
    recordMode_.setTooltip(parameterName_ + " record mode");
    setTooltip(parameterName_ + " lane: right-click, Shift+F10 or Return for the lane menu. Drag to reorder");
    applyRecordModeColour();
    resized(); // the name button is as wide as the name
    repaint();
}

// Write is the one mode that overwrites the lane while the transport records, so it reads in the
// error colour as a standing warning.
void AutomationLaneHeaderComponent::applyRecordModeColour() {
    juce::Colour text = coloursFor(*this).text;
    if (recordMode_.getSelectedId() == static_cast<int>(synth::LaneRecordMode::Write) + 1) {
        text = juce::Colour(0xffE5484D);
        if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
            text = lf->getTheme().colors.error;
    }
    recordMode_.setColour(juce::ComboBox::textColourId, text);
}

// Driven from the panel's existing transport poll, never a timer of its own; the text comparison
// keeps an unchanged value from costing a repaint even when the beat moved.
void AutomationLaneHeaderComponent::setReadoutBeat(double beat) {
    const auto* lane = doc_.getLane(laneId_);
    if (lane == nullptr)
        return;
    const auto text = laneValueText(*lane, laneValueAt(*lane, beat), host_);
    if (text == valueText_)
        return;
    valueText_ = text;
    readout_.setPlayheadText(text);
}

void AutomationLaneHeaderComponent::setSelectedPointValue(const std::optional<juce::String>& text) {
    readout_.setSelectedText(text);
}

void AutomationLaneHeaderComponent::resized() {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(kIndent + kStripeWidth + kPadding);
    bounds.removeFromRight(kPadding);
    auto top = bounds.removeFromTop(bounds.getHeight() / 2);
    auto bottom = bounds;
    readout_.setBounds(top.removeFromRight(kReadoutWidth));
    recordMode_.setBounds(bottom.removeFromRight(kComboWidth).reduced(0, 1));
    marker_.setBounds(
        top.removeFromLeft(LaneMarkerIcon::kSize).withSizeKeepingCentre(LaneMarkerIcon::kSize, LaneMarkerIcon::kSize));
    top.removeFromLeft(kMarkerGap);
    const auto button = top.withWidth(juce::jmin(nameButton_.getPreferredWidth(), top.getWidth()));
    nameArea_ = button.withSizeKeepingCentre(button.getWidth(), juce::jmin(button.getHeight(), kNameButtonHeight));
    nameButton_.setBounds(nameArea_);
    noteArea_ = bottom.withTrimmedLeft(LaneMarkerIcon::kSize + kMarkerGap + LaneNameButton::kPadding)
                    .withTrimmedRight(kPadding);
}

void AutomationLaneHeaderComponent::paint(juce::Graphics& g) {
    const auto colours = coloursFor(*this);
    g.fillAll(colours.surface);
    g.setColour(colours.border);
    g.drawHorizontalLine(getHeight() - 1, (float)kIndent, (float)getWidth());
    g.setColour(trackColour_.withAlpha(kStripeAlpha));
    g.fillRect(kIndent, 0, kStripeWidth, getHeight());

    g.setColour(colours.textMuted);
    g.setFont(juce::Font(juce::FontOptions(kModuleFontSize)));
    g.drawText(moduleName_, noteArea_, juce::Justification::centredLeft, true);
}

// The combo's own focus outline comes from the look-and-feel; the shared accent ring goes over it so
// every control in the row shows focus the same way.
void AutomationLaneHeaderComponent::paintOverChildren(juce::Graphics& g) {
    synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this);
    if (lift_ > 0.0f) {
        const auto accent = synth::theme::themeOf(*this).colors.accent;
        g.setColour(juce::Colours::white.withAlpha(0.05f * lift_));
        g.fillRect(getLocalBounds());
        g.setColour(accent.withMultipliedAlpha(lift_));
        g.drawRect(getLocalBounds(), 1);
    }
    synth::ui::paintFocusRing(g, recordMode_.getBounds().toFloat().expanded(1.0f), recordMode_, 3.0f);
}

} // namespace synth::ui
