// Concern: the modulator row's construction, names, layout and paint. The edits, the live-value refresh and
// the menu live in ModulatorRowEdits.cpp; the band in the lanes region is ModulatorBand.
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

#include "Modules/LfoRateDivisions.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TrackColour.h"

namespace synth::ui {

namespace {
constexpr int kPadding = 4;
constexpr int kTagWidth = 22;
constexpr int kShapeIconWidth = 22;
constexpr int kSyncWidth = 48;
constexpr int kGap = 2;
constexpr int kAmountTextWidth = 34; // "-100%"
constexpr int kRateTextWidth = 42;   // "20.0 Hz"
constexpr float kTagFontSize = 8.5f;
constexpr float kTitleFontSize = 10.0f;
constexpr float kValueFontSize = 9.0f;

struct RowColours {
    juce::Colour surface;
    juce::Colour border;
    juce::Colour text;
    juce::Colour textMuted;
};

RowColours coloursFor(const juce::Component& c) {
    const auto& t = synth::theme::themeOf(c).colors;
    return {t.surface, t.border, t.textPrimary, t.textMuted};
}

// A small control's value, drawn beside its bar: the slider shows no text box of its own, because a
// text box is an extra editable child with no name of its own for a screen reader, and not over the bar,
// whose fader cap would cover it.
void paintSliderValue(juce::Graphics& g, juce::Slider& slider, juce::Rectangle<int> area, juce::Colour colour) {
    if (!slider.isVisible() || area.isEmpty())
        return;
    g.setColour(colour);
    g.setFont(juce::Font(juce::FontOptions(kValueFontSize)));
    g.drawText(slider.getTextFromValue(slider.getValue()), area, juce::Justification::centredRight, false);
}
} // namespace

ModulatorRow::ModulatorRow(const ModulatorInfo& info, TrackHeaderHost* host, const juce::String& parameterName)
    : info_(info)
    , host_(host)
    , parameterName_(parameterName) {
    setComponentID("modulatorRow");
    // The row is a keyboard stop of its own, like a lane header (Up/Down walk the track list through it).
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    if (info_.isLfo)
        initLfoControls();
    applyNames();
    refreshValues();
}

// Tab reaches every control; a click does not move focus off the clips, whose Cmd+X/C/V the app
// routes by where real focus sits (same rule as the lane header).
void ModulatorRow::initLfoControls() {
    const juce::StringArray shapes{"Sine", "Triangle", "Sawtooth", "Square", "S&H", "Custom"};
    for (int i = 0; i < shapes.size(); ++i)
        shape_.addItem(shapes[i], i + 1);
    const juce::StringArray& rates = synth::lfoRateDivisions();
    for (int i = 0; i < rates.size(); ++i)
        syncRate_.addItem(rates[i], i + 1);

    rateHz_.setSliderStyle(juce::Slider::LinearBar);
    rateHz_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    rateHz_.setNormalisableRange(juce::NormalisableRange<double>(0.01, 20.0, 0.01, 0.5));
    rateHz_.textFromValueFunction = [](double v) { return juce::String(v, v < 10.0 ? 2 : 1) + " Hz"; };
    sync_.setButtonText("Sync");

    for (juce::Component* c : std::initializer_list<juce::Component*>{&shape_, &syncRate_, &rateHz_, &sync_}) {
        c->setMouseClickGrabsKeyboardFocus(false);
        c->setWantsKeyboardFocus(true);
        addChildComponent(c);
    }
    shape_.setVisible(true);
    sync_.setVisible(true);

    const auto lfo = info_.sourceUuid;
    shape_.onChange = [this, lfo] {
        shapeIcon_.setShape(shape_.getSelectedId() - 1);
        edit(lfo, "shape", (float)(shape_.getSelectedId() - 1), ParameterEditPhase::Once);
    };
    syncRate_.onChange = [this, lfo] {
        edit(lfo, "rateSync", (float)(syncRate_.getSelectedId() - 1), ParameterEditPhase::Once);
    };
    sync_.onClick = [this, lfo] {
        edit(lfo, "mode", sync_.getToggleState() ? 1.0f : 0.0f, ParameterEditPhase::Once);
        refreshValues(); // swaps the sync-rate combo and the Hz bar
    };
    wireDragEdits(rateHz_, lfo, "rateHz", [](double v) { return (float)v; });

    // The picture is display only; a right-click on it reaches the row's menu through the row's mouse listener.
    addAndMakeVisible(shapeIcon_);
    shapeIcon_.addMouseListener(this, false);
}

void ModulatorRow::setInfo(const ModulatorInfo& info, const juce::String& parameterName) {
    jassert(info.key() == info_.key());
    info_ = info;
    parameterName_ = parameterName;
    applyNames();
    repaint();
}

// Every name says which parameter and which source it controls, so a screen reader moving through a
// stack of rows ("Cutoff LFO shape", "Cutoff LFO rate") never has to guess. The amount is not a control
// here: it is the band's ("Cutoff LFO 1 amount").
void ModulatorRow::applyNames() {
    const auto who = parameterName_ + " " + (info_.isLfo ? juce::String("LFO") : info_.sourceTitle);
    const auto name = [](juce::Component& c, const juce::String& text) {
        c.setTitle(text);
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(&c))
            tip->setTooltip(text);
    };
    setTitle(who);
    setTooltip("Right-click, Shift+F10 or Return for the " + who + " modulator menu");
    name(shape_, who + " shape");
    name(syncRate_, who + " sync rate");
    name(rateHz_, who + " rate");
    name(sync_, who + " sync");
}

void ModulatorRow::resized() {
    auto bounds = getLocalBounds().withTrimmedLeft(kIndent + kPadding).withTrimmedRight(kPadding);
    const int lineHeight = bounds.getHeight() / 3;
    auto title = bounds.removeFromTop(lineHeight);
    auto middle = bounds.removeFromTop(lineHeight);
    tagArea_ = title.removeFromLeft(kTagWidth);
    if (info_.isLfo) {
        shapeIcon_.setBounds(title.removeFromRight(kShapeIconWidth).reduced(0, 1));
        layoutLfoControls(middle, bounds);
    } else {
        layoutAmount(middle);
    }
    titleArea_ = title.withTrimmedLeft(kPadding);
}

// Line 2: the shape combo at the width its longest choice needs (the app's own sizing rule), then the
// rate in what is left. Line 3: Sync, then the amount. Nothing is clipped at the default column width.
void ModulatorRow::layoutLfoControls(juce::Rectangle<int> shapeLine, juce::Rectangle<int> amountLine) {
    // The rate combo's longest name ("1/128") is reserved first, so the shape never takes its room.
    const int rateFit = synth::theme::AppLookAndFeel::comboBoxWidthToFitItems(syncRate_);
    const int shapeWidth = juce::jmin(synth::theme::AppLookAndFeel::comboBoxWidthToFitItems(shape_),
                                      shapeLine.getWidth() * 3 / 5, shapeLine.getWidth() - rateFit - kGap);
    shape_.setBounds(shapeLine.removeFromLeft(shapeWidth).reduced(0, 1));
    shapeLine.removeFromLeft(kGap);
    const int rateWidth = juce::jmin(rateFit, shapeLine.getWidth());
    syncRate_.setBounds(shapeLine.withWidth(rateWidth).reduced(0, 1));
    rateTextArea_ = shapeLine.removeFromRight(kRateTextWidth);
    rateHz_.setBounds(shapeLine.reduced(0, 3));

    sync_.setBounds(amountLine.removeFromLeft(kSyncWidth).reduced(0, 1));
    amountLine.removeFromLeft(kGap);
    layoutAmount(amountLine);
}

void ModulatorRow::layoutAmount(juce::Rectangle<int> line) { amountArea_ = line; }

void ModulatorRow::setTrackColour(juce::Colour colour) {
    if (trackColour_ == colour)
        return;
    trackColour_ = colour;
    repaint();
}

void ModulatorRow::setAmount(std::optional<double> amount) {
    const auto before = getAmountText();
    amount_ = amount;
    if (getAmountText() != before)
        repaint(amountArea_);
}

juce::String ModulatorRow::getAmountText() const { return amount_.has_value() ? amountText(*amount_) : juce::String(); }

// The track's colour, pushed to a readable contrast on the row's surface; the mod-wire colour until the
// owner has said which track the row is on.
juce::Colour ModulatorRow::readableTrackColour() const {
    return readableOn(trackColour_.value_or(info_.colour), coloursFor(*this).surface);
}

void ModulatorRow::paint(juce::Graphics& g) {
    const auto colours = coloursFor(*this);
    g.fillAll(colours.surface);
    g.setColour(colours.border);
    g.drawHorizontalLine(getHeight() - 1, (float)kIndent, (float)getWidth());
    const auto accent = readableTrackColour();
    g.setColour(accent.withAlpha(0.6f));
    g.fillRect(kIndent, 0, 2, getHeight());

    g.setColour(accent);
    g.setFont(juce::Font(juce::FontOptions(kTagFontSize, juce::Font::bold)));
    g.drawText(info_.isLfo ? "LFO" : "CV", tagArea_, juce::Justification::centredLeft, false);
    g.setColour(colours.text);
    g.setFont(juce::Font(juce::FontOptions(kTitleFontSize)));
    g.drawText(info_.sourceTitle, titleArea_, juce::Justification::centredLeft, true);
    if (amount_.has_value() && !amountArea_.isEmpty()) {
        g.setFont(juce::Font(juce::FontOptions(kValueFontSize)));
        g.setColour(colours.textMuted);
        g.drawText("Amount", amountArea_, juce::Justification::centredLeft, false);
        g.setColour(accent);
        g.drawText(getAmountText(), amountArea_.withTrimmedLeft(amountArea_.getWidth() - kAmountTextWidth),
                   juce::Justification::centredRight, false);
    }
}

// The value text sits on the bars, and every Tab stop shows the shared accent ring over whatever its
// look-and-feel draws, so focus reads the same on every control in the row.
void ModulatorRow::paintOverChildren(juce::Graphics& g) {
    synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this);
    const auto text = coloursFor(*this).text;
    paintSliderValue(g, rateHz_, rateTextArea_, text);
    for (auto* child : getChildren())
        if (child->isVisible() && child != &shapeIcon_) // the shape picture takes no focus
            synth::ui::paintFocusRing(g, child->getBounds().toFloat().expanded(1.0f), *child, 3.0f);
}

} // namespace synth::ui
