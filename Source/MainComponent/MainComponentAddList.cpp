// MainComponentAddList.cpp -- the one searchable "Add" list: tracks, modules, presets, snippets and markers together.
// The Timeline supplies the track rows and the module library the library rows (AddList.h orders and tags them); a
// pick runs the same code the Timeline's Add Track picker or Return on a sidebar row runs
// (MainComponentCommandPalette::runAddListPick, which the command palette's "+" rows run too).
#include "MainComponent.h"
#include "MainComponentCommandPalette.h"
#include "UI/AddList/AddList.h"
#include "UI/Timeline/TimelinePanelComponent/AddTrackPicker.h"
#include <memory>
#include <vector>

namespace {
// The strings a screen reader and the search field start with.
constexpr const char* kAddListTitle = "Add";
constexpr const char* kAddListSearch = "Search tracks, modules and presets";
} // namespace

bool MainComponent::openAddList(juce::Component& anchor, bool fromKeyboard) {
    openAddList(synth::ui::AddListOrigin::Timeline, anchor, fromKeyboard);
    return true;
}

// The library rows are read once, when the list opens, and a pick finds its row again by identity (a plugin scan or a
// snippet change in between cannot re-point it); the Timeline rows resolve against the menu's own build-time
// snapshots. Keyboard opens hand focus back to the button that opened the list, picked or not.
void MainComponent::openAddList(synth::ui::AddListOrigin origin, juce::Component& anchor, bool fromKeyboard) {
    using namespace synth::ui;
    auto libraryRows = std::make_shared<std::vector<AddListLibraryRow>>(moduleLibrary.getAddListRows());
    auto items = buildAddListItems(flattenAddTrackMenu(timelinePanel.buildAddMenuWithChannels()), *libraryRows, origin);

    juce::Component::SafePointer<MainComponent> safe(this);
    auto picked = std::make_shared<bool>(false);
    const bool fromCanvas = origin == AddListOrigin::Canvas;

    auto handBackFocus = [safe, fromKeyboard, fromCanvas] {
        if (!fromKeyboard || safe == nullptr)
            return;
        if (fromCanvas)
            safe->canvasToolbar_.focusSlot(CanvasToolbar::Add);
        else
            safe->timelinePanel.finishAddTrackMenu(0, true);
    };
    auto onPick = [safe, picked, libraryRows, fromKeyboard, fromCanvas, handBackFocus](int id) {
        *picked = true;
        if (safe != nullptr)
            MainComponentCommandPalette::runAddListPick(*safe, *libraryRows, id, fromKeyboard, fromCanvas,
                                                        handBackFocus);
    };
    auto onClosed = [picked, handBackFocus] {
        if (!*picked)
            handBackFocus();
    };

    auto picker = buildAddTrackPicker(std::move(items), std::move(onPick), std::move(onClosed));
    picker->setAccessibleNames(kAddListTitle, kAddListSearch);
    picker->setSearchPlaceholder(kAddListSearch);
    showAddTrackPicker(anchor, std::move(picker));
}
