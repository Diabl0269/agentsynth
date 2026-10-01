// CardBodyFooter.cpp -- the footer row at the bottom of a card body (above the More row): the layout's
// footer section (CardSection::kFooterId) and the card's chrome toggles, in one compact row. A toggle is
// the small pill; a fader, combo, switch or stepper gets its caption inline on its left and shares the
// width the pills leave. Items that do not fit wrap onto another row. Measured by the same walk as it is
// placed, from the same text widths. docs/layout/module-card.md#the-footer-row.
#include "CardBody.h"
#include "CardBodyLayoutWalk.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth {

namespace {

using Theme = synth::theme::AppLookAndFeel;

struct Chip {
    juce::Component* widget = nullptr;
    juce::Component* label = nullptr;
    int labelWidth = 0;
    int width = 0; ///< Preferred (pills: exact) width, caption included.
    int height = 0;
    bool stretches = false;
};

int captionWidth(const juce::String& text) {
    constexpr int kCaptionGap = 6;
    return juce::GlyphArrangement::getStringWidthInt(juce::Font(juce::FontOptions(Theme::kTogglePillFontHeight)),
                                                     text) +
           kCaptionGap;
}

Chip pillChip(juce::Component* widget, const juce::String& text) {
    return {widget, nullptr, 0, Theme::togglePillWidth(text), Theme::kTogglePillHeight, false};
}

Chip chipFor(const CardBodyItem& item) {
    if (item.pill)
        return pillChip(item.widget, item.captionText());
    Chip chip;
    chip.widget = item.widget;
    chip.label = item.label;
    chip.labelWidth = captionWidth(item.captionText());
    chip.width = chip.labelWidth + cardbody::kFooterMinStretch;
    chip.height = item.kind == CardBodyItem::Kind::FaderH ? cardbody::kFooterRowHeight : cardbody::kRowHeight;
    chip.stretches = true;
    return chip;
}

// Footer items in layout order (a swap group shows only the members whose condition holds; a view
// has no place in the row), then the chrome toggles.
std::vector<Chip> footerChips(const CardBodyPlan& plan, const std::vector<CardFooterExtra>& extras) {
    std::vector<Chip> chips;
    for (const auto& section : plan.sections) {
        if (!section.footer || !section.visible)
            continue;
        for (int index : section.items) {
            const auto& item = plan.items[(size_t)index];
            if (item.kind != CardBodyItem::Kind::View && item.shown)
                chips.push_back(chipFor(item));
        }
    }
    for (const auto& extra : extras)
        chips.push_back(pillChip(extra.widget, extra.text));
    return chips;
}

std::vector<std::vector<Chip>> flowIntoRows(const std::vector<Chip>& chips, int width) {
    std::vector<std::vector<Chip>> rows(1);
    int used = 0;
    for (const auto& chip : chips) {
        const int needed = rows.back().empty() ? chip.width : used + cardbody::kFooterGap + chip.width;
        if (!rows.back().empty() && needed > width) {
            rows.emplace_back();
            used = chip.width;
        } else {
            used = needed;
        }
        rows.back().push_back(chip);
    }
    return rows;
}

// Pills keep their width; whatever is left is shared among the stretching chips.
void placeRow(const std::vector<Chip>& row, int y, const cardbody::BodyGeometry& g) {
    int preferred = 0;
    int stretching = 0;
    for (const auto& chip : row) {
        preferred += chip.width;
        stretching += chip.stretches ? 1 : 0;
    }
    const int gaps = cardbody::kFooterGap * ((int)row.size() - 1);
    const int spare = std::max(0, g.contentW - preferred - gaps);
    int x = g.contentX;
    int given = 0;
    for (const auto& chip : row) {
        int width = std::min(chip.width, g.contentW);
        if (chip.stretches) {
            const int share =
                ++given == stretching ? spare - (spare / stretching) * (stretching - 1) : spare / stretching;
            width += share;
        }
        if (chip.label != nullptr)
            chip.label->setBounds(x, y, chip.labelWidth, cardbody::kFooterRowHeight);
        if (chip.widget != nullptr)
            chip.widget->setBounds(x + chip.labelWidth, y + (cardbody::kFooterRowHeight - chip.height) / 2,
                                   width - chip.labelWidth, chip.height);
        x += width + cardbody::kFooterGap;
    }
}

} // namespace

int layoutCardBodyFooter(const CardBodyPlan& plan, const std::vector<CardFooterExtra>& extras, int y,
                         const cardbody::BodyGeometry& g, bool apply) {
    const auto chips = footerChips(plan, extras);
    if (chips.empty())
        return y;
    const auto rows = flowIntoRows(chips, g.contentW);
    y += cardbody::kFooterRowGap; // a little air between the body and the row
    for (const auto& row : rows) {
        if (apply)
            placeRow(row, y, g);
        y += cardbody::kFooterRowHeight + cardbody::kFooterRowGap;
    }
    return y;
}

int CardBody::layoutFooter(int y, const cardbody::BodyGeometry& g, bool apply,
                           const std::vector<juce::ToggleButton*>& chromeToggles) const {
    std::vector<CardFooterExtra> extras;
    for (auto* toggle : chromeToggles)
        if (toggle != nullptr)
            extras.push_back({toggle, toggle->getButtonText()});
    return layoutCardBodyFooter(plan_, extras, y, g, apply);
}

} // namespace synth
