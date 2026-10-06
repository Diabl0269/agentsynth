// CardLayoutControlPanel.cpp -- the per-control panel's fields, their commits and their keyboard and
// accessibility wiring. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutControlPanel.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorModel.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kPad = 12;
constexpr int kCaptionH = 16;
constexpr int kFieldH = 24;
constexpr int kSwitchH = 26;
constexpr int kGap = 10;
constexpr int kMinWidth = 260;
constexpr int kMaxWidth = 440;
constexpr int kToWidth = 22;
constexpr int kFocusTries = 60;

// A segmented switch's width for its segment texts.
int switchWidth(const juce::StringArray& names) {
    float widest = 0.0f;
    const juce::Font font{juce::FontOptions(13.0f)};
    for (const auto& name : names)
        widest = std::max(widest, font.getStringWidthFloat(name));
    return (int)widest * names.size() + 20 * names.size();
}

void styleCaption(juce::Label& label) {
    label.setFont(juce::Font(juce::FontOptions(12.0f)));
    label.setInterceptsMouseClicks(false, false);
    label.setAccessible(false);
}

} // namespace

CardLayoutControlPanel::CardLayoutControlPanel() {
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);
    for (auto* caption :
         {&showAsCaption_, &sizeCaption_, &directionCaption_, &labelCaption_, &rangeCaption_, &rangeTo_, &hint_}) {
        styleCaption(*caption);
        addAndMakeVisible(*caption);
    }
    rangeTo_.setJustificationType(juce::Justification::centred);
    styleEditor(label_, "Label", "The control's name on the card. Empty puts the parameter's own name back");
    styleEditor(minimum_, "Minimum", "The lowest value the control reaches, in the parameter's own units");
    styleEditor(maximum_, "Maximum", "The highest value the control reaches, in the parameter's own units");
    label_.setExplicitFocusOrder(4);
    minimum_.setExplicitFocusOrder(5);
    maximum_.setExplicitFocusOrder(6);
    label_.onReturnKey = [this] { commitLabel(); };
    label_.onFocusLost = [this] { commitLabel(); };
    for (auto* field : {&minimum_, &maximum_}) {
        field->onReturnKey = [this] { commitRange(); };
        field->onFocusLost = [this] { commitRange(); };
    }
    hide_.setTitle("Hide from card");
    hide_.setTooltip("Hide this control from the card. It goes to the More row");
    hide_.setExplicitFocusOrder(7);
    hide_.onClick = [this] {
        if (onHide)
            onHide();
    };
    addAndMakeVisible(hide_);
    applyColours();
}

void CardLayoutControlPanel::styleEditor(juce::TextEditor& editor, const juce::String& title,
                                         const juce::String& tooltip) {
    editor.setTitle(title);
    editor.setTooltip(tooltip);
    editor.setMultiLine(false);
    editor.setSelectAllWhenFocused(true);
    removeHiddenTabStops(editor);
    bubbleEscapeToParents(editor);
    addAndMakeVisible(editor);
}

// Show as lists the kinds only (Knob, Fader, ...): a knob's size and a fader's direction have their own rows.
void CardLayoutControlPanel::buildShowAs() {
    juce::StringArray names;
    for (auto kind : kinds_)
        names.add(widgetKindName(kind));
    showAs_ = std::make_unique<CardSegmentedSwitch>("Show as", names);
    showAs_->setTooltip("How this control is drawn on the card");
    showAs_->setExplicitFocusOrder(1);
    showAs_->onChange = [this](int index) {
        if (index >= 0 && index < (int)kinds_.size() && onShowAs)
            onShowAs(widgetForKind(kinds_[(size_t)index], options_.widget, options_.widgetChoices));
    };
    addAndMakeVisible(*showAs_);
    showAsWidth_ = std::max(showAsWidth_, switchWidth(names));
}

// Size and Direction pick between two widgets of one kind: segment 0 is `first`, segment 1 `second`.
void CardLayoutControlPanel::buildPairSwitch(std::unique_ptr<CardSegmentedSwitch>& target, const juce::String& title,
                                             const juce::String& tooltip, const juce::StringArray& values,
                                             int focusOrder, CardWidget first, CardWidget second) {
    target = std::make_unique<CardSegmentedSwitch>(title, values);
    target->setTooltip(tooltip);
    target->setExplicitFocusOrder(focusOrder);
    target->onChange = [this, first, second](int index) {
        if ((index == 0 || index == 1) && onShowAs)
            onShowAs(index == 0 ? first : second);
    };
    addChildComponent(*target);
}

void CardLayoutControlPanel::setOptions(const ControlOptions& options) {
    const auto kinds = widgetKinds(options.widgetChoices);
    const bool rebuild = (showAs_ == nullptr && kinds.size() >= 2) || kinds != kinds_;
    options_ = options;
    setTitle(options_.caption + " options");
    if (rebuild) {
        showAs_.reset();
        showAsWidth_ = switchWidth({"Vertical", "Horizontal"}); // the widest pair row
        kinds_ = kinds;
        if (kinds_.size() >= 2)
            buildShowAs();
    }
    if (size_ == nullptr) {
        buildPairSwitch(size_, "Size", "How big this knob is drawn. A large knob takes a bigger dial",
                        {"Small", "Large"}, 2, CardWidget::Knob, CardWidget::KnobLarge);
        buildPairSwitch(direction_, "Direction", "Whether this fader stands up or lies down",
                        {"Vertical", "Horizontal"}, 3, CardWidget::FaderV, CardWidget::FaderH);
    }
    if (showAs_ != nullptr)
        for (int i = 0; i < (int)kinds_.size(); ++i)
            if (kinds_[(size_t)i] == widgetKindOf(options_.widget))
                showAs_->setSelectedIndex(i, juce::dontSendNotification);
    showAsCaption_.setVisible(showAs_ != nullptr);
    const bool hasSize = offersKnobSize(options_.widgetChoices, options_.widget);
    const bool hasDirection = offersFaderDirection(options_.widgetChoices, options_.widget);
    size_->setVisible(hasSize);
    sizeCaption_.setVisible(hasSize);
    direction_->setVisible(hasDirection);
    directionCaption_.setVisible(hasDirection);
    size_->setSelectedIndex(options_.widget == CardWidget::KnobLarge ? 1 : 0, juce::dontSendNotification);
    direction_->setSelectedIndex(options_.widget == CardWidget::FaderH ? 1 : 0, juce::dontSendNotification);
    const bool hasRange = options_.fullRange.has_value();
    for (auto* c : std::initializer_list<juce::Component*>{&rangeCaption_, &rangeTo_, &minimum_, &maximum_})
        c->setVisible(hasRange);
    hint_.setVisible(false);
    fillFields();
    const int width = juce::jlimit(kMinWidth, kMaxWidth, std::max(kMinWidth, showAsWidth_ + 2 * kPad));
    setSize(width, 0);
    setSize(width, arrange());
}

void CardLayoutControlPanel::fillFields() {
    label_.setText(options_.caption, false);
    const auto& range = options_.range;
    minimum_.setText(range.has_value() ? formatRangeValue(range->getStart()) : juce::String(), false);
    maximum_.setText(range.has_value() ? formatRangeValue(range->getEnd()) : juce::String(), false);
    const auto full = options_.fullRange.value_or(juce::Range<double>());
    minimum_.setTextToShowWhenEmpty(formatRangeValue(full.getStart()), juce::Colours::grey);
    maximum_.setTextToShowWhenEmpty(formatRangeValue(full.getEnd()), juce::Colours::grey);
}

// Lays the rows out top to bottom for the current width and answers the height they need.
int CardLayoutControlPanel::arrange() {
    const int width = getWidth() - 2 * kPad;
    int y = kPad;
    if (showAs_ != nullptr) {
        showAsCaption_.setBounds(kPad, y, width, kCaptionH);
        showAs_->setBounds(kPad, y + kCaptionH, width, kSwitchH);
        y += kCaptionH + kSwitchH + kGap;
    }
    if (size_ != nullptr && (size_->isVisible() || direction_->isVisible())) {
        auto& caption = size_->isVisible() ? sizeCaption_ : directionCaption_;
        auto& pair = size_->isVisible() ? *size_ : *direction_;
        caption.setBounds(kPad, y, width, kCaptionH);
        pair.setBounds(kPad, y + kCaptionH, width, kSwitchH);
        y += kCaptionH + kSwitchH + kGap;
    }
    labelCaption_.setBounds(kPad, y, width, kCaptionH);
    label_.setBounds(kPad, y + kCaptionH, width, kFieldH);
    y += kCaptionH + kFieldH + kGap;
    if (options_.fullRange.has_value()) {
        rangeCaption_.setBounds(kPad, y, width, kCaptionH);
        const int field = (width - kToWidth) / 2;
        minimum_.setBounds(kPad, y + kCaptionH, field, kFieldH);
        rangeTo_.setBounds(kPad + field, y + kCaptionH, kToWidth, kFieldH);
        maximum_.setBounds(kPad + width - field, y + kCaptionH, field, kFieldH);
        y += kCaptionH + kFieldH;
        if (hint_.isVisible()) {
            hint_.setBounds(kPad, y + 2, width, kCaptionH);
            y += kCaptionH + 2;
        }
        y += kGap;
    }
    hide_.setBounds(kPad, y, width, kSwitchH);
    return y + kSwitchH + kPad;
}

void CardLayoutControlPanel::resized() { arrange(); }

void CardLayoutControlPanel::commitLabel() {
    if (labelOverrideFor(label_.getText(), options_.displayName) ==
        labelOverrideFor(options_.caption, options_.displayName))
        return;
    if (onLabel)
        onLabel(label_.getText());
}

// A refused entry puts the stored values back and says why; an accepted one is written, and the editor
// hands the control's new state back through setOptions.
void CardLayoutControlPanel::commitRange() {
    if (!options_.fullRange.has_value())
        return;
    const auto entry = parseRangeEntry(minimum_.getText(), maximum_.getText(), *options_.fullRange);
    const bool changed = hint_.getText().isNotEmpty() != !entry.ok;
    hint_.setText(entry.ok ? juce::String() : entry.hint, juce::dontSendNotification);
    hint_.setVisible(!entry.ok);
    if (changed)
        setSize(getWidth(), arrange());
    if (!entry.ok || entry.range == options_.range) {
        fillFields();
        return;
    }
    if (onRange)
        onRange(entry.range);
}

bool CardLayoutControlPanel::keyPressed(const juce::KeyPress& key) {
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (onRequestClose)
        onRequestClose();
    return true;
}

void CardLayoutControlPanel::applyColours() {
    const auto& colours = synth::theme::themeOf(*this).colors;
    for (auto* caption :
         {&showAsCaption_, &sizeCaption_, &directionCaption_, &labelCaption_, &rangeCaption_, &rangeTo_})
        caption->setColour(juce::Label::textColourId, colours.textMuted);
    hint_.setColour(juce::Label::textColourId, colours.warning);
}

void CardLayoutControlPanel::lookAndFeelChanged() { applyColours(); }

juce::Component* CardLayoutControlPanel::firstFocusStop() const {
    if (showAs_ != nullptr)
        return showAs_.get();
    return const_cast<juce::TextEditor*>(&label_);
}

bool CardLayoutControlPanel::focusIsInside() const {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    return focused != nullptr && (focused == this || isParentOf(focused));
}

// A CallOutBox attaches its content after construction and only becomes the key window a moment after it is
// shown, so focus is taken as soon as the window can give it.
void CardLayoutControlPanel::timerCallback() {
    auto* peer = getPeer();
    if (peer != nullptr && isShowing() && !focusIsInside()) {
        if (!peer->isFocused()) {
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->toFront(true);
        } else {
            firstFocusStop()->grabKeyboardFocus();
        }
    }
    if (focusIsInside() || ++focusTries_ > kFocusTries)
        stopTimer();
}

void CardLayoutControlPanel::parentHierarchyChanged() {
    if (getParentComponent() != nullptr && !focusIsInside()) {
        focusTries_ = 0;
        startTimerHz(30);
    }
}

void CardLayoutControlPanel::visibilityChanged() {
    if (isShowing() && !focusIsInside() && !isTimerRunning()) {
        focusTries_ = 0;
        startTimerHz(30);
    }
}

} // namespace synth::ui
