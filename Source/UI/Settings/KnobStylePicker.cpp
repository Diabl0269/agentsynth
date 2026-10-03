#include "KnobStylePicker.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/KnobPainter.h"

namespace synth::ui {

using synth::theme::KnobStyle;

namespace {
constexpr float kKnobPreviewSize = 44.0f;
constexpr float kPreviewPosition = 0.62f;
constexpr int kButtonMaxWidth = 76;
constexpr int kButtonHeight = 76;
constexpr float kLabelHeight = 12.0f;
constexpr float kCornerRadius = 8.0f;
} // namespace

// One preview: the knob painted in its style above the style's label; the picked one gets an accent outline and wash.
class KnobStylePicker::StyleButton : public juce::Button {
public:
    StyleButton(KnobStyle style, KnobStylePicker& owner)
        : juce::Button(synth::theme::knobStyleLabel(style))
        , style_(style)
        , owner_(owner) {
        setClickingTogglesState(true);
        setRadioGroupId(kRadioGroup);
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        const juce::String label = synth::theme::knobStyleLabel(style);
        setTitle(label + " knob style");
        setTooltip("Use the " + label + " look for every knob");
    }

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

        const auto knob = juce::Rectangle<float>(kKnobPreviewSize, kKnobPreviewSize)
                              .withCentre({area.getCentreX(), area.getY() + 8.0f + kKnobPreviewSize * 0.5f});
        const auto colour = owner_.appearance_.colourByFamily ? c.hueAmber : c.accent;
        synth::theme::paintKnob(g, theme, style_, knob, kPreviewPosition, colour);

        g.setColour(selected ? c.textPrimary : c.textMuted);
        g.setFont(synth::theme::AppLookAndFeel::uiFont(kLabelHeight));
        g.drawText(synth::theme::knobStyleLabel(style_),
                   area.withTrimmedTop(area.getHeight() - 22.0f).withHeight(16.0f), juce::Justification::centred);

        synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this, kCornerRadius);
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
        const int next = (int)style_ + step;
        if (next >= 0 && next < synth::theme::kKnobStyleCount)
            owner_.buttons_[(size_t)next]->grabKeyboardFocus();
        return true; // clamped at the ends
    }

    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    static constexpr int kRadioGroup = 0x4b6e; // arbitrary, unique among the Settings window's groups
    KnobStyle style_;
    KnobStylePicker& owner_;
};

KnobStylePicker::KnobStylePicker() {
    for (int i = 0; i < synth::theme::kKnobStyleCount; ++i) {
        auto button = std::make_unique<StyleButton>((KnobStyle)i, *this);
        button->onClick = [this, i] { styleChosen(i); };
        addAndMakeVisible(*button);
        buttons_[(size_t)i] = std::move(button);
    }
    buttons_[(size_t)appearance_.style]->setToggleState(true, juce::dontSendNotification);

    addAndMakeVisible(familyToggle_);
    familyToggle_.setToggleState(appearance_.colourByFamily, juce::dontSendNotification);
    familyToggle_.setTitle("Colour knobs by module family");
    familyToggle_.setTooltip("Tint each module card's knobs with its family colour instead of the accent");
    familyToggle_.onClick = [this] {
        appearance_.colourByFamily = familyToggle_.getToggleState();
        for (auto& b : buttons_)
            b->repaint();
        if (onChanged)
            onChanged(appearance_);
    };
}

KnobStylePicker::~KnobStylePicker() = default;

void KnobStylePicker::styleChosen(int index) {
    appearance_.style = (KnobStyle)index;
    if (onChanged)
        onChanged(appearance_);
}

void KnobStylePicker::setAppearance(const synth::theme::KnobAppearance& appearance) {
    appearance_ = appearance;
    buttons_[(size_t)appearance_.style]->setToggleState(true, juce::dontSendNotification);
    familyToggle_.setToggleState(appearance_.colourByFamily, juce::dontSendNotification);
    refreshPreviewColour();
}

void KnobStylePicker::setTheme(const synth::theme::Theme& theme) {
    theme_ = theme;
    refreshPreviewColour();
}

void KnobStylePicker::refreshPreviewColour() {
    for (auto& b : buttons_)
        b->repaint();
}

juce::Button& KnobStylePicker::getStyleButtonForTest(int index) { return *buttons_[(size_t)index]; }

void KnobStylePicker::resized() {
    auto area = getLocalBounds();
    auto row = area.removeFromTop(kButtonHeight);
    const int width = juce::jmin(kButtonMaxWidth, row.getWidth() / synth::theme::kKnobStyleCount);
    for (auto& b : buttons_)
        b->setBounds(row.removeFromLeft(width));
    area.removeFromTop(6);
    familyToggle_.setBounds(area.removeFromTop(24));
}

} // namespace synth::ui
