#include "UI/Layout/SearchPillField.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Chrome/ShortcutHint/ShortcutHintText.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FadeAmount.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr float kRadius = 13.0f;
constexpr int kEdgePad = 8;        // the right edge to the cap or the clear button
constexpr int kMagnifierLeft = 10; // the left edge to the magnifier
constexpr int kMagnifierSize = 14;
constexpr int kPartGap = 6; // between the parts
constexpr int kTextLeft = kMagnifierLeft + kMagnifierSize + kPartGap;
constexpr int kClearSize = 16;
constexpr int kCapHeight = 18;
constexpr int kTextOnlyRight = 10;
constexpr float kFontSize = 12.0f;
constexpr double kRingFadeMs = 100.0;

void paintMagnifier(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour) {
    const auto lens = box.withSize(10.0f, 10.0f).reduced(0.75f);
    juce::Path handle;
    handle.startNewSubPath(box.getX() + 8.6f, box.getY() + 8.6f);
    handle.lineTo(box.getRight() - 1.0f, box.getBottom() - 1.0f);
    g.setColour(colour);
    g.drawEllipse(lens, 1.5f);
    g.strokePath(handle, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace

// The round x that takes the hint's place while there is text. A plain juce::Button, so the real mouse path, Return
// and the screen reader's press all reach the one click callback.
class SearchPillField::ClearButton : public juce::Button {
public:
    ClearButton()
        : juce::Button("Clear search") {
        setComponentID("searchPillClear");
        setTitle("Clear search");
        setTooltip("Clear search");
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto& c = synth::theme::themeOf(*this).colors;
        const auto disc = getLocalBounds().toFloat().withSizeKeepingCentre((float)kClearSize, (float)kClearSize);
        g.setColour(down ? c.surfaceHi.brighter(0.15f) : (highlighted ? c.surfaceHi.brighter(0.08f) : c.surfaceHi));
        g.fillEllipse(disc);
        const auto mark = disc.reduced(5.0f);
        juce::Path cross;
        cross.startNewSubPath(mark.getTopLeft());
        cross.lineTo(mark.getBottomRight());
        cross.startNewSubPath(mark.getTopRight());
        cross.lineTo(mark.getBottomLeft());
        g.setColour(highlighted || down ? c.textPrimary : c.textMuted);
        g.strokePath(cross, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        paintFocusRing(g, disc.expanded(1.0f), *this, (float)kClearSize / 2.0f + 1.0f);
    }
};

struct SearchPillField::Fades {
    Fades(juce::Component& owner, juce::Component& clearButton)
        : hint(owner)
        , clear({&clearButton})
        , ring(owner) {}
    FadeAmount hint;
    FadeVisibility clear;
    ShownFade ring;
};

SearchPillField::SearchPillField()
    : clear_(std::make_unique<ClearButton>())
    , fades_(std::make_unique<Fades>(*this, *clear_)) {
    setMultiLine(false);
    setReturnKeyStartsNewLine(false);
    setSelectAllWhenFocused(true);
    setJustification(juce::Justification::centredLeft);
    setFont(juce::Font(juce::FontOptions(kFontSize)));
    setIndents(kTextLeft, 0);
    setBorder(juce::BorderSize<int>(0, 0, 0, kTextOnlyRight));
    setPaintingIsUnclipped(true); // the focus ring sits just outside the pill
    removeHiddenTabStops(*this);  // the editor's own viewport is no Tab stop; the clear button added below is
    addChildComponent(*clear_);
    clear_->onClick = [this] { clearText(); };
    addListener(this);
    fades_->hint.repaintArea = [this] { return getShortcutHintBounds().expanded(8); };
    fades_->hint.snapTo(false);
    fades_->ring.onFrame = [this] { repaintWithRing(); };
    applyTheme();
}

SearchPillField::~SearchPillField() { removeListener(this); }

bool SearchPillField::isShortcutHintShown() const noexcept { return fades_->hint.isShown(); }

bool SearchPillField::isClearButtonShown() const { return fades_->clear.isShown(); }

void SearchPillField::setPlaceholder(const juce::String& text) {
    placeholder_ = text;
    // JUCE would draw the placeholder only while unfocused; the pill draws its own, so JUCE's stays invisible.
    setTextToShowWhenEmpty(text, juce::Colours::transparentBlack);
    refreshAccessories();
    repaint();
}

void SearchPillField::setShortcutHint(const juce::String& keyLabel) {
    if (hint_ == keyLabel)
        return;
    hint_ = keyLabel;
    updateTooltip();
    refreshAccessories();
    repaint();
}

void SearchPillField::setShortcutFromAction(const ShortcutManager* manager, const juce::String& actionId) {
    setShortcutHint(manager != nullptr ? synth::ui::hint::formatKeyCapTextForPlatform(manager->getBinding(actionId))
                                       : juce::String());
}

void SearchPillField::setSearchTooltip(const juce::String& text) {
    tooltipBase_ = text;
    updateTooltip();
}

void SearchPillField::updateTooltip() {
    if (tooltipBase_.isNotEmpty())
        setTooltip(formatShortcutHint(tooltipBase_, hint_));
}

juce::Component& SearchPillField::getClearButton() { return *clear_; }

juce::Rectangle<int> SearchPillField::getMagnifierBounds() const {
    return {kMagnifierLeft, (getHeight() - kMagnifierSize) / 2, kMagnifierSize, kMagnifierSize};
}

juce::Rectangle<int> SearchPillField::getClearButtonBounds() const {
    return {getWidth() - kEdgePad - kClearSize, (getHeight() - kClearSize) / 2, kClearSize, kClearSize};
}

juce::Rectangle<int> SearchPillField::getShortcutHintBounds() const {
    const int width = capWidth();
    return {getWidth() - kEdgePad - width, (getHeight() - kCapHeight) / 2, width, kCapHeight};
}

juce::Rectangle<int> SearchPillField::getTextArea() const {
    const bool hasText = getTotalNumChars() > 0;
    const int right = rightReserve(hasText, !hasText && hintFits());
    return {kTextLeft, 0, juce::jmax(0, getWidth() - kTextLeft - right), getHeight()};
}

int SearchPillField::capWidth() const {
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        return lf->getShortcutKeyCapWidth(hint_, false);
    return juce::jmax(synth::theme::AppLookAndFeel::kKeyCapMinWidth,
                      hint_.length() * 6 + 2 * synth::theme::AppLookAndFeel::kKeyCapSidePadding);
}

// The cap needs its own room beside the magnifier AND the whole placeholder, so it never covers the placeholder or
// squeezes it into an ellipsis.
bool SearchPillField::hintFits() const {
    if (hint_.isEmpty())
        return false;
    const int placeholderWidth =
        placeholder_.isEmpty()
            ? 0
            : (int)std::ceil(juce::GlyphArrangement::getStringWidth(getFont(), placeholder_)) + kPartGap;
    return getWidth() >= kTextLeft + placeholderWidth + capWidth() + kEdgePad;
}

int SearchPillField::rightReserve(bool hasText, bool hintShown) const {
    if (hasText)
        return kEdgePad + kClearSize + kPartGap;
    if (hintShown)
        return kEdgePad + capWidth() + kPartGap;
    return kTextOnlyRight;
}

// Decides what the right edge shows from what is typed and how wide the field is, and gives the text the rest.
void SearchPillField::refreshAccessories() {
    const bool hasText = getTotalNumChars() > 0;
    const bool hintShown = !hasText && hintFits();
    fades_->hint.setShown(hintShown);
    fades_->clear.setShown(hasText);
    const juce::BorderSize<int> border(0, 0, 0, rightReserve(hasText, hintShown));
    if (getBorder() != border)
        setBorder(border);
}

bool SearchPillField::accessoriesOutOfStep() const {
    const bool hasText = getTotalNumChars() > 0;
    return fades_->clear.isShown() != hasText || fades_->hint.isShown() != (!hasText && hintFits());
}

void SearchPillField::setText(const juce::String& newText, bool sendTextChangeMessage) {
    NavigationSearchField::setText(newText, sendTextChangeMessage);
    refreshAccessories();
}

void SearchPillField::clearText() {
    setText({}, true);
    refreshAccessories();
    grabKeyboardFocus();
}

bool SearchPillField::keyPressed(const juce::KeyPress& key) {
    const bool handled = NavigationSearchField::keyPressed(key);
    refreshAccessories();
    return handled;
}

void SearchPillField::insertTextAtCaret(const juce::String& text) {
    NavigationSearchField::insertTextAtCaret(text);
    refreshAccessories();
}

void SearchPillField::applyTheme() {
    const auto& c = synth::theme::themeOf(*this).colors;
    setColour(juce::TextEditor::textColourId, c.textPrimary);
    // the pill paints its own fill and outline
    setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
}

void SearchPillField::lookAndFeelChanged() {
    NavigationSearchField::lookAndFeelChanged();
    applyTheme();
    refreshAccessories();
    repaint();
}

void SearchPillField::parentHierarchyChanged() {
    NavigationSearchField::parentHierarchyChanged();
    applyTheme();
}

void SearchPillField::resized() {
    NavigationSearchField::resized();
    clear_->setBounds(getClearButtonBounds());
    refreshAccessories();
}

void SearchPillField::focusGained(FocusChangeType type) {
    NavigationSearchField::focusGained(type);
    updateRing();
}

void SearchPillField::focusLost(FocusChangeType type) {
    NavigationSearchField::focusLost(type);
    updateRing();
}

void SearchPillField::updateRing() {
    fades_->ring.fadeTo(hasKeyboardFocus(false) ? 1.0f : 0.0f, kRingFadeMs, easeOutCubic);
}

void SearchPillField::repaintWithRing() {
    repaint();
    if (auto* parent = getParentComponent())
        parent->repaint(getBounds().expanded(kRingOutset + 2));
}

// The pill, its focus ring, the magnifier and the placeholder. The ring is the shared focus ring, pushed out by a
// 2 px gap in the page colour so it reads as separate from the border; its fade runs on the shared ShownFade.
void SearchPillField::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    const auto& c = theme.colors;
    const auto pill = getLocalBounds().toFloat();
    // a change made through a juce::TextEditor reference skips setText above; catch up once painting is done
    if (accessoriesOutOfStep())
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SearchPillField>(this)] {
            if (safe != nullptr)
                safe->refreshAccessories();
        });
    const float ring = (showsFocusRing(*this) && !fades_->ring.isRunning()) ? 1.0f : fades_->ring.alpha();

    if (ring > 0.0f) {
        const float thickness = focusRingThickness(theme);
        g.beginTransparencyLayer(ring);
        g.setColour(c.bg0);
        g.drawRoundedRectangle(pill.expanded(kFocusFillGap * 0.5f), kRadius + kFocusFillGap * 0.5f, kFocusFillGap);
        paintFocusRingAlways(g, pill.expanded(kFocusFillGap + thickness), *this, kRadius + kFocusFillGap + thickness);
        g.endTransparencyLayer();
    }

    g.setColour(c.surface);
    g.fillRoundedRectangle(pill, kRadius);
    g.setColour(c.border.interpolatedWith(c.accent, ring));
    g.drawRoundedRectangle(pill.reduced(0.5f), kRadius - 0.5f, theme.metrics.borderWidth);

    paintMagnifier(g, getMagnifierBounds().toFloat(), c.textMuted);

    if (getTotalNumChars() == 0 && placeholder_.isNotEmpty()) {
        g.setColour(c.textMuted);
        g.setFont(getFont());
        g.drawText(placeholder_, getTextArea(), juce::Justification::centredLeft, true);
    }
}

void SearchPillField::paintOverChildren(juce::Graphics& g) {
    // not the base class: its outline and placeholder are the square, focus-only ones the pill replaces
    const float amount = fades_->hint.value();
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    if (amount <= 0.0f || lf == nullptr || hint_.isEmpty())
        return;
    g.beginTransparencyLayer(amount);
    lf->drawShortcutKeyCap(g, getShortcutHintBounds(), hint_, false);
    g.endTransparencyLayer();
}

} // namespace synth::ui
