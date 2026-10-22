// SoundBrowserAccessibility.cpp -- how the Sounds view reaches a screen reader: one list whose spoken value follows the
// selected row ("acoustic-snare-center, one-shot, 0.8 seconds, 2 of 23"), the way the module library does it for its
// painted rows.
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"

namespace synth::ui {

namespace {

class SoundListValue : public juce::AccessibilityTextValueInterface {
public:
    explicit SoundListValue(const SoundBrowserComponent& owner)
        : owner_(owner) {}
    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return owner_.getAccessibilityValueText(); }
    void setValueAsString(const juce::String&) override {}

private:
    const SoundBrowserComponent& owner_;
};

} // namespace

juce::String SoundBrowserComponent::getAccessibilityValueText() const {
    int count = 0;
    int position = 0;
    for (int i = 0; i < (int)rows_.size(); ++i) {
        if (!rows_[(size_t)i].navigable())
            continue;
        ++count;
        if (i == selected_)
            position = count;
    }
    if (position == 0) {
        juce::StringArray labels;
        for (const auto& p : getPathParts())
            labels.add(p.label);
        const auto items = count == 1 ? juce::String("1 item") : juce::String(count) + " items";
        return isSearchActive() ? "Search results, " + items : describePathForAccessibility(labels) + ", " + items;
    }
    const auto& row = rows_[(size_t)selected_];
    switch (row.kind) {
    case RowKind::Sound:
        return describeSoundForAccessibility(*row.entry, position, count);
    case RowKind::Folder:
        return describeFolderForAccessibility(row.text, row.soundCount, position, count);
    case RowKind::Place:
        return describePlaceForAccessibility(row.text, row.soundCount, position, count);
    case RowKind::Hint:
        break;
    }
    return row.text;
}

std::unique_ptr<juce::AccessibilityHandler> SoundBrowserComponent::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(
        *this, juce::AccessibilityRole::list, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<SoundListValue>(*this)});
}

// A value-changed event goes out only when the text differs from the last one handed out.
void SoundBrowserComponent::refreshAccessibilityValue() {
    const auto text = getAccessibilityValueText();
    if (text == announcedValueText_)
        return;
    announcedValueText_ = text;
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

} // namespace synth::ui
