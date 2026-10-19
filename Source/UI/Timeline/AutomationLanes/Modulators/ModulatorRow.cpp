// Concern: the modulator row's construction, names, layout and paint. The edits, the live-value refresh and
// the menu live in ModulatorRowEdits.cpp; the band in the lanes region is ModulatorBand.
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

#include "Modules/LfoRateDivisions.h"
#include "UI/Graph/CardBody/CardBodySwapMotion.h"
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorAmountLane.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TrackColour.h"

namespace synth::ui {

namespace {
constexpr int kPadding = 4;
constexpr int kShapeIconWidth = 22;
constexpr int kSyncWidth = 48;
constexpr int kGap = 2;
constexpr int kAmountTextWidth = 34; // "-100%"
constexpr int kRateTextWidth = 42;   // "20.0 Hz"
constexpr int kMarkerGap = 2;        // between the marker and the name button, whose own padding makes the rest
constexpr int kNameButtonHeight = 20;
constexpr float kValueFontSize = 9.0f;
constexpr int kMinNameWidth = 40; // px of the title kept for the name when the chips want more

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

ModulatorRow::ModulatorRow(const ModulatorInfo& info, TrackHeaderHost* host, const juce::String& parameterName,
                           LaneControlSet* controls)
    : info_(info)
    , host_(host)
    , parameterName_(parameterName)
    , strip_(LaneControlKind::Mod, controls, controlActions()) {
    setComponentID("modulatorRow");
    // The row is a keyboard stop of its own, like a lane header (Up/Down walk the track list through it). A press does
    // not grab focus (it may become a drag); a plain click on the row, not on a control, takes it on release.
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    addAndMakeVisible(marker_);
    marker_.addMouseListener(this, false);
    addAndMakeVisible(nameButton_);
    nameButton_.onClick = [this] { openChangeSourcePicker(); };
    addAndMakeVisible(strip_);
    strip_.onPreferredWidthChanged = [this] { resized(); };
    strip_.onEditLookChanged = [this] { repaint(); };
    strip_.onContextMenu = [this](juce::Point<int> screen) { showMenuAt(contextMenuOptionsAtPoint(screen)); };
    if (info_.isLfo)
        initLfoControls();
    // The arrow is a Tab stop and toggles on Return/Space or a click; a right-click on it is the row's menu.
    addAndMakeVisible(foldArrow_);
    foldArrow_.onClick = [this] {
        if (onFoldRequested)
            onFoldRequested(!collapsed_);
    };
    foldArrow_.onPopupMenuRequested = [this] { showMenuAt(contextMenuOptionsAtPointer()); };
    applyNames();
    refreshValues();
}

ModulatorRow::~ModulatorRow() = default;

// Tab reaches every control; a click does not move focus off the clips, whose Cmd+X/C/V the app
// routes by where real focus sits (same rule as the lane header).
void ModulatorRow::initLfoControls() {
    if (lfoControlsBuilt_)
        return;
    lfoControlsBuilt_ = true;
    const juce::StringArray shapes{"Sine", "Triangle", "Sawtooth", "Square", "S&H", "Custom"};
    for (int i = 0; i < shapes.size(); ++i)
        shape_.addItem(shapes[i], i + 1);
    syncRate_.setChoices(synth::lfoRateDivisions());

    rateHz_.setSliderStyle(juce::Slider::LinearBar);
    rateHz_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    rateHz_.setNormalisableRange(juce::NormalisableRange<double>(0.01, 20.0, 0.01, 0.5));
    rateHz_.textFromValueFunction = [](double v) { return juce::String(v, v < 10.0 ? 2 : 1) + " Hz"; };
    sync_.setButtonText("Sync");

    for (juce::Component* c : std::initializer_list<juce::Component*>{&shape_, &syncRate_, &rateHz_, &sync_}) {
        c->setMouseClickGrabsKeyboardFocus(false);
        c->setWantsKeyboardFocus(true);
        c->addKeyListener(this);
        addChildComponent(c);
    }
    shape_.setVisible(true);
    sync_.setVisible(true);

    // Every edit names the LFO the row shows at the time: setRouting may re-bind the row to another one.
    shape_.onChange = [this] {
        shapeIcon_.setShape(shape_.getSelectedId() - 1);
        edit(info_.sourceUuid, "shape", (float)(shape_.getSelectedId() - 1), ParameterEditPhase::Once);
    };
    syncRate_.onChange = [this] {
        edit(info_.sourceUuid, "rateSync", (float)(syncRate_.getSelectedId() - 1), ParameterEditPhase::Once);
    };
    sync_.onClick = [this] {
        edit(info_.sourceUuid, "mode", sync_.getToggleState() ? 1.0f : 0.0f, ParameterEditPhase::Once);
        refreshValues(); // swaps the sync-rate combo and the Hz bar
    };
    wireDragEdits(rateHz_, "rateHz", [](double v) { return (float)v; });

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

// A routing replaced in place (Change source, Change parameter, the undo of either) keeps its row, so focus, the fold
// and the slot stay; only what names or drives the source changes. The parts that show the source (the name, the shape
// picture, the LFO controls) are pictured first, then the row re-binds and re-lays out at once, and the pictures shrink
// away before the new parts grow in: animation.md "Controls swapping in place". It lands at once off screen, under
// Reduce Motion or with Animations off.
void ModulatorRow::setRouting(const ModulatorInfo& info, const juce::String& parameterName) {
    if (info.key() == info_.key()) {
        setInfo(info, parameterName);
        return;
    }
    finishSwapMotion();
    const bool animate = canAnimateSwap() && !getLocalBounds().isEmpty();
    std::vector<synth::SwapMotion::Departure> leaving;
    if (animate)
        for (auto* part : sourceParts())
            if (part->isVisible() && !part->getBounds().isEmpty())
                leaving.push_back({createComponentSnapshot(part->getBounds(), true, 2.0f), part->getBounds()});
    info_ = info;
    parameterName_ = parameterName;
    if (info_.isLfo)
        initLfoControls();
    applySourceKind();
    applyNames();
    refreshValues();
    resized();
    repaint();
    if (!animate)
        return;
    std::vector<synth::SwapMotion::Arrival> arriving;
    for (auto* part : sourceParts())
        if (part->isVisible() && !part->getBounds().isEmpty())
            arriving.push_back({part, nullptr, part->getBounds()});
    swap_ = std::make_unique<synth::SwapMotion>(*this, std::move(leaving), std::move(arriving));
    swap_->run([this] {
        applySourceKind(); // a fold or a mode flip during the motion decides what shows now
        refreshValues();
    });
}

// The LFO controls and the shape picture show for an LFO source only, and not on a folded row (refreshValues picks the
// sync-rate combo or the Hz bar).
void ModulatorRow::applySourceKind() {
    if (!lfoControlsBuilt_)
        return;
    const bool show = info_.isLfo && !compact_;
    setPartVisible(shape_, show);
    setPartVisible(sync_, show);
    setPartVisible(shapeIcon_, show);
    if (!info_.isLfo) {
        setPartVisible(syncRate_, false);
        setPartVisible(rateHz_, false);
    }
}

// A part a running swap holds hidden (the leaving pictures are still shrinking) stays hidden; the swap shows it.
void ModulatorRow::setPartVisible(juce::Component& part, bool visible) {
    if (swap_ != nullptr && swap_->holdsHidden(part))
        return;
    part.setVisible(visible);
}

std::vector<juce::Component*> ModulatorRow::sourceParts() {
    std::vector<juce::Component*> parts{&nameButton_};
    if (lfoControlsBuilt_)
        parts.insert(parts.end(), {&shapeIcon_, &shape_, &syncRate_, &rateHz_, &sync_});
    return parts;
}

bool ModulatorRow::canAnimateSwap() const {
    return (forceSwapForTest_ || isShowing()) && !prefersReducedMotion() && !animationsOff();
}

void ModulatorRow::finishSwapMotion() {
    if (swap_ != nullptr)
        swap_->finish();
}

bool ModulatorRow::isSwapMotionRunning() const { return swap_ != nullptr && swap_->isRunning(); }

void ModulatorRow::stepSwapMotionForTest(double elapsedMs) {
    if (swap_ != nullptr)
        swap_->setElapsed(elapsedMs);
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
    setTitle(info_.sourceTitle + " mod lane on " + parameterName_);
    setTooltip("Right-click, Shift+F10 or Return for the " + who +
               " modulator menu. Drag to reorder or move to another lane");
    const auto kind = info_.isLfo ? juce::String("an LFO") : info_.isEnvelope ? "an envelope" : "a modulator";
    marker_.setTooltip("Mod lane: " + kind + " moves this parameter");
    nameButton_.setName(info_.sourceTitle);
    const bool canChange = host_ != nullptr && host_->canChangeModulatorSource(info_);
    nameButton_.setActionText("Change source of " + info_.sourceTitle + " on " + parameterName_,
                              canChange ? "Pick a different source" : "This source cannot be swapped here");
    nameButton_.setEnabled(canChange);
    strip_.refresh();
    name(shape_, who + " shape");
    name(syncRate_, who + " sync rate");
    name(rateHz_, who + " rate");
    name(sync_, who + " sync");
    const auto fold = info_.sourceTitle + (parameterName_.isNotEmpty() ? " on " + parameterName_ : juce::String());
    foldArrow_.setStateWithText(!collapsed_, (collapsed_ ? "Expand " : "Collapse ") + fold,
                                (collapsed_ ? "Expand " : "Collapse ") + fold +
                                    (collapsed_ ? " (Right arrow on the row)" : " (Left arrow on the row)"));
}

void ModulatorRow::setCollapsed(bool collapsed) {
    if (collapsed_ == collapsed)
        return;
    collapsed_ = collapsed;
    applyNames();
}

void ModulatorRow::setCompact(bool compact) {
    if (compact_ == compact)
        return;
    compact_ = compact;
    applyCompact();
}

void ModulatorRow::setTitleLineHeight(int height) {
    if (titleLineHeight_ == height)
        return;
    titleLineHeight_ = height;
    resized();
}

// A folded row shows its title and amount only. Focus inside a control that is about to hide moves to the row, so the
// keyboard keeps its place.
void ModulatorRow::applyCompact() {
    if (info_.isLfo) {
        if (compact_) {
            auto* focused = juce::Component::getCurrentlyFocusedComponent();
            if (focused != nullptr && focused != this && isParentOf(focused) && focused != &foldArrow_)
                grabKeyboardFocus();
        }
        applySourceKind();
        refreshValues(); // the sync-rate combo or the Hz bar, whichever the mode shows
    }
    resized();
    repaint();
}

void ModulatorRow::resized() {
    auto bounds = getLocalBounds().withTrimmedLeft(kIndent + kStripeWidth + kPadding).withTrimmedRight(kPadding);
    const int lineHeight = titleLineHeight_ > 0 ? titleLineHeight_ : bounds.getHeight() / 3;
    auto title = bounds.removeFromTop(lineHeight);
    auto middle = bounds.removeFromTop(lineHeight);
    bounds.setHeight(juce::jmax(bounds.getHeight(), lineHeight)); // while the row tweens, the last line is clipped
    // The fold arrow sits in the gutter left of the stripe, so the marker and the name line up with a lane header's.
    const juce::Rectangle<int> arrow(0, title.getY(), kIndent, title.getHeight());
    foldArrow_.setBounds(arrow.withSizeKeepingCentre(juce::jmin(arrow.getWidth(), TrackFoldArrow::kSize),
                                                     juce::jmin(arrow.getHeight(), TrackFoldArrow::kSize)));
    marker_.setBounds(title.removeFromLeft(LaneMarkerIcon::kSize)
                          .withSizeKeepingCentre(LaneMarkerIcon::kSize, LaneMarkerIcon::kSize));
    title.removeFromLeft(kMarkerGap);
    compactAmountArea_ = {};
    if (compact_) {
        if (amount_.has_value())
            compactAmountArea_ = title.removeFromRight(kAmountTextWidth);
        amountArea_ = {};
    } else if (info_.isLfo) {
        shapeIcon_.setBounds(title.removeFromRight(kShapeIconWidth).reduced(0, 1));
        layoutLfoControls(middle, bounds);
    } else {
        layoutAmount(middle);
    }
    // The chips take the title line's right end (after the shape picture and the folded amount), leaving the name
    // some room.
    const int chipSize = juce::jmin(LaneControlChip::kSize, title.getHeight());
    strip_.setChipSize(chipSize);
    const int stripWidth = juce::jmin(strip_.preferredWidth(), juce::jmax(chipSize, title.getWidth() - kMinNameWidth));
    strip_.setBounds(title.removeFromRight(stripWidth));
    const auto button = title.withWidth(juce::jmin(nameButton_.getPreferredWidth(), title.getWidth()));
    nameButton_.setBounds(
        button.withSizeKeepingCentre(button.getWidth(), juce::jmin(button.getHeight(), kNameButtonHeight)));
    strip_.placeTray(getBounds());
}

void ModulatorRow::moved() {
    strip_.placeTray(getBounds());
    strip_.repositionRow();
}

// Line 2: the shape combo at the width its longest choice needs (the app's own sizing rule), then the
// rate in what is left. Line 3: Sync, then the amount. Nothing is clipped at the default column width.
void ModulatorRow::layoutLfoControls(juce::Rectangle<int> shapeLine, juce::Rectangle<int> amountLine) {
    // The rate combo's longest name ("1/128D") is reserved first, so the shape never takes its room.
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
    const bool hadAmount = amount_.has_value();
    amount_ = amount;
    if (compact_ && hadAmount != amount_.has_value())
        resized(); // the folded row's title line gives the readout room only while there is one
    if (getAmountText() != before)
        repaint();
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
    g.setColour(accent.withAlpha(0.45f));
    g.fillRect(kIndent, 0, kStripeWidth, getHeight());

    if (compact_ && amount_.has_value() && !compactAmountArea_.isEmpty()) {
        g.setFont(juce::Font(juce::FontOptions(kValueFontSize)));
        g.setColour(accent);
        g.drawText(getAmountText(), compactAmountArea_, juce::Justification::centredRight, false);
    } else if (amount_.has_value() && !amountArea_.isEmpty()) {
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
    if (lift_ > 0.0f) {
        g.setColour(juce::Colours::white.withAlpha(0.05f * lift_));
        g.fillRect(getLocalBounds());
        g.setColour(synth::theme::themeOf(*this).colors.accent.withMultipliedAlpha(lift_));
        g.drawRect(getLocalBounds(), 1);
    }
    if (const float edit = strip_.editAmount(); edit > 0.0f) {
        const auto outlineAccent = synth::theme::themeOf(*this).colors.accent;
        const auto outline = getLocalBounds().withTrimmedLeft(kIndent);
        g.setColour(outlineAccent.withMultipliedAlpha(0.22f * edit));
        g.drawRect(outline.reduced(1), 2);
        g.setColour(outlineAccent.withMultipliedAlpha(edit));
        g.drawRect(outline, 1);
    }
    const auto text = coloursFor(*this).text;
    paintSliderValue(g, rateHz_, rateTextArea_, text);
    for (auto* child : getChildren())
        if (child->isVisible() && child != &shapeIcon_ && child != &foldArrow_ && child != &nameButton_ &&
            child != &marker_ && child != &strip_) // the arrow, the name button and the chips paint their own ring
            synth::ui::paintFocusRing(g, child->getBounds().toFloat().expanded(1.0f), *child, 3.0f);
}

} // namespace synth::ui
