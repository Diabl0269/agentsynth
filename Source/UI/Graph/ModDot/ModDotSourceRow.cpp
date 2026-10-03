#include "ModDotSourceRow.h"

#include "ModDotPalette.h"
#include "UI/Layout/FocusRing.h"

namespace synth::ui {

namespace {
constexpr int kPad = 8;
constexpr int kSwatch = 10;
constexpr int kNameWidth = 62;
constexpr int kAmountWidth = 44;
constexpr float kFont = 12.5f;
} // namespace

// The signed amount as a button: a click or Return opens the inline editor.
class ModDotSourceRow::AmountButton final : public juce::Button {
public:
    AmountButton()
        : juce::Button({}) {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::IBeamCursor);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
            triggerClick();
            return true;
        }
        return false;
    }
    void paintButton(juce::Graphics& g, bool over, bool) override {
        const auto p = modDotPaletteFor(*this);
        if (over) {
            g.setColour(p.hover);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
        }
        g.setColour(p.text);
        g.setFont(p.mono(12.0f));
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centredRight);
        paintFocusRing(g, getLocalBounds().toFloat(), *this, 4.0f);
    }
};

ModDotSourceRow::ModDotSourceRow(const KnobModSource& source, const juce::String& paramName)
    : source_(source)
    , paramName_(paramName)
    , hover_(*this)
    , amountButton_(std::make_unique<AmountButton>())
    , timelineButton_(ModDotGlyph::Timeline, {}, {})
    , removeButton_(ModDotGlyph::Trash, {}, {}) {
    addAndMakeVisible(bar_);
    addAndMakeVisible(*amountButton_);
    addAndMakeVisible(timelineButton_);
    addAndMakeVisible(removeButton_);
    addMouseListener(this, true); // hover over any child lights the row

    bar_.onFocused = [this] {
        if (onSelect)
            onSelect(*this);
    };
    bar_.onGestureBegin = [this] {
        if (onGestureBegin)
            onGestureBegin(*this);
    };
    bar_.onValueChanged = [this](float v) {
        amountButton_->setButtonText(ModDotAmountBar::percentText(v));
        if (onAmountDragged)
            onAmountDragged(*this, v);
    };
    bar_.onGestureEnd = [this] {
        if (onGestureEnd)
            onGestureEnd(*this);
    };
    amountButton_->onClick = [this] { beginAmountEdit(); };
    timelineButton_.onClick = [this] {
        if (onShowInTimeline)
            onShowInTimeline(*this);
    };
    removeButton_.onClick = [this] {
        if (onRemove)
            onRemove(*this);
    };
    bar_.setValue(source.amount);
    amountButton_->setButtonText(ModDotAmountBar::percentText(source.amount));
    applyNames();
}

ModDotSourceRow::~ModDotSourceRow() = default;

juce::Button& ModDotSourceRow::amountButton() noexcept { return *amountButton_; }

void ModDotSourceRow::applyNames() {
    const auto& name = source_.sourceName;
    setTitle(name + ", " + ModDotAmountBar::percentText(source_.amount));
    bar_.setTitle(name + " amount");
    bar_.setTooltip("Drag to change how strongly " + name + " moves " + paramName_);
    amountButton_->setTitle(name + " amount value");
    amountButton_->setTooltip("Click to type how strongly " + name + " moves " + paramName_);
    timelineButton_.setTitle("Show " + name + " in timeline");
    timelineButton_.setTooltip("Show " + name + " in timeline");
    removeButton_.setTitle("Remove " + name);
    removeButton_.setTooltip("Remove " + name);
}

void ModDotSourceRow::update(const KnobModSource& source) {
    const bool renamed = source.sourceName != source_.sourceName;
    const bool amountChanged = source.amount != source_.amount;
    source_ = source;
    if (!bar_.isMouseButtonDown()) {
        bar_.setValue(source.amount);
        amountButton_->setButtonText(ModDotAmountBar::percentText(source.amount));
    }
    if (renamed || amountChanged)
        applyNames();
    repaint();
}

juce::Colour ModDotSourceRow::swatchColour() const { return modDotPaletteFor(*this).swatchFor(bar_.getValue()); }

void ModDotSourceRow::setSelected(bool selected) {
    if (selected_ == selected)
        return;
    selected_ = selected;
    repaint();
}

void ModDotSourceRow::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, (float)getWidth(), (float)kHeight).reduced(2.0f, 1.0f);
    if (hover_.value() > 0.0f) {
        g.setColour(p.hover.withAlpha(hover_.value()));
        g.fillRoundedRectangle(area, 6.0f);
    }
    if (selected_) {
        g.setColour(p.accent.withAlpha(0.10f));
        g.fillRoundedRectangle(area, 6.0f);
    }
    g.setColour(p.swatchFor(bar_.getValue()));
    g.fillEllipse(swatchArea_.toFloat());
    g.setColour(p.text);
    g.setFont(juce::Font(juce::FontOptions(kFont)));
    g.drawText(source_.sourceName, nameArea_, juce::Justification::centredLeft, true);
}

void ModDotSourceRow::resized() {
    // Laid out for the full row height whatever height the page has given it: a row growing in or shrinking out
    // is revealed and hidden, never squashed.
    auto r = juce::Rectangle<int>(0, 0, getWidth(), kHeight).reduced(kPad - 2, 0);
    removeButton_.setBounds(r.removeFromRight(ModDotGlyphButton::kSize)
                                .withSizeKeepingCentre(ModDotGlyphButton::kSize, ModDotGlyphButton::kSize));
    timelineButton_.setBounds(r.removeFromRight(ModDotGlyphButton::kSize));
    amountButton_->setBounds(r.removeFromRight(kAmountWidth).withSizeKeepingCentre(kAmountWidth, 20));
    swatchArea_ = r.removeFromLeft(kSwatch).withSizeKeepingCentre(kSwatch, kSwatch);
    r.removeFromLeft(6);
    nameArea_ = r.removeFromLeft(kNameWidth);
    r.removeFromLeft(4);
    bar_.setBounds(r.withSizeKeepingCentre(r.getWidth() - 4, 22));
    if (editor_ != nullptr)
        editor_->setBounds(amountButton_->getBounds());
}

void ModDotSourceRow::mouseEnter(const juce::MouseEvent&) { hover_.setHovered(isMouseOver(true)); }
void ModDotSourceRow::mouseExit(const juce::MouseEvent&) { hover_.setHovered(isMouseOver(true)); }

void ModDotSourceRow::mouseDown(const juce::MouseEvent& e) {
    if (e.eventComponent == this && onSelect)
        onSelect(*this);
}

bool ModDotSourceRow::keyPressed(const juce::KeyPress& key) {
    if (editor_ == nullptr && onNavigate &&
        (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey))) {
        onNavigate(*this, key.isKeyCode(juce::KeyPress::upKey) ? -1 : 1);
        return true;
    }
    return false;
}

void ModDotSourceRow::beginAmountEdit() {
    if (editor_ != nullptr)
        return;
    if (onSelect)
        onSelect(*this);
    const auto p = modDotPaletteFor(*this);
    editor_ = std::make_unique<NavigationSearchField>();
    editor_->setTitle(source_.sourceName + " amount value");
    editor_->setMultiLine(false);
    editor_->setJustification(juce::Justification::centredRight);
    editor_->setFont(p.mono(12.0f));
    editor_->setBorder(juce::BorderSize<int>(0, 2, 0, 2));
    editor_->setColour(juce::TextEditor::backgroundColourId, p.field);
    editor_->setColour(juce::TextEditor::textColourId, p.text);
    editor_->setColour(juce::TextEditor::outlineColourId, p.border);
    editor_->setColour(juce::TextEditor::focusedOutlineColourId, p.accent);
    editor_->setInputRestrictions(5, "0123456789.-+%");
    editor_->setText(juce::String(juce::roundToInt(bar_.getValue() * 100.0f)), false);
    editor_->onNavigationKey = [this](const juce::KeyPress& key) {
        if (key == juce::KeyPress::returnKey) {
            endAmountEdit(true);
            return true;
        }
        if (key == juce::KeyPress::escapeKey) {
            endAmountEdit(false);
            return true;
        }
        return false;
    };
    editor_->onFocusLost = [this] {
        juce::Component::SafePointer<ModDotSourceRow> self(this);
        juce::MessageManager::callAsync([self] {
            if (self != nullptr && self->editor_ != nullptr && !self->editor_->hasKeyboardFocus(true))
                self->endAmountEdit(true);
        });
    };
    addAndMakeVisible(*editor_);
    editor_->setBounds(amountButton_->getBounds());
    editor_->grabKeyboardFocus();
    editor_->selectAll();
}

void ModDotSourceRow::endAmountEdit(bool apply) {
    if (editor_ == nullptr)
        return;
    const auto text = editor_->getText().retainCharacters("0123456789.-+");
    // Kept alive (hidden) until the next edit: this runs inside the editor's own key handler.
    editor_->onFocusLost = nullptr;
    removeChildComponent(editor_.get());
    retired_ = std::move(editor_);
    // Focus goes back to the amount button once the editor is gone.
    amountButton_->grabKeyboardFocus();
    if (apply && text.containsAnyOf("0123456789") && onAmountTyped)
        onAmountTyped(*this, juce::jlimit(-1.0f, 1.0f, text.getFloatValue() / 100.0f));
}

} // namespace synth::ui
