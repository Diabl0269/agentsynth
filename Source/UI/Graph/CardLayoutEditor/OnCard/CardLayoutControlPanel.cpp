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
    rangeFade_.onFrame = [this] { setSize(getWidth(), arrange()); };
    hintFade_.onFrame = [this] { setSize(getWidth(), arrange()); };
    hintFade_.onHidden = [this] {
        if (!hintFade_.isShown())
            hint_.setText({}, juce::dontSendNotification);
    };
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
        sizeFade_ = std::make_unique<synth::ui::FadeVisibility>(
            std::initializer_list<juce::Component*>{&sizeCaption_, size_.get()});
        directionFade_ = std::make_unique<synth::ui::FadeVisibility>(
            std::initializer_list<juce::Component*>{&directionCaption_, direction_.get()});
        sizeFade_->onFrame = [this] { setSize(getWidth(), arrange()); };
        directionFade_->onFrame = [this] { setSize(getWidth(), arrange()); };
        sizeFade_->setShown(false);
        directionFade_->setShown(false);
    }
    if (showAs_ != nullptr)
        for (int i = 0; i < (int)kinds_.size(); ++i)
            if (kinds_[(size_t)i] == widgetKindOf(options_.widget))
                showAs_->setSelectedIndex(i, juce::dontSendNotification);
    showAsCaption_.setVisible(showAs_ != nullptr);
    const bool hasSize = offersKnobSize(options_.widgetChoices, options_.widget);
    const bool hasDirection = offersFaderDirection(options_.widgetChoices, options_.widget);
    sizeFade_->setShown(hasSize);
    directionFade_->setShown(hasDirection);
    size_->setSelectedIndex(options_.widget == CardWidget::KnobLarge ? 1 : 0, juce::dontSendNotification);
    direction_->setSelectedIndex(options_.widget == CardWidget::FaderH ? 1 : 0, juce::dontSendNotification);
    const bool hasRange = options_.fullRange.has_value();
    rangeFade_.setShown(hasRange);
    hintFade_.setShown(false);
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
    if (sizeFade_ != nullptr) {
        // Size and Direction share one slot, so a swap between them keeps it; a lone arrival or exit grows or
        // shrinks it with the fade (the rows are squeezed, not moved) and the rows below slide.
        const int full = kCaptionH + kSwitchH;
        const float pairShown = std::min(1.0f, sizeFade_->progress() + directionFade_->progress());
        const int slot = scaled(full, pairShown);
        squeeze(sizeCaption_, kPad, y, width, 0, kCaptionH, slot);
        squeeze(directionCaption_, kPad, y, width, 0, kCaptionH, slot);
        squeeze(*size_, kPad, y, width, kCaptionH, kSwitchH, slot);
        squeeze(*direction_, kPad, y, width, kCaptionH, kSwitchH, slot);
        y += slot + scaled(kGap, pairShown);
    }
    labelCaption_.setBounds(kPad, y, width, kCaptionH);
    label_.setBounds(kPad, y + kCaptionH, width, kFieldH);
    y += kCaptionH + kFieldH + kGap;
    {
        const float shown = rangeFade_.progress();
        const int slot = scaled(kCaptionH + kFieldH, shown);
        const int field = (width - kToWidth) / 2;
        squeeze(rangeCaption_, kPad, y, width, 0, kCaptionH, slot);
        squeeze(minimum_, kPad, y, field, kCaptionH, kFieldH, slot);
        squeeze(rangeTo_, kPad + field, y, kToWidth, kCaptionH, kFieldH, slot);
        squeeze(maximum_, kPad + width - field, y, field, kCaptionH, kFieldH, slot);
        y += slot;
        const int hintSlot = scaled(kCaptionH + 2, hintFade_.progress());
        squeeze(hint_, kPad, y, width, 2, kCaptionH, hintSlot);
        y += hintSlot + scaled(kGap, shown);
    }
    hide_.setBounds(kPad, y, width, kSwitchH);
    return y + kSwitchH + kPad;
}

// `value` times a fade's progress, in whole pixels.
int CardLayoutControlPanel::scaled(int value, float progress) { return juce::roundToInt((float)value * progress); }

// Puts a row's component at `offset` inside a slot of `slotHeight` starting at `top`, cut off where the slot ends,
// so a row that is fading in or out is squeezed into the room it has instead of overlapping its neighbours.
void CardLayoutControlPanel::squeeze(juce::Component& c, int x, int top, int w, int offset, int height,
                                     int slotHeight) {
    c.setBounds(x, top + offset, w, juce::jlimit(0, height, slotHeight - offset));
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
    // The words stay until the fade-out ends (onHidden clears them); off screen the fade lands at once.
    if (!entry.ok)
        hint_.setText(entry.hint, juce::dontSendNotification);
    hintFade_.setShown(!entry.ok);
    if (entry.ok && !hintFade_.isFading())
        hint_.setText({}, juce::dontSendNotification);
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
