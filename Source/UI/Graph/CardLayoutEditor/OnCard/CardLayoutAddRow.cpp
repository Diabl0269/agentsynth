// CardLayoutAddRow.cpp -- the Add control panel's row: its highlighted name and its click-or-drag mouse.
#include "CardLayoutAddRow.h"
#include "UI/Layout/SearchMatch.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr float kFontHeight = 13.0f;
constexpr float kWash = 0.14f;
constexpr float kHoverWash = 0.07f;
constexpr int kDragThreshold = 3;

// The name in the text colour, each run the query matched in the accent colour and semi-bold.
juce::AttributedString nameWithMatches(const juce::String& name, const juce::String& query,
                                       const synth::theme::Colors& colours) {
    const auto plain = synth::theme::AppLookAndFeel::uiFont(kFontHeight);
    const auto bold = synth::theme::AppLookAndFeel::uiSemiBoldFont(kFontHeight);
    juce::AttributedString text;
    text.setJustification(juce::Justification::centredLeft);
    text.setWordWrap(juce::AttributedString::none);
    int at = 0;
    for (const auto& span : searchHighlightSpans(name, query)) {
        text.append(name.substring(at, span.start), plain, colours.textPrimary);
        text.append(name.substring(span.start, span.start + span.length), bold, colours.accent);
        at = span.start + span.length;
    }
    text.append(name.substring(at), plain, colours.textPrimary);
    return text;
}

} // namespace

CardLayoutAddRow::CardLayoutAddRow(AddableControl control)
    : juce::Button(control.name)
    , control_(std::move(control)) {
    setWantsKeyboardFocus(false);
    setTitle(control_.name);
    setTooltip("Add " + control_.name + " to the card, or drag it onto the card");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    onClick = [this] {
        if (onPick)
            onPick();
    };
}

void CardLayoutAddRow::setQuery(const juce::String& query) {
    if (query == query_)
        return;
    query_ = query;
    repaint();
}

void CardLayoutAddRow::setSelected(bool selected) {
    if (selected == selected_)
        return;
    selected_ = selected;
    repaint();
}

void CardLayoutAddRow::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    const auto area = getLocalBounds().toFloat().reduced(4.0f, 1.0f);
    if (selected_ || highlighted) {
        g.setColour(colours.accent.withAlpha(selected_ ? kWash : kHoverWash));
        g.fillRoundedRectangle(area, 6.0f);
    }
    nameWithMatches(control_.name, query_, colours).draw(g, getLocalBounds().reduced(14, 0).toFloat());
}

void CardLayoutAddRow::mouseDown(const juce::MouseEvent& e) {
    dragging_ = false;
    pressAt_ = e.getScreenPosition();
}

void CardLayoutAddRow::mouseDrag(const juce::MouseEvent& e) {
    if (!e.mods.isLeftButtonDown() || (!dragging_ && e.getScreenPosition().getDistanceFrom(pressAt_) < kDragThreshold))
        return;
    const bool begin = !dragging_;
    dragging_ = true;
    if (onDrag)
        onDrag(begin ? DragPhase::Begin : DragPhase::Move, e.getScreenPosition());
}

void CardLayoutAddRow::mouseUp(const juce::MouseEvent& e) {
    if (std::exchange(dragging_, false)) {
        if (onDrag)
            onDrag(DragPhase::End, e.getScreenPosition());
    } else if (!e.mods.isPopupMenu() && getLocalBounds().contains(e.getPosition()) && onClick) {
        onClick();
    }
}

} // namespace synth::ui
