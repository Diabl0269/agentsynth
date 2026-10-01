// Concern: the automation lane header's construction, doc refresh, value readout, layout and paint.
// The record-mode and "..." menu actions live in AutomationLaneHeaderMenu.cpp.
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"

namespace synth::ui {

namespace {
constexpr int kPadding = 4;
constexpr int kComboWidth = 62;
constexpr int kMenuButtonWidth = 18;
constexpr float kStripeAlpha = 0.45f;
constexpr float kNameFontSize = 11.0f;
constexpr float kModuleFontSize = 9.5f;
constexpr float kReadoutFontSize = 10.0f;

struct LaneHeaderColours {
    juce::Colour surface{0xff1B1F26};
    juce::Colour border{0xff2A2F38};
    juce::Colour text{0xffEAEEF3};
    juce::Colour textMuted{0xff8A93A0};
};

LaneHeaderColours coloursFor(const juce::Component& c) {
    LaneHeaderColours result;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel())) {
        const auto& t = lf->getTheme().colors;
        result = {t.surface, t.border, t.textPrimary, t.textMuted};
    }
    return result;
}
} // namespace

AutomationLaneHeaderComponent::AutomationLaneHeaderComponent(synth::TimelineDoc& doc, synth::LaneId lane,
                                                             TrackHeaderHost* host, AppUndoManager* undo)
    : doc_(doc)
    , laneId_(lane)
    , host_(host)
    , undo_(undo) {
    setComponentID("automationLaneHeader");

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

    addAndMakeVisible(menuButton_);
    menuButton_.onClick = [this] { showMenu(); };

    refreshFromDoc();
}

AutomationLaneHeaderComponent::MenuButton::MenuButton()
    : juce::Button("automationLaneMenu") {
    setComponentID("automationLaneMenu");
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
}

// Three dots drawn rather than a text ellipsis glyph, so the button never depends on font coverage.
void AutomationLaneHeaderComponent::MenuButton::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto colours = coloursFor(*this);
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(highlighted ? colours.text : colours.textMuted);
    constexpr float dot = 2.5f;
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse(
            juce::Rectangle<float>(dot, dot).withCentre(bounds.getCentre().translated((float)i * 4.5f, 0.0f)));
    synth::ui::paintFocusRing(g, bounds, *this, 3.0f);
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
    trackColour_ = laneColourFor(doc_, laneId_, coloursFor(*this).textMuted);

    recordMode_.setSelectedId(lane->recordMode + 1, juce::dontSendNotification);
    recordMode_.setTitle(parameterName_ + " record mode");
    recordMode_.setTooltip(parameterName_ + " record mode");
    menuButton_.setTitle("Lane menu for " + parameterName_);
    menuButton_.setTooltip("Lane menu for " + parameterName_);
    applyRecordModeColour();
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
    repaint(readoutArea_);
}

void AutomationLaneHeaderComponent::resized() {
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(kIndent + kStripeWidth + kPadding);
    bounds.removeFromRight(kPadding);
    auto top = bounds.removeFromTop(bounds.getHeight() / 2);
    auto bottom = bounds;
    menuButton_.setBounds(top.removeFromRight(kMenuButtonWidth).reduced(0, 1));
    readoutArea_ = top.removeFromRight(kReadoutWidth);
    recordMode_.setBounds(bottom.removeFromRight(kComboWidth).reduced(0, 1));
    nameArea_ = top.getUnion(bottom.withTrimmedRight(kPadding));
}

void AutomationLaneHeaderComponent::paint(juce::Graphics& g) {
    const auto colours = coloursFor(*this);
    g.fillAll(colours.surface);
    g.setColour(colours.border);
    g.drawHorizontalLine(getHeight() - 1, (float)kIndent, (float)getWidth());
    g.setColour(trackColour_.withAlpha(kStripeAlpha));
    g.fillRect(kIndent, 0, kStripeWidth, getHeight());

    auto names = nameArea_;
    const auto topLine = names.removeFromTop(names.getHeight() / 2);
    g.setColour(colours.text);
    g.setFont(juce::Font(juce::FontOptions(kNameFontSize, juce::Font::bold)));
    g.drawText(parameterName_, topLine, juce::Justification::centredLeft, true);
    g.setColour(colours.textMuted);
    g.setFont(juce::Font(juce::FontOptions(kModuleFontSize)));
    g.drawText(moduleName_, names, juce::Justification::centredLeft, true);

    g.setFont(
        juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), kReadoutFontSize, juce::Font::plain)));
    g.drawText(valueText_, readoutArea_, juce::Justification::centredRight, true);
}

// The combo's own focus outline comes from the look-and-feel; the shared accent ring goes over it so
// every control in the row shows focus the same way.
void AutomationLaneHeaderComponent::paintOverChildren(juce::Graphics& g) {
    synth::ui::paintFocusRing(g, recordMode_.getBounds().toFloat().expanded(1.0f), recordMode_, 3.0f);
}

} // namespace synth::ui
