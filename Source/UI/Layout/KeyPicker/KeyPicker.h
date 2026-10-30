#pragma once

#include "Music/SongKey.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/CalloutReveal.h"
#include "UI/Layout/FilteredRowFades.h"
#include "UI/Layout/NavigationSearchField.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

namespace synth::ui {

class KeyPickerButton;
class KeyPickerFollowRow;
class KeyPickerKeyboard;
class KeyPickerRow;

/** The key picker: a popover for choosing a musical key (root plus scale). Lives in a juce::CallOutBox the caller
 *  launches (like ModMatrixPicker); it never launches one itself. A pick reports through `onPick` and closes the box.
 *
 *  Typing "d dor" sets the root to D and filters the scales; Up/Down move the highlight, Return picks it, Escape
 *  closes without a pick. A keyboard strip shows the highlighted key's notes, and clicking a key on it sets the root.
 */
class KeyPicker
    : public juce::Component
    , public synth::testmode::StateSource
    , private juce::Timer {
public:
    struct Options {
        std::optional<MusicalScale> current;     // the key shown ticked; nullopt = no key
        bool showFollowRow = false;              // a "Follow song key" row at the top
        bool following = false;                  // that row is on (ticked) instead of a key
        juce::String followDetail;               // muted text at the right of the follow row
        bool showNoKey = true;                   // the "No key" button
        std::vector<UserScale> userScales;       // a "Your scales" group at the end, each at the picker's root
        juce::String what = "Song key";          // accessible title prefix
        bool showPreviewSound = false;           // a "Preview sound" combo above the buttons
        std::vector<juce::String> previewTracks; // its choices after "Default sound", one per instrument track
        int previewSelected = 0;                 // 0 = Default sound, n = previewTracks[n - 1]
    };

    KeyPicker(Options options, std::function<void(std::optional<MusicalScale>)> onPick);
    ~KeyPicker() override;

    /** A key was picked (nullopt = no key); the picker closes. Callers may clear it when they go first. */
    std::function<void(std::optional<MusicalScale>)> onPick;
    /** The follow row was chosen; the picker closes. */
    std::function<void()> onFollow;
    /** The highlighted key settled for kAuditionDelayMs; nullopt when it leaves, or the picker closes. */
    std::function<void(std::optional<MusicalScale>)> onAudition;
    /** The Preview sound combo changed: 0 = Default sound, n = Options::previewTracks[n - 1]. */
    std::function<void(int choice)> onPreviewSoundChanged;
    /** "Detect from notes": the key that fits the notes. Unset hides the button; set it before the picker is shown. */
    std::function<std::optional<MusicalScale>()> detect;
    /** Runs once from the destructor, however the picker closed. */
    std::function<void()> onClosed;

    static constexpr int kAuditionDelayMs = 150;
    static constexpr int kWidth = 330;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void lookAndFeelChanged() override;
    void parentHierarchyChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void focusOfChildComponentChanged(FocusChangeType) override { repaint(); }

    void addTestModeState(juce::DynamicObject& into) const override;

    /** Closes the launching CallOutBox, if any. */
    void dismiss();

    // ---- Test seams ----
    void setQuery(const juce::String& text);
    int getRoot() const noexcept { return root_; }
    std::optional<MusicalScale> getHighlighted() const;
    std::vector<MusicalScale> visibleKeys() const;
    bool isVisibleKeyTicked(int index) const;
    juce::String getStatusText() const;
    bool isRelativeEnabled() const;
    bool isDetectShown() const;
    juce::String getRelativeText() const;
    void pickHighlighted();
    void clickRelative();
    void clickDetect();
    void clickNoKey();
    void clickFollow();
    void clickKeyboardRoot(int pitchClass);
    bool isPreviewSoundShown() const;
    juce::StringArray getPreviewSoundItems() const;
    int getPreviewSoundChoice() const;
    void choosePreviewSound(int choice);
    void hoverVisibleKey(int index);
    bool sendKey(const juce::KeyPress& key);
    std::vector<juce::String> visibleGroupTitles() const;

private:
    CalloutReveal reveal_{*this};

    void buildPreviewSound();
    void buildRows();
    void applyFilter();
    void layoutRowColumn();
    void setRoot(int root);
    void refreshTicks();
    void moveHighlight(int delta);
    void setHighlight(int index);
    void highlightKey(const MusicalScale& key);
    void highlightChanged();
    void hoverRow(KeyPickerRow& row);
    void pick(std::optional<MusicalScale> key);
    void applyColours();
    void setStatus(const juce::String& text);
    std::optional<MusicalScale> relativeTarget() const;
    void updateRelativeButton();
    std::vector<KeyPickerRow*> visibleItemRows() const;
    void timerCallback() override;
    void endAudition();
    static int preferredHeight(bool followRow, bool previewRow);

    Options options_;
    int root_ = 0;
    bool ready_ = false;
    bool auditioning_ = false;
    std::optional<MusicalScale> lastHighlight_;

    std::unique_ptr<KeyPickerFollowRow> follow_;
    std::unique_ptr<NavigationSearchField> search_;
    std::unique_ptr<KeyPickerKeyboard> keyboard_;
    juce::Viewport viewport_;
    juce::Component rowColumn_;
    std::vector<std::unique_ptr<KeyPickerRow>> rows_;
    FilteredRowFades fades_; // index-aligned with rows_, declared after it so it goes first
    int highlighted_ = 0;    // index among the visible scale rows
    juce::Label status_;
    std::unique_ptr<KeyPickerButton> relative_;
    std::unique_ptr<KeyPickerButton> detectButton_;
    std::unique_ptr<KeyPickerButton> noKey_;
    juce::Label previewLabel_;
    juce::ComboBox previewCombo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyPicker)
};

} // namespace synth::ui
