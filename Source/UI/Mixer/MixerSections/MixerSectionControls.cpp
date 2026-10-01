// Concern: the section divider's paint and resize gesture, and the hidden section's strip.
#include "MixerSectionControls.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
struct DividerColours {
    juce::Colour border;
    juce::Colour accent;
};

// Literal fallbacks: a headless test has no themed LookAndFeel installed.
DividerColours dividerColoursFor(const juce::Component& c) {
    if (const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel())) {
        const auto& colors = laf->getTheme().colors;
        return {colors.border, colors.accent};
    }
    return {juce::Colour(0xff2A2F38), juce::Colour(0xff00D1FF)};
}
} // namespace

juce::String mixerSectionCountSummary(int count, const juce::String& singular, const juce::String& plural) {
    if (count <= 0)
        return "no " + plural;
    return juce::String(count) + " " + (count == 1 ? singular : plural);
}

MixerSectionDivider::MixerSectionDivider() {
    // The panel stays the mixer's single focusable leaf.
    setWantsKeyboardFocus(false);
}

void MixerSectionDivider::setLayout(MixerSectionLayout* layout, MixerSection section) {
    layout_ = layout;
    section_ = section;
    setTitle(juce::String("Resize ") + MixerSectionLayout::nameOf(section));
    setTooltip(section == MixerSection::Eq ? juce::String()
                                           : "Drag to resize the " + juce::String(MixerSectionLayout::nameOf(section)) +
                                                 " section, double-click to reset it");
    setMouseCursor(isResizable() ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

// The EQ section is a single fixed-height curve, so its divider is a plain line: no resize cursor and
// no drag.
bool MixerSectionDivider::isResizable() const noexcept { return layout_ != nullptr && section_ != MixerSection::Eq; }

bool MixerSectionDivider::isHighlighted() const noexcept {
    if (!isResizable())
        return false;
    return layout_->getHoveredDivider() == (int)section_ || layout_->getDraggingDivider() == (int)section_;
}

// Idle: a 1 px border line through the middle of the 6 px hit area. Hovered or dragging (in ANY
// column -- the state lives on the shared layout): a 2 px accent line, so the whole row
// lights up together.
void MixerSectionDivider::paint(juce::Graphics& g) {
    const auto colours = dividerColoursFor(*this);
    const int mid = getHeight() / 2;
    if (isHighlighted()) {
        g.setColour(colours.accent);
        g.fillRect(0, mid - 1, getWidth(), 2);
    } else {
        g.setColour(colours.border);
        g.fillRect(0, mid, getWidth(), 1);
    }
}

void MixerSectionDivider::mouseEnter(const juce::MouseEvent&) {
    if (isResizable())
        layout_->setHoveredDivider((int)section_);
}

void MixerSectionDivider::mouseExit(const juce::MouseEvent&) {
    if (isResizable() && layout_->getHoveredDivider() == (int)section_)
        layout_->setHoveredDivider(-1);
}

// Screen coordinates, not component-relative: a drag that grows the bottom dock moves this divider
// (and the whole panel) up under the pointer, so only the pointer's own travel is stable.
void MixerSectionDivider::mouseDown(const juce::MouseEvent& e) {
    if (!isResizable() || !e.mods.isLeftButtonDown())
        return;
    dragStartScreenY_ = e.getScreenY();
    layout_->beginDividerDrag(section_);
}

void MixerSectionDivider::mouseDrag(const juce::MouseEvent& e) {
    if (isResizable() && layout_->getDraggingDivider() == (int)section_)
        layout_->dragDividerBy(section_, e.getScreenY() - dragStartScreenY_);
}

void MixerSectionDivider::mouseUp(const juce::MouseEvent&) {
    if (isResizable() && layout_->getDraggingDivider() == (int)section_)
        layout_->endDividerDrag();
}

void MixerSectionDivider::mouseDoubleClick(const juce::MouseEvent&) {
    if (isResizable())
        layout_->resetToDefault(section_);
}

MixerCollapsedSection::MixerCollapsedSection() { setWantsKeyboardFocus(false); }

void MixerCollapsedSection::setLayout(MixerSectionLayout* layout, MixerSection section) {
    layout_ = layout;
    section_ = section;
    setTitle(juce::String("Show ") + MixerSectionLayout::nameOf(section));
    setTooltip("Show the " + juce::String(MixerSectionLayout::nameOf(section)) + " section");
}

void MixerCollapsedSection::setSummary(const juce::String& summary) {
    if (summary_ == summary)
        return;
    summary_ = summary;
    repaint();
}

void MixerCollapsedSection::paint(juce::Graphics& g) {
    if (summary_.isEmpty())
        return;
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    g.setColour(laf != nullptr ? laf->getTheme().colors.textDisabled : juce::Colour(0xff5C6470));
    g.setFont(juce::Font(juce::FontOptions(9.0f)));
    g.drawText(summary_, getLocalBounds().reduced(4, 0), juce::Justification::centredLeft, true);
}

void MixerCollapsedSection::mouseUp(const juce::MouseEvent& e) {
    if (layout_ != nullptr && e.mouseWasClicked() && !e.mods.isPopupMenu())
        layout_->setHidden(section_, false);
}

std::unique_ptr<juce::AccessibilityHandler> MixerCollapsedSection::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::button,
        juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] {
            if (layout_ != nullptr)
                layout_->setHidden(section_, false);
        }));
}

} // namespace synth::ui
