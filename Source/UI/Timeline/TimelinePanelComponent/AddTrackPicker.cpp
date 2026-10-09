// AddTrackPicker.cpp -- flattens the "+ Track" menu into picker rows and launches the picker.
#include "AddTrackPicker.h"

namespace synth::ui {
namespace {

constexpr const char* kTracksHeader = "Tracks";
constexpr const char* kMoreHeader = "More";

juce::String headerForSubmenu(const juce::String& title) {
    if (title == "Plugin")
        return "Plugins";
    if (title == "Instrument Track")
        return "Instrument Tracks";
    return title;
}

// Words the search also matches that the row does not show.
juce::String extraSearchWords(const juce::String& header, const juce::String& text) {
    if (header == "Plugins")
        return "plugin instrument vst au";
    if (header == "Instrument Tracks")
        return "instrument synth";
    if (header.endsWith("Preset"))
        return "preset";
    if (text == "Add Marker")
        return "marker flag locator";
    if (text.startsWith("Insert Track Preset"))
        return "preset file import";
    if (header == kTracksHeader)
        return "new";
    return {};
}

juce::String disabledDetail(const juce::String& text) {
    if (text == "Create Channels")
        return "Every track already has a channel";
    return "Not available yet";
}

void flattenInto(const juce::PopupMenu& menu, const juce::String& submenuTitle,
                 std::vector<ModMatrixPicker::Item>& out) {
    juce::PopupMenu::MenuItemIterator it(menu, false);
    while (it.next()) {
        const auto& item = it.getItem();
        if (item.isSeparator || item.isSectionHeader)
            continue;
        if (item.subMenu != nullptr) {
            flattenInto(*item.subMenu, item.text, out);
            continue;
        }
        if (item.itemID <= 0 || item.text.isEmpty())
            continue;
        ModMatrixPicker::Item row;
        row.id = item.itemID;
        row.text = item.text;
        if (submenuTitle.isNotEmpty())
            row.category = headerForSubmenu(submenuTitle);
        else
            row.category = (item.text == "MIDI Track" || item.text == "Audio Track") ? kTracksHeader : kMoreHeader;
        row.searchText = extraSearchWords(row.category, item.text);
        row.enabled = item.isEnabled;
        if (!item.isEnabled)
            row.detail = disabledDetail(item.text);
        out.push_back(std::move(row));
    }
}
} // namespace

std::vector<ModMatrixPicker::Item> flattenAddTrackMenu(const juce::PopupMenu& menu) {
    std::vector<ModMatrixPicker::Item> rows;
    flattenInto(menu, {}, rows);
    return rows;
}

std::unique_ptr<ModMatrixPicker> buildAddTrackPicker(std::vector<ModMatrixPicker::Item> items,
                                                     std::function<void(int)> onPick, std::function<void()> onClosed) {
    auto picker = std::make_unique<ModMatrixPicker>("track", std::move(items), 0, std::move(onPick));
    picker->setAccessibleNames("Add track or marker", "Search tracks, instruments, plugins and presets");
    picker->onClosed = std::move(onClosed);
    return picker;
}

void showAddTrackPicker(juce::Component& anchor, std::unique_ptr<ModMatrixPicker> picker) {
    if (auto& hook = test_hooks::addTrackPickerHookForTest()) {
        hook(std::move(picker));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(picker), anchor.getScreenBounds(), nullptr);
}

namespace test_hooks {
std::function<void(std::unique_ptr<ModMatrixPicker>)>& addTrackPickerHookForTest() {
    static std::function<void(std::unique_ptr<ModMatrixPicker>)> hook;
    return hook;
}
} // namespace test_hooks

} // namespace synth::ui
