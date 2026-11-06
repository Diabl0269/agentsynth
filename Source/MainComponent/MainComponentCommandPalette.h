#pragma once

// The command palette's MainComponent wiring: what it lists (actions, views, modules, tracks, markers, settings,
// projects, presets and, after "+", the Add list) and what a pick runs. A friend of MainComponent. Message thread only.

#include "Timeline/TimelineDoc/TimelineTypes.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

class MainComponent;
namespace synth::ui {
class CommandPaletteComponent;
struct PaletteEntry;
struct AddListLibraryRow;
} // namespace synth::ui

struct MainComponentCommandPalette {
    explicit MainComponentCommandPalette(MainComponent& owner);
    ~MainComponentCommandPalette();

    /** `mc`'s palette, made on first use. */
    static MainComponentCommandPalette& of(MainComponent& mc);
    /** Opens the palette, or closes it when it is open (Cmd+K, the top bar's Search button). */
    static void toggle(MainComponent& mc);

    /** Runs Add list row `id` exactly as a pick in the Add list does. `libraryRows` are the rows the list was built
     *  from; `handBackFocus` runs where a keyboard pick hands focus back to the button that opened the list. */
    static void runAddListPick(MainComponent& mc, const std::vector<synth::ui::AddListLibraryRow>& libraryRows, int id,
                               bool fromKeyboard, bool fromCanvas, const std::function<void()>& handBackFocus);

    void open();
    /** Closes with a fade; keyboard focus goes back to what had it when the palette opened. */
    void close();
    bool isOpen() const noexcept { return palette_ != nullptr && !closing_; }

    // ---- Test seams ----
    synth::ui::CommandPaletteComponent* getPaletteForTest() const noexcept { return palette_.get(); }
    /** The component focus was handed back to by the last close (null when it was gone). */
    juce::Component* getFocusRestoredForTest() const noexcept { return focusRestoredForTest_; }
    /** When set, replaces the plugin test for a track row's window button and the window toggle itself. */
    std::function<bool(synth::TrackId)> isPluginTrackForTest;
    std::function<void(synth::TrackId)> togglePluginWindowForTest;

private:
    void rebuildEntries();
    void addActionEntries();
    void addPlaceEntries();
    void addModuleEntries();
    void addTrackAndMarkerEntries();
    void addSettingEntries();
    void addFileEntries();
    struct AddRows;
    const AddRows& addRows();

    void pick(int id);
    void pressRowAction(int id);
    void flipSetting(size_t entryIndex);
    void rememberPick(const juce::String& key);
    void restoreFocus();
    void savePosition(juce::Point<int> topLeft);
    juce::Point<int> startPosition() const;

    MainComponent& mc_;
    std::unique_ptr<synth::ui::CommandPaletteComponent> palette_;
    bool closing_ = false;
    std::vector<synth::ui::PaletteEntry> entries_;
    std::vector<std::function<void()>> runners_; // one per entry, same index
    std::vector<synth::TrackId> entryTracks_;    // per entry: the track a row's window button acts on
    std::shared_ptr<std::vector<synth::ui::AddListLibraryRow>> libraryRows_;
    std::unique_ptr<AddRows> addRows_; // built the first time a query asks for them
    juce::Component::SafePointer<juce::Component> focusBeforeOpen_;
    juce::Component* focusRestoredForTest_ = nullptr;
};
