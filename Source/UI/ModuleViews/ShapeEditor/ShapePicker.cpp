// ShapePicker.cpp -- the thumbnail rows of the shape picker menu.
#include "ShapePicker.h"
#include "UI/Library/SoundBrowser/ShapeCurveView.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kRowHeight = 30;
constexpr int kSidePadding = 10;
constexpr int kThumbWidth = 52;
constexpr int kThumbHeight = 20;
constexpr int kGap = 10;
constexpr int kNameWidth = 150;

/** One menu row: the picture of the curve, the name and the badge. The menu owns the highlight and the keys. */
class ShapePickerRow : public juce::PopupMenu::CustomComponent {
public:
    explicit ShapePickerRow(ShapePickerEntry entry)
        : juce::PopupMenu::CustomComponent(true)
        , entry_(std::move(entry)) {
        // The menu item announces the row; the picture and badge inside it are not separate stops.
        setInterceptsMouseClicks(false, false);
    }

    void getIdealSize(int& width, int& height) override {
        width = kSidePadding + kThumbWidth + kGap + kNameWidth + kGap + badgeWidth(entry_.badge) + kSidePadding;
        height = kRowHeight;
    }

    void paint(juce::Graphics& g) override {
        const auto& theme = synth::theme::themeOf(*this);
        const auto area = getLocalBounds();
        if (isItemHighlighted()) {
            g.setColour(theme.colors.accent.withAlpha(0.18f));
            g.fillRect(area);
        }
        const auto thumb = juce::Rectangle<float>((float)kSidePadding, (float)(area.getHeight() - kThumbHeight) * 0.5f,
                                                  (float)kThumbWidth, (float)kThumbHeight);
        paintShapeThumbnail(g, theme, entry_.curve, thumb);
        const int badgeW = badgeWidth(entry_.badge);
        const int nameLeft = kSidePadding + kThumbWidth + kGap;
        g.setColour(theme.colors.textPrimary);
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(entry_.name, nameLeft, 0, area.getWidth() - nameLeft - badgeW - kGap - kSidePadding,
                   area.getHeight(), juce::Justification::centredLeft, true);
        paintBadge(g, theme, entry_.badge,
                   juce::Rectangle<float>((float)(area.getWidth() - kSidePadding - badgeW),
                                          (float)(area.getHeight() - kBadgeHeight) * 0.5f, (float)badgeW,
                                          (float)kBadgeHeight));
    }

private:
    ShapePickerEntry entry_;
};
} // namespace

std::vector<ShapePickerEntry> libraryPickerEntries(const synth::shapes::ShapeLibrary& library,
                                                   synth::shapes::ShapeKind kind) {
    std::vector<ShapePickerEntry> out;
    for (auto& shape : library.allShapes(kind))
        out.push_back({shape.name, shape.curve, shape.factory ? BadgeKind::Factory : BadgeKind::Yours, shape});
    return out;
}

juce::String pickerEntryTitle(const ShapePickerEntry& entry) { return entry.name + ", " + badgeText(entry.badge); }

void addShapePickerItem(juce::PopupMenu& menu, const ShapePickerEntry& entry,
                        std::function<void(const ShapePickerEntry&)> onPick) {
    juce::PopupMenu::Item item;
    item.text = pickerEntryTitle(entry);
    item.itemID = -1;
    item.customComponent = new ShapePickerRow(entry);
    item.action = [entry, onPick = std::move(onPick)] {
        if (onPick)
            onPick(entry);
    };
    menu.addItem(std::move(item));
}

juce::PopupMenu makeShapePickerMenu(const std::vector<ShapePickerEntry>& entries,
                                    std::function<void(const ShapePickerEntry&)> onPick) {
    juce::PopupMenu menu;
    bool yoursHeader = false;
    bool factoryHeader = false;
    for (const auto& entry : entries) {
        if (entry.badge == BadgeKind::Yours && !yoursHeader) {
            menu.addSectionHeader("Your shapes");
            yoursHeader = true;
        } else if (entry.badge != BadgeKind::Yours && !factoryHeader) {
            if (yoursHeader)
                menu.addSeparator();
            menu.addSectionHeader("Factory shapes");
            factoryHeader = true;
        }
        addShapePickerItem(menu, entry, onPick);
    }
    return menu;
}

} // namespace synth::ui
