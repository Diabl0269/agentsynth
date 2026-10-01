// ModuleLibraryAccessibility.cpp -- how the module library reaches a screen-reader user: it is a
// list whose spoken value follows the keyboard-focused row ("Oscillator, 3 of 12, Sources"). The rows
// are painted, not components, so the list carries the text itself, the way the piano roll does for
// its focused note.
#include "ModuleLibraryComponent.h"

#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/Library/ModuleLibraryAccessibilityText.h"

namespace {

class LibraryValueInterface : public juce::AccessibilityTextValueInterface {
public:
    explicit LibraryValueInterface(const ModuleLibraryComponent& library)
        : library_(library) {}

    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return library_.getAccessibilityValueText(); }
    void setValueAsString(const juce::String&) override {}

private:
    const ModuleLibraryComponent& library_;
};

} // namespace

juce::String ModuleLibraryComponent::getAccessibilityValueText() const {
    using namespace synth::ui;
    const auto navigable = navigableEntryIndices();
    const auto isChildRow = [this](int index) {
        const auto kind = entries[(size_t)index].kind;
        return kind != RowKind::Header && kind != RowKind::SubHeader && kind != RowKind::EmptyHint;
    };

    if (keyboardFocusedIndex < 0 || keyboardFocusedIndex >= (int)entries.size())
        return describeLibraryForAccessibility((int)navigable.size());

    const auto& focused = entries[(size_t)keyboardFocusedIndex];
    if (focused.kind == RowKind::Header || focused.kind == RowKind::SubHeader) {
        const bool isHeader = focused.kind == RowKind::Header;
        int items = 0;
        for (size_t i = 0; i < entries.size(); ++i)
            if (isChildRow((int)i) && entries[i].section == (isHeader ? focused.text : focused.section))
                ++items;
        const auto key = isHeader ? focused.text : subsectionKey(focused.section, focused.text);
        return describeLibrarySectionForAccessibility(focused.text, isSectionCollapsed(key), items);
    }

    int position = 0;
    int count = 0;
    for (const int index : navigable) {
        if (!isChildRow(index) || entries[(size_t)index].section != focused.section)
            continue;
        ++count;
        if (index == keyboardFocusedIndex)
            position = count;
    }
    return describeLibraryRowForAccessibility(focused.text, position, count, focused.section);
}

std::unique_ptr<juce::AccessibilityHandler> ModuleLibraryComponent::createAccessibilityHandler() {
    return std::make_unique<synth::ui::TooltipHelpHandler>(
        *this, juce::AccessibilityRole::list, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<LibraryValueInterface>(*this)});
}

// A value-changed event is posted only when the text differs from the last one handed out, so a
// repaint or a refresh that leaves the focused row alone never re-reads it.
void ModuleLibraryComponent::refreshAccessibilityValue() {
    const auto text = getAccessibilityValueText();
    if (text == announcedValueText)
        return;
    announcedValueText = text;
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}
