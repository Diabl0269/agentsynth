// Concern: the automation lane header's construction, doc refresh, value readout, layout and paint.
// The record-mode and lane menu actions live in AutomationLaneHeaderMenu.cpp, the control chips' actions in
// AutomationLaneHeaderControls.cpp, the parameter pickers in AutomationLaneHeaderParameterPicker.cpp.
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"

namespace synth::ui {

namespace {
constexpr int kPadding = 4;
constexpr int kMinNoteWidth = 40; // px of the module note kept when the chips want more
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
                                                             TrackHeaderHost* host, AppUndoManager* undo,
                                                             LaneControlSet* controls)
    : doc_(doc)
    , laneId_(lane)
    , host_(host)
    , undo_(undo)
    , strip_(LaneControlKind::Automation, controls, controlActions()) {
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

    addAndMakeVisible(strip_);
    strip_.onPreferredWidthChanged = [this] { resized(); };
    strip_.onEditLookChanged = [this] { repaint(); };
    strip_.onContextMenu = [this](juce::Point<int> screen) { showMenuAt(contextMenuOptionsAtPoint(screen)); };

    addAndMakeVisible(readout_);
    // The readout takes mouse clicks (for its tooltip), so a right-click on it is forwarded here to open the menu.
    readout_.addMouseListener(this, false);

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

    strip_.refresh();
    setTooltip(parameterName_ + " lane: right-click, Shift+F10 or Return for the lane menu. Drag to reorder");
    resized(); // the name button is as wide as the name
    repaint();
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
    // The chips take the bottom line's right end, as wide as the set wants but leaving the module note some room.
    const int chipSize = juce::jmin(LaneControlChip::kSize, bottom.getHeight());
    strip_.setChipSize(chipSize);
    const int stripWidth = juce::jmin(strip_.preferredWidth(), juce::jmax(chipSize, bottom.getWidth() - kMinNoteWidth));
    strip_.setBounds(bottom.removeFromRight(stripWidth));
    marker_.setBounds(
        top.removeFromLeft(LaneMarkerIcon::kSize).withSizeKeepingCentre(LaneMarkerIcon::kSize, LaneMarkerIcon::kSize));
    top.removeFromLeft(kMarkerGap);
    const auto button = top.withWidth(juce::jmin(nameButton_.getPreferredWidth(), top.getWidth()));
    nameArea_ = button.withSizeKeepingCentre(button.getWidth(), juce::jmin(button.getHeight(), kNameButtonHeight));
    nameButton_.setBounds(nameArea_);
    noteArea_ = bottom.withTrimmedLeft(LaneMarkerIcon::kSize + kMarkerGap + LaneNameButton::kPadding)
                    .withTrimmedRight(kPadding);
    strip_.placeTray(getBounds());
}

void AutomationLaneHeaderComponent::moved() {
    strip_.placeTray(getBounds());
    strip_.repositionRow();
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
    if (const float edit = strip_.editAmount(); edit > 0.0f) {
        const auto accent = synth::theme::themeOf(*this).colors.accent;
        const auto outline = getLocalBounds().withTrimmedLeft(kIndent);
        g.setColour(accent.withMultipliedAlpha(0.22f * edit));
        g.drawRect(outline.reduced(1), 2);
        g.setColour(accent.withMultipliedAlpha(edit));
        g.drawRect(outline, 1);
    }
}

} // namespace synth::ui
