// EQBandTypeMenu.cpp -- see EQBandTypeMenu.h. Two columns of rows: the eight types (each with its kind colour) and the
// nine cut slopes, dimmed and skipped by the keyboard until the band is a cut.

#include "EQBandTypeMenu.h"

#include "EQBandKinds.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <optional>

namespace synth::ui {

namespace {
constexpr int kRowH = 24;
constexpr int kPad = 6;
constexpr int kTypeColW = 118;
constexpr int kSlopeColW = 88;
constexpr synth::eq::BandType kTypeOrder[synth::eq::kNumBandTypes] = {
    synth::eq::BandType::Peak,     synth::eq::BandType::LowShelf, synth::eq::BandType::HighShelf,
    synth::eq::BandType::LowCut,   synth::eq::BandType::HighCut,  synth::eq::BandType::Notch,
    synth::eq::BandType::BandPass, synth::eq::BandType::Tilt};
} // namespace

// One choice: a type (with a swatch in its kind colour) or a slope. A tick marks the current one.
class EQBandTypeMenu::Row : public juce::Button {
public:
    Row(const juce::String& text, std::optional<synth::eq::BandType> type)
        : juce::Button(text)
        , type_(type) {
        setTitle(text);
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void setCurrent(bool current) {
        if (current_ == current)
            return;
        current_ = current;
        setToggleState(current, juce::dontSendNotification);
        repaint();
    }

    void paintButton(juce::Graphics& g, bool over, bool down) override {
        const auto& colors = synth::theme::themeOf(*this).colors;
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        if (over || down) {
            g.setColour(colors.surfaceHi);
            g.fillRoundedRectangle(area, 4.0f);
        }
        float x = 8.0f;
        if (type_) {
            g.setColour(eqBandKindColour(colors, eqBandKindOf(*type_)).withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f));
            g.fillEllipse(x, area.getCentreY() - 4.0f, 8.0f, 8.0f);
            x += 16.0f;
        }
        g.setColour((current_ ? colors.accent : colors.textPrimary).withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f));
        g.setFont(juce::Font(12.0f, current_ ? juce::Font::bold : juce::Font::plain));
        g.drawText(getButtonText(), juce::Rectangle<float>(x, 0.0f, (float)getWidth() - x - 4.0f, (float)getHeight()),
                   juce::Justification::centredLeft, false);
        paintFocusRing(g, getLocalBounds().toFloat(), *this, 4.0f);
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (isEnabled() && key == juce::KeyPress::returnKey) {
            if (onClick)
                onClick();
            return true;
        }
        return juce::Button::keyPressed(key);
    }

private:
    std::optional<synth::eq::BandType> type_;
    bool current_ = false;
};

EQBandTypeMenu::EQBandTypeMenu(synth::eq::BandType current, int slopeDbPerOct)
    : current_(current)
    , slope_(slopeDbPerOct)
    , arrows_(*this) {
    setTitle("Band type");
    for (size_t i = 0; i < typeRows_.size(); ++i) {
        const auto type = kTypeOrder[i];
        typeRows_[i] = std::make_unique<Row>(eqBandTypeName(type), type);
        typeRows_[i]->setTooltip("Make this band a " + eqBandTypeName(type).toLowerCase());
        typeRows_[i]->onClick = [this, type] { chooseType(type); };
        addAndMakeVisible(*typeRows_[i]);
    }
    for (size_t i = 0; i < slopeRows_.size(); ++i) {
        const int slope = synth::eq::kSlopesDbPerOct[i];
        slopeRows_[i] = std::make_unique<Row>(eqSlopeText(slope), std::nullopt);
        slopeRows_[i]->setTooltip("Cut slope " + eqSlopeText(slope) + " (only for a low or high cut)");
        slopeRows_[i]->onClick = [this, slope] { chooseSlope(slope); };
        addAndMakeVisible(*slopeRows_[i]);
    }
    showCurrent();
    setSize(kPad * 3 + kTypeColW + kSlopeColW, kPad * 2 + 9 * kRowH);
}

EQBandTypeMenu::~EQBandTypeMenu() = default;

void EQBandTypeMenu::showCurrent() {
    for (size_t i = 0; i < typeRows_.size(); ++i)
        typeRows_[i]->setCurrent(kTypeOrder[i] == current_);
    for (size_t i = 0; i < slopeRows_.size(); ++i) {
        slopeRows_[i]->setCurrent(synth::eq::kSlopesDbPerOct[i] == slope_);
        slopeRows_[i]->setEnabled(eqTypeHasSlope(current_));
    }
    repaint();
}

void EQBandTypeMenu::resized() {
    for (size_t i = 0; i < typeRows_.size(); ++i)
        typeRows_[i]->setBounds(kPad, kPad + (int)i * kRowH, kTypeColW, kRowH);
    for (size_t i = 0; i < slopeRows_.size(); ++i)
        slopeRows_[i]->setBounds(kPad * 2 + kTypeColW, kPad + (int)i * kRowH, kSlopeColW, kRowH);
}

void EQBandTypeMenu::paint(juce::Graphics& g) {
    const auto& colors = synth::theme::themeOf(*this).colors;
    g.fillAll(colors.surface);
    g.setColour(colors.border);
    const int dividerX = kPad + kTypeColW + kPad / 2;
    g.drawVerticalLine(dividerX, (float)kPad, (float)(getHeight() - kPad));
}

// The menu opens with the current type focused, so Return keeps it and the arrows move from there.
void EQBandTypeMenu::parentHierarchyChanged() {
    if (getParentComponent() == nullptr && getPeer() == nullptr)
        return;
    juce::Component::SafePointer<Row> focusRow;
    for (size_t i = 0; i < typeRows_.size(); ++i)
        if (kTypeOrder[i] == current_)
            focusRow = typeRows_[i].get();
    juce::MessageManager::callAsync([focusRow] {
        if (focusRow != nullptr && focusRow->isShowing())
            focusRow->grabKeyboardFocus();
    });
}

void EQBandTypeMenu::chooseType(synth::eq::BandType type) {
    current_ = type;
    if (onChoose)
        onChoose(type, 0);
    showCurrent();
    if (!eqTypeHasSlope(type))
        closeHostingWindow(*this);
}

void EQBandTypeMenu::chooseSlope(int slopeDbPerOct) {
    slope_ = slopeDbPerOct;
    if (onChoose)
        onChoose(current_, slopeDbPerOct);
    showCurrent();
    closeHostingWindow(*this);
}

void EQBandTypeMenu::chooseTypeForTest(synth::eq::BandType type) { chooseType(type); }
void EQBandTypeMenu::chooseSlopeForTest(int slopeDbPerOct) { chooseSlope(slopeDbPerOct); }
bool EQBandTypeMenu::slopesEnabledForTest() const { return slopeRows_[0]->isEnabled(); }

juce::Component* EQBandTypeMenu::typeRowForTest(synth::eq::BandType type) {
    for (size_t i = 0; i < typeRows_.size(); ++i)
        if (kTypeOrder[i] == type)
            return typeRows_[i].get();
    return nullptr;
}

juce::Component* EQBandTypeMenu::slopeRowForTest(int slopeDbPerOct) {
    for (size_t i = 0; i < slopeRows_.size(); ++i)
        if (synth::eq::kSlopesDbPerOct[i] == slopeDbPerOct)
            return slopeRows_[i].get();
    return nullptr;
}

} // namespace synth::ui
