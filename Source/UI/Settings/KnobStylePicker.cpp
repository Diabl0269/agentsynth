#include "KnobStylePicker.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeelFader.h"
#include "UI/Theme/AppLookAndFeel/KnobPainter.h"
#include <cmath>

namespace synth::ui {

using synth::theme::KnobStyle;

namespace {
constexpr float kKnobPreviewSize = 44.0f;
constexpr float kPreviewPosition = 0.62f;
constexpr int kButtonMaxWidth = 76;
constexpr int kButtonHeight = 76;
constexpr int kFaderButtonHeight = 54;
constexpr int kRowLabelWidth = 56;
constexpr int kRowGap = 6;
constexpr int kFaderPreviewWidth = 52;
constexpr int kFaderPreviewHeight = 22;
constexpr float kLabelHeight = 12.0f;
constexpr float kCornerRadius = 8.0f;
} // namespace

// One preview: a knob or a fader painted in its style above the style's label; the picked one gets an accent
// outline and wash.
class KnobStylePicker::StyleButton : public juce::Button {
public:
    // `style` null is the Follow option.
    StyleButton(Kind kind, std::optional<KnobStyle> style, KnobStylePicker& owner)
        : juce::Button(style ? synth::theme::knobStyleLabel(*style) : "Follow")
        , kind_(kind)
        , style_(style)
        , owner_(owner) {
        setClickingTogglesState(true);
        setRadioGroupId(kind == Kind::Knob ? kKnobRadioGroup : kFaderRadioGroup);
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        refreshTexts();
    }

    void refreshTexts() {
        const juce::String noun = kind_ == Kind::Knob ? "knob" : "fader";
        const bool override_ = owner_.mode_ == Mode::Override;
        if (!style_) {
            setTitle("Follow " + noun + " style" + (override_ ? " for " + owner_.where_ : juce::String()));
            setTooltip(owner_.followTooltip_);
            return;
        }
        const juce::String label = synth::theme::knobStyleLabel(*style_);
        setTitle(label + " " + noun + " style" + (override_ ? " for " + owner_.where_ : juce::String()));
        setTooltip(override_ ? "Use the " + label + " look for " + noun + "s on " + owner_.where_
                             : "Use the " + label + " look for every " + noun);
    }

    std::optional<KnobStyle> style() const noexcept { return style_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto& theme = owner_.theme_;
        const auto& c = theme.colors;
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        const bool selected = getToggleState();

        if (selected) {
            g.setColour(c.accent.withAlpha(0.14f));
            g.fillRoundedRectangle(area, kCornerRadius);
        } else if (highlighted || down) {
            g.setColour(c.textPrimary.withAlpha(0.06f));
            g.fillRoundedRectangle(area, kCornerRadius);
        }
        g.setColour(selected ? c.accent : c.border);
        g.drawRoundedRectangle(area, kCornerRadius, selected ? 2.0f : theme.metrics.borderWidth);

        const auto colour = owner_.appearance_.colourByFamily ? c.hueAmber : c.accent;
        if (!style_) {
            g.setColour(selected ? c.textPrimary : c.textMuted);
            g.setFont(synth::theme::AppLookAndFeel::uiFont(kLabelHeight));
            g.drawText("Follow", area, juce::Justification::centred);
            synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this, kCornerRadius);
            return;
        }
        if (kind_ == Kind::Knob) {
            const auto knob = juce::Rectangle<float>(kKnobPreviewSize, kKnobPreviewSize)
                                  .withCentre({area.getCentreX(), area.getY() + 8.0f + kKnobPreviewSize * 0.5f});
            synth::theme::paintKnob(g, theme, *style_, knob, kPreviewPosition, colour);
        } else {
            paintFaderPreview(g, area, colour);
        }

        g.setColour(selected ? c.textPrimary : c.textMuted);
        g.setFont(synth::theme::AppLookAndFeel::uiFont(kLabelHeight));
        g.drawText(synth::theme::knobStyleLabel(*style_),
                   area.withTrimmedTop(area.getHeight() - 22.0f).withHeight(16.0f), juce::Justification::centred);

        synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this, kCornerRadius);
    }

    // A medium horizontal fader at kPreviewPosition, painted by the painter every linear slider uses.
    void paintFaderPreview(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) {
        const juce::Point<int> size{kFaderPreviewWidth, kFaderPreviewHeight};
        const int thumb = synth::theme::fader::thumbRadiusFor(false, size.x, size.y);
        const juce::Rectangle<int> travel{thumb, 0, size.x - 2 * thumb, size.y};
        const float pos = (float)travel.getX() + kPreviewPosition * (float)travel.getWidth();
        synth::theme::fader::State state;
        state.style = *style_;
        state.valueColour = colour;
        const auto origin = juce::Point<int>((int)std::round(area.getCentreX()) - size.x / 2, (int)area.getY() + 6);
        juce::Graphics::ScopedSaveState save(g);
        g.setOrigin(origin);
        synth::theme::fader::paint(g, owner_.theme_, travel, size, pos, false, state);
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key.getModifiers().isAnyModifierKeyDown())
            return false;
        if (key == juce::KeyPress::returnKey) {
            triggerClick();
            return true;
        }
        const int step = key == juce::KeyPress::rightKey ? 1 : (key == juce::KeyPress::leftKey ? -1 : 0);
        if (step == 0)
            return false;
        const int next = (style_ ? (int)*style_ : -1) + step;
        if (auto* target = owner_.buttonAt(kind_, next))
            target->grabKeyboardFocus();
        return true; // clamped at the ends
    }

    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    static constexpr int kKnobRadioGroup = 0x4b6e; // arbitrary, unique among the Settings window's groups
    static constexpr int kFaderRadioGroup = 0x4b6f;
    Kind kind_;
    std::optional<KnobStyle> style_;
    KnobStylePicker& owner_;
};

KnobStylePicker::KnobStylePicker(Mode mode)
    : mode_(mode) {
    if (mode_ == Mode::Override) {
        followTooltip_ = "Use the style from Settings > Appearance > Controls";
        for (const auto kind : {Kind::Knob, Kind::Fader}) {
            auto& follow = kind == Kind::Knob ? knobFollow_ : faderFollow_;
            follow = std::make_unique<StyleButton>(kind, std::nullopt, *this);
            follow->onClick = [this, kind] { styleChosen(kind, -1); };
            addAndMakeVisible(*follow);
        }
    }
    for (const auto kind : {Kind::Knob, Kind::Fader}) {
        auto& row = kind == Kind::Knob ? buttons_ : faderButtons_;
        for (int i = 0; i < synth::theme::kKnobStyleCount; ++i) {
            auto button = std::make_unique<StyleButton>(kind, (KnobStyle)i, *this);
            button->onClick = [this, kind, i] { styleChosen(kind, i); };
            addAndMakeVisible(*button);
            row[(size_t)i] = std::move(button);
        }
    }
    if (mode_ == Mode::Override) {
        setToggles(Kind::Knob, -1);
        setToggles(Kind::Fader, -1);
    } else {
        buttons_[(size_t)appearance_.style]->setToggleState(true, juce::dontSendNotification);
        faderButtons_[(size_t)appearance_.faderStyle]->setToggleState(true, juce::dontSendNotification);
    }

    for (auto* label : {&knobRowLabel_, &faderRowLabel_}) {
        label->setJustificationType(juce::Justification::centredLeft);
        label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*label);
    }

    addChildComponent(familyToggle_);
    familyToggle_.setVisible(mode_ == Mode::Settings);
    familyToggle_.setToggleState(appearance_.colourByFamily, juce::dontSendNotification);
    familyToggle_.setTitle("Colour controls by module family");
    familyToggle_.setTooltip("Tint each module card's knobs and faders with its family colour instead of the accent");
    familyToggle_.onClick = [this] {
        appearance_.colourByFamily = familyToggle_.getToggleState();
        refreshPreviewColour();
        if (onChanged)
            onChanged(appearance_);
    };
}

KnobStylePicker::~KnobStylePicker() = default;

KnobStylePicker::StyleButton* KnobStylePicker::buttonAt(Kind kind, int index) const {
    if (index == -1)
        return (kind == Kind::Knob ? knobFollow_ : faderFollow_).get();
    if (index < 0 || index >= synth::theme::kKnobStyleCount)
        return nullptr;
    return (kind == Kind::Knob ? buttons_ : faderButtons_)[(size_t)index].get();
}

void KnobStylePicker::setToggles(Kind kind, int index) {
    if (auto* button = buttonAt(kind, index))
        button->setToggleState(true, juce::dontSendNotification);
}

void KnobStylePicker::styleChosen(Kind kind, int index) {
    // Picking a preview also clicks the one it replaces (switched off); only the newly on one is a choice.
    if (!buttonAt(kind, index)->getToggleState())
        return;
    if (mode_ == Mode::Override) {
        auto& chosen = kind == Kind::Knob ? knobOverride_ : faderOverride_;
        chosen = index < 0 ? std::nullopt : std::optional<KnobStyle>((KnobStyle)index);
        if (onOverrideChanged)
            onOverrideChanged(knobOverride_, faderOverride_);
        return;
    }
    (kind == Kind::Knob ? appearance_.style : appearance_.faderStyle) = (KnobStyle)index;
    if (onChanged)
        onChanged(appearance_);
}

void KnobStylePicker::setOverrideScope(const juce::String& where, const juce::String& followTooltip) {
    where_ = where;
    followTooltip_ = followTooltip;
    refreshButtonTexts();
}

void KnobStylePicker::refreshButtonTexts() {
    for (auto* b : {knobFollow_.get(), faderFollow_.get()})
        if (b != nullptr)
            b->refreshTexts();
    for (auto* row : {&buttons_, &faderButtons_})
        for (auto& b : *row)
            b->refreshTexts();
}

void KnobStylePicker::setOverride(std::optional<KnobStyle> knob, std::optional<KnobStyle> fader) {
    knobOverride_ = knob;
    faderOverride_ = fader;
    setToggles(Kind::Knob, knob ? (int)*knob : -1);
    setToggles(Kind::Fader, fader ? (int)*fader : -1);
    refreshPreviewColour();
}

void KnobStylePicker::setAppearance(const synth::theme::KnobAppearance& appearance) {
    appearance_ = appearance;
    buttons_[(size_t)appearance_.style]->setToggleState(true, juce::dontSendNotification);
    faderButtons_[(size_t)appearance_.faderStyle]->setToggleState(true, juce::dontSendNotification);
    familyToggle_.setToggleState(appearance_.colourByFamily, juce::dontSendNotification);
    refreshPreviewColour();
}

void KnobStylePicker::setTheme(const synth::theme::Theme& theme) {
    theme_ = theme;
    for (auto* label : {&knobRowLabel_, &faderRowLabel_})
        label->setColour(juce::Label::textColourId, theme_.colors.textPrimary);
    refreshPreviewColour();
}

void KnobStylePicker::refreshPreviewColour() {
    for (auto& b : buttons_)
        b->repaint();
    for (auto& b : faderButtons_)
        b->repaint();
    for (auto* b : {knobFollow_.get(), faderFollow_.get()})
        if (b != nullptr)
            b->repaint();
}

juce::Button& KnobStylePicker::getStyleButtonForTest(int index) { return *buttons_[(size_t)index]; }

juce::Button& KnobStylePicker::getFollowButtonForTest(bool fader) { return *(fader ? faderFollow_ : knobFollow_); }

juce::Button& KnobStylePicker::getFaderStyleButtonForTest(int index) { return *faderButtons_[(size_t)index]; }

void KnobStylePicker::resized() {
    auto area = getLocalBounds();
    auto layoutRow = [&](juce::Label& label, Row& buttons, int height) {
        auto row = area.removeFromTop(height);
        label.setBounds(row.removeFromLeft(kRowLabelWidth));
        const int slots = synth::theme::kKnobStyleCount + (mode_ == Mode::Override ? 1 : 0);
        const int width = juce::jmin(kButtonMaxWidth, row.getWidth() / slots);
        if (auto* follow = &label == &knobRowLabel_ ? knobFollow_.get() : faderFollow_.get())
            follow->setBounds(row.removeFromLeft(width));
        for (auto& b : buttons)
            b->setBounds(row.removeFromLeft(width));
        area.removeFromTop(kRowGap);
    };
    layoutRow(knobRowLabel_, buttons_, kButtonHeight);
    layoutRow(faderRowLabel_, faderButtons_, kFaderButtonHeight);
    if (mode_ == Mode::Settings)
        familyToggle_.setBounds(area.removeFromTop(24));
}

} // namespace synth::ui
