// Concern: MixerSectionRail's row placement, chevron toggles and the divider drag bubble.
#include "MixerSectionRail.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kLabelRowHeight = 14;
constexpr int kChevronSize = 8;
constexpr int kBubbleHeight = 14;

const synth::theme::Theme* themeOf(const juce::Component& c) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel());
    return laf != nullptr ? &laf->getTheme() : nullptr;
}

MixerSection sectionAt(size_t index) { return (MixerSection)(int)index; }
} // namespace

MixerSectionRail::ToggleButton::ToggleButton()
    : juce::Button({}) {
    setWantsKeyboardFocus(false); // the panel stays the mixer's single focusable leaf
    setClickingTogglesState(false);
}

void MixerSectionRail::ToggleButton::setSection(MixerSectionLayout* layout, MixerSection section) {
    layout_ = layout;
    section_ = section;
    const bool hidden = layout_ != nullptr && layout_->isHidden(section_);
    setToggleState(!hidden, juce::dontSendNotification);
    setTitle(juce::String(hidden ? "Show " : "Hide ") + MixerSectionLayout::nameOf(section_));
    repaint();
}

// An 8 px chevron (down while shown, right while hidden) then the section name in micro caps.
void MixerSectionRail::ToggleButton::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto* theme = themeOf(*this);
    const auto chevronColour = theme != nullptr ? theme->colors.textPrimary : juce::Colour(0xffEAEEF3);
    const auto nameColour = theme != nullptr ? theme->colors.textMuted : juce::Colour(0xff8A93A0);
    const float micro = theme != nullptr ? theme->type.micro : 8.5f;

    auto area = getLocalBounds().toFloat().reduced(6.0f, 0.0f);
    const auto chevronBox =
        area.removeFromLeft((float)kChevronSize).withSizeKeepingCentre((float)kChevronSize, (float)kChevronSize);
    juce::Path chevron;
    if (getToggleState()) { // shown: pointing down
        chevron.startNewSubPath(chevronBox.getX() + 1.0f, chevronBox.getY() + 2.5f);
        chevron.lineTo(chevronBox.getCentreX(), chevronBox.getBottom() - 2.0f);
        chevron.lineTo(chevronBox.getRight() - 1.0f, chevronBox.getY() + 2.5f);
    } else { // hidden: pointing right
        chevron.startNewSubPath(chevronBox.getX() + 2.5f, chevronBox.getY() + 1.0f);
        chevron.lineTo(chevronBox.getRight() - 2.0f, chevronBox.getCentreY());
        chevron.lineTo(chevronBox.getX() + 2.5f, chevronBox.getBottom() - 1.0f);
    }
    g.setColour(highlighted ? chevronColour : chevronColour.withAlpha(0.85f));
    g.strokePath(chevron, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    area.removeFromLeft(4.0f);
    g.setColour(highlighted ? nameColour.brighter(0.3f) : nameColour);
    g.setFont(juce::Font(juce::FontOptions(micro)).withExtraKerningFactor(0.08f));
    g.drawText(juce::String(MixerSectionLayout::nameOf(section_)).toUpperCase(), area, juce::Justification::centredLeft,
               true);
}

MixerSectionRail::MixerSectionRail() {
    setWantsKeyboardFocus(false);
    for (size_t i = 0; i < toggles_.size(); ++i) {
        addAndMakeVisible(toggles_[i]);
        toggles_[i].onClick = [this, i] {
            if (layout_ != nullptr)
                layout_->toggleHidden(sectionAt(i));
        };
        addAndMakeVisible(dividers_[i]);
        dividers_[i].setShowsGrip(true);
    }
}

void MixerSectionRail::setLayout(MixerSectionLayout& layout) {
    layout_ = &layout;
    for (size_t i = 0; i < toggles_.size(); ++i)
        dividers_[i].setLayout(layout_, sectionAt(i));
    refreshLayout();
}

void MixerSectionRail::setColumnGeometry(int columnHeight, int scrollY) {
    if (columnHeight_ == columnHeight && scrollY_ == scrollY)
        return;
    columnHeight_ = columnHeight;
    scrollY_ = scrollY;
    resized();
}

void MixerSectionRail::refreshLayout() {
    for (size_t i = 0; i < toggles_.size(); ++i)
        toggles_[i].setSection(layout_, sectionAt(i));
    resized();
    repaint();
}

juce::Button& MixerSectionRail::getToggleButtonForTest(MixerSection section) noexcept {
    return toggles_[(size_t)section];
}

MixerSectionDivider& MixerSectionRail::getDividerForTest(MixerSection section) noexcept {
    return dividers_[(size_t)section];
}

juce::String MixerSectionRail::getDragBubbleTextForTest() const { return dragBubbleText(); }

juce::String MixerSectionRail::dragBubbleText() const {
    if (layout_ == nullptr || layout_->getDraggingDivider() < 0)
        return {};
    const int rows = layout_->getRowCount((MixerSection)layout_->getDraggingDivider());
    return juce::String(rows) + (rows == 1 ? " row" : " rows");
}

// Each rail row uses the SAME resolved geometry the columns do (MixerSectionLayout::resolve with the
// columns' own height), offset by the panel's vertical scroll, so a rail row and its sections always
// line up. The label row sits at the top of the section, capped to what the section actually got.
void MixerSectionRail::resized() {
    if (layout_ == nullptr)
        return;
    const auto geometry = layout_->resolve(columnHeight_);
    for (size_t i = 0; i < toggles_.size(); ++i) {
        const int top = geometry.sectionTop[i] - scrollY_;
        toggles_[i].setBounds(0, top, getWidth() - 1, juce::jmin(kLabelRowHeight, geometry.sectionHeight[i]));
        dividers_[i].setBounds(0, geometry.dividerTop[i] - scrollY_, getWidth() - 1,
                               MixerSectionLayout::kDividerHeight);
    }
}

void MixerSectionRail::paint(juce::Graphics& g) {
    const auto* theme = themeOf(*this);
    g.fillAll(theme != nullptr ? theme->colors.bg0 : juce::Colour(0xff0B0D10));
    g.setColour(theme != nullptr ? theme->colors.border : juce::Colour(0xff2A2F38));
    g.fillRect(getWidth() - 1, 0, 1, getHeight());
}

// The row-count bubble rides the divider being dragged, painted in the value (mono) face.
void MixerSectionRail::paintOverChildren(juce::Graphics& g) {
    const auto text = dragBubbleText();
    if (text.isEmpty())
        return;
    const auto* theme = themeOf(*this);
    const auto fill = theme != nullptr ? theme->colors.surfaceHi : juce::Colour(0xff232833);
    const auto outline = theme != nullptr ? theme->colors.accent : juce::Colour(0xff00D1FF);
    const auto textColour = theme != nullptr ? theme->colors.textPrimary : juce::Colour(0xffEAEEF3);
    const juce::String mono = theme != nullptr ? theme->type.monoFamily : juce::String("JetBrains Mono");
    const float size = theme != nullptr ? theme->type.value : 10.0f;

    const auto& divider = dividers_[(size_t)layout_->getDraggingDivider()];
    const auto bubble = juce::Rectangle<float>(6.0f, (float)(divider.getBounds().getCentreY() - kBubbleHeight / 2),
                                               (float)getWidth() - 13.0f, (float)kBubbleHeight);
    g.setColour(fill);
    g.fillRoundedRectangle(bubble, 7.0f);
    g.setColour(outline);
    g.drawRoundedRectangle(bubble.reduced(0.5f), 7.0f, 1.0f);
    g.setColour(textColour);
    g.setFont(juce::Font(juce::FontOptions(mono, size, juce::Font::plain)));
    g.drawText(text, bubble, juce::Justification::centred, false);
}

} // namespace synth::ui
