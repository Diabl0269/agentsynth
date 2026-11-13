#include "PointValueField.h"

#include "UI/Chrome/ShortcutHint/ShortcutHintLayout.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kGapPx = 10;      // between the point's centre and the field's edge
constexpr int kMinWidthPx = 84; // room to type a longer value than the one shown
constexpr int kPadPx = 28;
constexpr int kHeightPx = 22;

struct Palette {
    juce::Colour fill{juce::Colours::black};
    juce::Colour text{juce::Colours::lightgrey};
    juce::Colour border{juce::Colours::grey};
    juce::Colour accent{juce::Colours::orange};
    juce::Colour error{juce::Colours::red};
    float fontPx = 11.0f;
};

Palette paletteFor(const juce::Component& c) {
    Palette p;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel())) {
        const auto& theme = lf->getTheme();
        p.fill = theme.colors.surfaceHi;
        p.text = theme.colors.textPrimary;
        p.border = theme.colors.border;
        p.accent = theme.colors.accent;
        p.error = theme.colors.error;
        p.fontPx = theme.type.value + 1.0f;
    }
    return p;
}

juce::Font monoFont(float px) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), px, juce::Font::plain));
}

bool isDigit(juce::juce_wchar c) { return c >= '0' && c <= '9'; }
bool isSign(juce::juce_wchar c) { return c == '-' || c == '+' || c == 0x2212; }

// Right of the anchor, vertically centred, kept inside `area`; left of the anchor when there is no room on the right.
juce::Rectangle<int> boxFor(juce::Point<float> anchor, int w, juce::Rectangle<int> area) {
    int x = (int)std::lround(anchor.x) + kGapPx;
    if (x + w > area.getRight())
        x = (int)std::lround(anchor.x) - kGapPx - w;
    x = juce::jlimit(area.getX(), std::max(area.getX(), area.getRight() - w), x);
    const int y = juce::jlimit(area.getY(), std::max(area.getY(), area.getBottom() - kHeightPx),
                               (int)std::lround(anchor.y) - kHeightPx / 2);
    return {x, y, w, kHeightPx};
}
} // namespace

PointValueField::PointValueField(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner) {
    setMultiLine(false);
    setReturnKeyStartsNewLine(false);
    setScrollbarsShown(false);
    setSelectAllWhenFocused(true);
    setPopupMenuEnabled(false);
    setComponentID("pointValueField");
    setTooltip("Type a value and press Return to set it, Escape to cancel");
    setInterceptsMouseClicks(false, false);
    onTextChange = [this] { setInvalid(false); };
    onFocusLost = [this] {
        if (open_)
            finish(parse && parse(getText()).has_value(), false);
    };
    owner.addChildComponent(*this);
    applyColours();
}

PointValueField::~PointValueField() { fade_.stop(vblank_); }

// An optional sign, digits with at most one point, then a unit made of anything that is not part of a number: "-12 dB",
// "3.5%", "1/4". Text with a second number in it ("1.2.3", "5-3") is not a value.
std::optional<double> PointValueField::parseNumber(const juce::String& text) {
    const auto s = text.trim();
    auto it = s.getCharPointer();
    juce::String number;
    if (isSign(*it)) {
        if (*it == '-' || *it == 0x2212)
            number << '-';
        ++it;
    }
    bool digits = false, point = false;
    for (; !it.isEmpty(); ++it) {
        if (isDigit(*it)) {
            digits = true;
        } else if (*it == '.' && !point) {
            point = true;
        } else {
            break;
        }
        number << *it;
    }
    if (!digits)
        return std::nullopt;
    for (; !it.isEmpty(); ++it)
        if (isDigit(*it) || *it == '.' || isSign(*it))
            return std::nullopt;
    const double value = number.getDoubleValue();
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

bool PointValueField::animates() const { return owner_.isShowing() && !prefersReducedMotion(); }

void PointValueField::applyColours() {
    const auto p = paletteFor(*this);
    const auto& shown = invalid_ ? p.error : p.text;
    setFont(monoFont(p.fontPx));
    setColour(juce::TextEditor::backgroundColourId, p.fill);
    setColour(juce::TextEditor::textColourId, shown);
    setColour(juce::TextEditor::highlightColourId, p.accent.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, shown);
    setColour(juce::TextEditor::outlineColourId, invalid_ ? p.error : p.border);
    setColour(juce::TextEditor::focusedOutlineColourId, invalid_ ? p.error : p.border);
    setColour(juce::CaretComponent::caretColourId, p.accent);
    applyFontToAllText(getFont());
    repaint();
}

void PointValueField::lookAndFeelChanged() {
    juce::TextEditor::lookAndFeelChanged();
    applyColours();
}

void PointValueField::setInvalid(bool invalid) {
    if (invalid_ == invalid)
        return;
    invalid_ = invalid;
    applyColours();
}

void PointValueField::paintOverChildren(juce::Graphics& g) {
    juce::TextEditor::paintOverChildren(g);
    paintFocusRing(g, getLocalBounds().toFloat(), *this, 3.0f);
}

void PointValueField::open(juce::Point<float> anchor, const juce::String& text, const juce::String& name) {
    const auto palette = paletteFor(owner_);
    const int w = std::max(
        kMinWidthPx, (int)std::ceil(juce::GlyphArrangement::getStringWidth(monoFont(palette.fontPx), text)) + kPadPx);
    setBounds(boxFor(anchor, w, owner_.getLocalBounds()));
    setTitle(name);
    invalid_ = false;
    applyColours();
    setText(text, false);
    open_ = true;
    setInterceptsMouseClicks(true, true);
    setVisible(true);
    toFront(false);
    fade(true);
    grabKeyboardFocus();
    selectAll();
}

void PointValueField::finish(bool commit, bool refocus) {
    if (!open_)
        return;
    std::optional<double> value;
    if (commit && parse)
        value = parse(getText());
    open_ = false;
    setInterceptsMouseClicks(false, false);
    fade(false);
    if (refocus)
        owner_.grabKeyboardFocus();
    if (value.has_value() && onCommit)
        onCommit(*value);
}

void PointValueField::close(bool commit) { finish(commit, true); }

void PointValueField::submit() {
    if (parse && parse(getText()).has_value())
        finish(true, true);
    else
        setInvalid(true);
}

// Return and Escape are handled here, before the TextEditor posts them as command messages: the field then closes
// inside the key press itself, never from a listener callback. An edit clears the error at once; onTextChange only
// follows on a later message-loop turn.
bool PointValueField::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::returnKey) {
        submit();
        return true;
    }
    if (key == juce::KeyPress::escapeKey) {
        finish(false, true);
        return true;
    }
    const auto before = getText();
    const bool handled = juce::TextEditor::keyPressed(key);
    if (getText() != before)
        setInvalid(false);
    return handled;
}

void PointValueField::fade(bool in) {
    if (!animates()) {
        fade_.stop(vblank_);
        setOpacity(in ? 1.0f : 0.0f);
        return;
    }
    const float from = opacity_;
    if (in) {
        fade_.start(vblank_, hint::resumeDurationMs(from, kFadeInMs), easeOutCubic,
                    [this, from](float e) { setOpacity(hint::tweenUp(from, e)); });
    } else {
        fade_.start(vblank_, kFadeOutMs, easeInCubic, [this, from](float e) { setOpacity(hint::tweenDown(from, e)); });
    }
}

void PointValueField::setOpacity(float value) {
    opacity_ = juce::jlimit(0.0f, 1.0f, value);
    setAlpha(opacity_);
    if (opacity_ <= 0.0f && !open_)
        setVisible(false);
}

} // namespace synth::ui
