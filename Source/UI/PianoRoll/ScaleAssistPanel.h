#pragma once

// Scale-assist panel for the piano roll: the clip's Key field (follow the song key or its own), a custom-scale editor,
// the show-only-scale-notes toggle and a random-generation block. Pitch-quantize is deliberately NOT here: it has
// exactly one entry point now, the roll's header chip (plus its Option+Shift+Q key). A button that duplicated it inside
// a panel the user has to open first was a second door to the same room, and the two could only ever disagree about
// their enabled state. Deliberately dumb — it holds no reference to the roll, no TimelineDoc and no undo manager. Every
// action the roll needs to react to travels OUT through a std::function callback, and every piece of state the roll
// needs to push BACK in (switching clips, restoring a persisted scale) travels IN through
// setSelection() — the same "panel owns presentation, owner owns the doc" split
// PianoRollComponent itself follows for TimelineViewState.
//
// User scales persist through synth::serializeUserScales/parseUserScales under one
// juce::PropertiesFile key ("pianoRollUserScales"), the same one-key idiom NoteColour.h's own
// persistence follows. setPropertiesFile(nullptr) is a legal, PERMANENT state: Save still works,
// it is simply session-only (nothing survives past this juce::PropertiesFile instance) — the same
// null-degrades-gracefully contract every other timeline sub-component's setter follows.

#include "Music/MusicalScale.h"
#include "Music/SongKey.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Layout/KeyField.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/Layout/NoteRangePicker/NoteRangePicker.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <array>
#include <cstdint>
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

namespace synth::ui {

class ScaleAssistPanel : public juce::Component {
public:
    ScaleAssistPanel() {
        setComponentID("scaleAssistPanel");
        // Its own focus region (Tab: timeline -> routing pane -> scale pane): the root takes focus, Up/Down step
        // through the controls and Escape comes back. A click never moves keyboard focus onto the root.
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        setTitle("Scale assist");
        buildKeyControls();
        buildCustomScaleEditor();
        buildPitchVisibilityControl();
        buildRandomGenerationControls();
        // Every control above lives in `scaleContent_`, the scrolled child of `scrollViewport_`.
        // The viewport shows its vertical scrollbar (and clips its child) only while the content's
        // natural height exceeds the panel's own height, so when the panel IS tall enough the whole
        // sidebar is visible and there is nothing to scroll. A too-short panel (a cramped timeline)
        // gets the scrollbar and the user scrolls within the sidebar instead of having its tail
        // controls squished off the bottom. See resized()/contentNaturalHeight().
        addAndMakeVisible(scrollViewport_);
        scrollViewport_.setComponentID("scaleAssistScrollViewport");
        scrollViewport_.setTitle("Scale assist options");
        scrollViewport_.setViewedComponent(&scaleContent_, /*deleteComponentWhenNoLongerNeeded=*/false);
        scrollViewport_.setScrollBarsShown(/*vertical=*/true, /*horizontal=*/false);
        showCustomEditor(false);
        paneKeys_.attachToDescendants();
        // The custom scale block is hidden here, so attachToDescendants() skipped its controls.
        for (auto& toggle : customPitchToggles_)
            paneKeys_.attach(toggle);
        paneKeys_.attach(customScaleNameEditor_);
        paneKeys_.attach(saveCustomScaleButton_);
    }
    ~ScaleAssistPanel() override {
        if (auto* picker = getOpenKeyPicker()) {
            picker->onPick = nullptr;
            picker->onFollow = nullptr;
        }
    }

    bool keyPressed(const juce::KeyPress& key) override {
        const bool plain = !key.getModifiers().isAnyModifierKeyDown();
        if (plain && key.isKeyCode(juce::KeyPress::downKey))
            return stepFocusWithin(*this, +1, nullptr);
        return plain && key.isKeyCode(juce::KeyPress::upKey);
    }

    void paintOverChildren(juce::Graphics& g) override { paintFocusRegionOutline(*this, g); }

    void paint(juce::Graphics& g) override {
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        // A hairline against whatever sits to our right (the roll's keys column) — enough to read
        // as a separate panel without a themed border token of its own.
        g.setColour(findColour(juce::Label::textColourId).withAlpha(0.15f));
        g.drawVerticalLine(getWidth() - 1, 0.0f, (float)getHeight());
    }

    void resized() override {
        // Every control lives in `scaleContent_`, the scrolled child of `scrollViewport_`. The
        // viewport shows its vertical scrollbar and clips its child only while the content's
        // natural height exceeds the panel's own — so a tall-enough panel shows the whole sidebar
        // and there is nothing to scroll, while a cramped one gets the scrollbar to reach its tail.
        scrollViewport_.setBounds(getLocalBounds());
        scaleContent_.setSize(juce::jmax(0, scrollViewport_.getWidth()), juce::jmax(0, contentNaturalHeight()));
        layoutContentInto(scaleContent_.getLocalBounds());
        scrollViewport_.repaint();
    }

    // The custom editor block: sharps row + gap, naturals row + gap, name row + gap.
    static constexpr int kCustomBlockHeight = 18 + 2 + 24 + 4 + 22 + 6;
    // What the block takes of the content: all of it once shown, none when hidden, and in between while it fades,
    // so the rows below slide with the fade instead of jumping.
    int customBlockHeight() const { return juce::roundToInt((float)kCustomBlockHeight * customFade_.progress()); }

    // Height the sidebar needs at full size: the 6 px inset on each side plus every row that
    // layoutContentInto consumes (the custom-editor block only while it is showing). It mirrors
    // that layout's row sizes (a test pins the two together: the last row's bottom plus the inset).
    int contentNaturalHeight() const {
        constexpr int kInset = 6; // matches the inset layoutContentInto takes off the content area
        int y = kInset;
        y += 22 + 4;                // key field + gap
        y += 22 + 6;                // custom scales button + gap
        y += customBlockHeight();   // the custom editor: its full height once shown, tweened with its fade
        y += 22 + 10;               // pitch-visibility toggle + gap
        y += kRangeLabelHeight + 2; // "Note range" label + gap
        y += NoteRangePicker::kPreferredHeight + 6; // range picker + gap
        y += 24 + 2;                                // generate button + gap
        y += 20;                                    // add-to-existing toggle
        return y + kInset;
    }

    // The one layout pass, positioning every control inside `area` (the content component's
    // local space). No panel height enters here — it just lays out to full size; scrolling is
    // the viewport's job above. Kept in lockstep with contentNaturalHeight.
    void layoutContentInto(juce::Rectangle<int> area) {
        auto bounds = area.reduced(6);

        keyField_.setBounds(bounds.removeFromTop(22));
        bounds.removeFromTop(4);

        customScalesButton_.setBounds(bounds.removeFromTop(22));
        bounds.removeFromTop(6);

        // The custom editor lives in `customBlock_`, which clips it: while it fades the block is only as tall as the
        // fade has got and its rows (always laid out at full size, from the block's top) are cut off at the bottom.
        const auto blockArea = bounds.removeFromTop(customBlockHeight());
        customBlock_.setBounds(blockArea);
        {
            juce::Rectangle<int> inner(0, 0, blockArea.getWidth(), kCustomBlockHeight);
            // A mini one-octave keyboard rather than a flat row of checkboxes: sharps get their
            // own row ABOVE the naturals, each one centred on the boundary between the two
            // naturals it sits between on a real keyboard — see layoutMiniKeyboard.
            auto sharpsRow = inner.removeFromTop(18);
            inner.removeFromTop(2);
            auto naturalsRow = inner.removeFromTop(24);
            layoutMiniKeyboard(sharpsRow, naturalsRow);
            inner.removeFromTop(4);

            auto nameRow = inner.removeFromTop(22);
            saveCustomScaleButton_.setBounds(nameRow.removeFromRight(56));
            nameRow.removeFromRight(4);
            customScaleNameEditor_.setBounds(nameRow);
        }

        pitchVisibilityToggle_.setBounds(bounds.removeFromTop(22));
        bounds.removeFromTop(10);

        rangeLabel_.setBounds(bounds.removeFromTop(kRangeLabelHeight));
        bounds.removeFromTop(2);
        rangePicker_.setBounds(bounds.removeFromTop(NoteRangePicker::kPreferredHeight));
        bounds.removeFromTop(6);

        generateButton_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(2);
        // The mode control sits UNDER Generate rather than beside it: at kScalePanelWidth (170 px,
        // less the 6 px inset) a button-plus-checkbox row would leave the checkbox's label truncated
        // to a few characters, and "Add to existing" has to be readable for the button above it to
        // mean anything.
        addToExistingToggle_.setBounds(bounds.removeFromTop(20));
    }

    // ---- Persistence (user scales only — panel visibility is the ROLL's own key) ----

    // Null-safe: with no PropertiesFile, Save still appends/overwrites userScales_ for the
    // session, it just never reaches disk. Re-parses and REPLACES the in-memory list from `props`
    // when non-null, since a freshly-wired properties file is the canonical on-disk state.
    void setPropertiesFile(juce::PropertiesFile* props) {
        propertiesFile_ = props;
        if (propertiesFile_ != nullptr)
            userScales_ = synth::parseUserScales(propertiesFile_->getValue(kUserScalesPropertyKey, {}));
    }

    // ---- State the owner pushes IN (never fires a callback — this is a REFLECTION, not an
    // edit the user made) ----
    // `scale` is the key in force (the song key when `following`).
    void setSelection(std::optional<synth::MusicalScale> scale, bool pitchVisibilityOn, bool following = false) {
        selectedScale_ = scale;
        following_ = following;
        pitchVisibilityOn_ = pitchVisibilityOn;
        showSelectedKey();
        pitchVisibilityToggle_.setToggleState(pitchVisibilityOn_, juce::dontSendNotification);
        showCustomEditor(false);
        repaint();
    }
    /** The song key the picker's Follow row names; a reflection, like setSelection. */
    void setSongKey(std::optional<synth::MusicalScale> songKey) { songKey_ = std::move(songKey); }

    std::optional<synth::MusicalScale> getSelectedScale() const noexcept { return selectedScale_; }
    bool isFollowingSongKey() const noexcept { return following_; }
    bool isPitchVisibilityOn() const noexcept { return pitchVisibilityOn_; }

    int getMinPitchSelection() const noexcept { return rangePicker_.getLow(); }
    int getMaxPitchSelection() const noexcept { return rangePicker_.getHigh(); }
    /** false (the default) means Generate REPLACES the clip's contents, true means it overlays the
     *  generated notes on top of whatever is already there. Replace stays the default because it is
     *  what "generate a pattern" meant before this control existed — a persisted preference would
     *  make the button's effect depend on invisible state, so this is deliberately session-only and
     *  starts off on every fresh panel. */
    bool isAddToExistingSelected() const noexcept { return addToExistingToggle_.getToggleState(); }

    // ---- Callbacks the owner wires (see the class comment) ----
    std::function<void(std::optional<synth::MusicalScale>)> onScaleChanged; // an own key was picked
    std::function<void()> onFollowSongKey;                                  // the Follow song key row
    /** Launches the key picker; unset = a CallOutBox from the field. Tests replace it to keep the picker. */
    std::function<void(std::unique_ptr<juce::Component> picker, juce::Rectangle<int> screenArea)> pickerLauncher;

    void openKeyPicker() {
        if (openPicker_ != nullptr)
            return;
        KeyPicker::Options options;
        options.current = following_ ? std::nullopt : selectedScale_;
        options.showFollowRow = true;
        options.following = following_;
        options.followDetail = songKey_.has_value() ? synth::keyDisplayName(*songKey_) : juce::String("No key");
        options.showNoKey = false; // an own key is always a key; "no key" is following a song with none
        options.userScales = userScales_;
        options.what = "Key";
        juce::Component::SafePointer<ScaleAssistPanel> self(this);
        auto picker = std::make_unique<KeyPicker>(std::move(options), [self](std::optional<synth::MusicalScale> key) {
            if (self != nullptr && key.has_value())
                self->pickOwnKey(*key);
        });
        picker->onFollow = [self] {
            if (self != nullptr && self->onFollowSongKey)
                self->onFollowSongKey();
        };
        openPicker_ = picker.get();
        const auto area = keyField_.getScreenBounds();
        if (pickerLauncher)
            pickerLauncher(std::move(picker), area);
        else
            juce::CallOutBox::launchAsynchronously(std::move(picker), area, nullptr);
    }
    KeyPicker* getOpenKeyPicker() const noexcept { return dynamic_cast<KeyPicker*>(openPicker_.getComponent()); }
    std::function<void(bool)> onPitchVisibilityChanged;
    // `addToExisting` is the toggle's state at the moment Generate was pressed, passed BY VALUE
    // rather than left for the owner to read back off the panel: the owner's handler mutates the doc
    // (and can repaint/rebuild), so nothing it needs may depend on this component still being in the
    // same state — or alive — afterwards.
    std::function<void(int minPitch, int maxPitch, bool addToExisting)> onGenerate;

    // ---- Test accessors (every interactive child also carries its own componentID) ----
    PaneKeys& getPaneKeysForTest() noexcept { return paneKeys_; }
    KeyField& getKeyField() noexcept { return keyField_; }
    juce::TextButton& getCustomScalesButton() noexcept { return customScalesButton_; }
    juce::ToggleButton& getPitchVisibilityToggle() noexcept { return pitchVisibilityToggle_; }
    juce::TextButton& getGenerateButton() noexcept { return generateButton_; }
    juce::ToggleButton& getAddToExistingToggle() noexcept { return addToExistingToggle_; }
    NoteRangePicker& getRangePicker() noexcept { return rangePicker_; }
    juce::TextEditor& getCustomScaleNameEditor() noexcept { return customScaleNameEditor_; }
    juce::TextButton& getSaveCustomScaleButton() noexcept { return saveCustomScaleButton_; }
    juce::ToggleButton& getCustomPitchToggle(int pitchClass) noexcept {
        return customPitchToggles_[(size_t)juce::jlimit(0, 11, pitchClass)];
    }
    bool isCustomEditorVisibleForTest() const noexcept { return customEditorVisible_; }
    const std::vector<synth::UserScale>& getUserScalesForTest() const noexcept { return userScales_; }
    // The viewport is the object that decides the scroll; exposing it (and the natural content height
    // it compares against) lets a test assert "bar shows only when content overflows" without any
    // paint or device.
    juce::Viewport& getScrollViewportForTest() noexcept { return scrollViewport_; }
    int getContentNaturalHeightForTest() const noexcept { return contentNaturalHeight(); }
    // The custom editor's block (keys, name, Save): it is what fades, and it clips its rows to the height reached.
    juce::Component& getCustomBlockForTest() noexcept { return customBlock_; }

private:
    static constexpr int kDefaultMinPitch = 36; // C2
    static constexpr int kDefaultMaxPitch = 72; // C5
    static constexpr int kRangeLabelHeight = 14;
    static constexpr const char* kUserScalesPropertyKey = synth::kUserScalesSettingsKey;

    // A custom-scale toggle painted as a small piano KEY rather than a checkbox+label: it overrides
    // paintButton() and draws through AppLookAndFeel::drawKeyToggle (the stock drawToggleButton always
    // draws a tick box, which reads nothing like a keyboard). isBlack_ picks the pianoKeyBlack or
    // pianoKeyWhite base fill (set once at construction time in buildCustomScaleEditor); the accent fill
    // on top of it IS the toggled-on state, so there is no separate tick. Still a plain juce::ToggleButton
    // underneath: getToggleState()/setToggleState()/onClick/getButtonText() are untouched.
    class PianoKeyToggle : public juce::ToggleButton {
    public:
        void setBlackKey(bool isBlack) noexcept { isBlack_ = isBlack; }

        void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override {
            if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
                lf->drawKeyToggle(g, *this, isBlack_, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
            else
                synth::theme::paintKeyToggle(g, *this, synth::theme::themeOf(*this), isBlack_,
                                             shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        }

    private:
        bool isBlack_ = false;
    };

    static bool isBlackPitchClass(int pitchClass) noexcept {
        switch (pitchClass) {
        case 1:
        case 3:
        case 6:
        case 8:
        case 10:
            return true;
        default:
            return false;
        }
    }

    // Natural-key pitch classes in KEYBOARD order (C D E F G A B), and, for each of the six gaps
    // between adjacent naturals, which sharp sits there — kNoSharp marks the two gaps with no black
    // key at all (E/F, B/C), mirroring isBlackPitchClass above.
    static constexpr int kNaturalOrder[7] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int kNoSharp = -1;
    static constexpr int kSharpAfterNatural[6] = {1, 3, kNoSharp, 6, 8, 10};

    // Lays the 12 toggles out as a mini one-octave keyboard: naturals fill `naturalsRow` evenly,
    // sharps sit in their OWN row above (`sharpsRow`), each centred on the boundary between the two
    // naturals it falls between on a real keyboard. The two rows never share a y-range, so a sharp
    // and a natural can never overlap regardless of how their x positions land.
    void layoutMiniKeyboard(juce::Rectangle<int> sharpsRow, juce::Rectangle<int> naturalsRow) {
        const int naturalWidth = juce::jmax(1, naturalsRow.getWidth() / 7);
        for (int i = 0; i < 7; ++i) {
            const int x = naturalsRow.getX() + i * naturalWidth;
            // The last cell soaks up the rounding remainder so the row's right edge lands exactly on
            // the panel's own right edge instead of short by a pixel or two.
            const int w = (i == 6) ? naturalsRow.getRight() - x : naturalWidth;
            customPitchToggles_[(size_t)kNaturalOrder[i]].setBounds(
                juce::Rectangle<int>(x, naturalsRow.getY(), w, naturalsRow.getHeight()).reduced(1, 0));
        }

        const int sharpWidth = juce::jmax(1, naturalWidth * 5 / 8);
        for (int gap = 0; gap < 6; ++gap) {
            const int pitchClass = kSharpAfterNatural[gap];
            if (pitchClass == kNoSharp)
                continue;
            const int boundaryX = naturalsRow.getX() + (gap + 1) * naturalWidth;
            customPitchToggles_[(size_t)pitchClass].setBounds(
                juce::Rectangle<int>(boundaryX - sharpWidth / 2, sharpsRow.getY(), sharpWidth, sharpsRow.getHeight()));
        }
    }

    void buildKeyControls() {
        scaleContent_.addAndMakeVisible(keyField_);
        keyField_.setComponentID("scaleAssistKeyField");
        keyField_.onOpen = [this] { openKeyPicker(); };
        keyField_.setFollowing(true);

        scaleContent_.addAndMakeVisible(customScalesButton_);
        customScalesButton_.setComponentID("scaleAssistCustomScalesButton");
        customScalesButton_.setTooltip("Make your own scale from any notes; it joins Your scales in the key picker");
        customScalesButton_.onClick = [this] { showCustomEditor(!customEditorVisible_); };
    }

    void buildCustomScaleEditor() {
        for (int pc = 0; pc < 12; ++pc) {
            auto& toggle = customPitchToggles_[(size_t)pc];
            customBlock_.addAndMakeVisible(toggle);
            toggle.setComponentID("scaleAssistCustomToggle" + juce::String(pc));
            toggle.setButtonText(pitchClassName(pc));
            toggle.setTitle("Custom scale note " + pitchClassName(pc));
            toggle.setTooltip("Include " + pitchClassName(pc) + " in the custom scale");
            toggle.setBlackKey(isBlackPitchClass(pc));
        }

        scaleContent_.addChildComponent(customBlock_);
        customFade_.onFrame = [this] { resized(); };
        customBlock_.addAndMakeVisible(customScaleNameEditor_);
        customScaleNameEditor_.setComponentID("scaleAssistCustomNameEditor");
        customScaleNameEditor_.setTextToShowWhenEmpty("Scale name", juce::Colours::grey);
        customScaleNameEditor_.setTitle("Custom scale name");

        customBlock_.addAndMakeVisible(saveCustomScaleButton_);
        saveCustomScaleButton_.setComponentID("scaleAssistCustomSaveButton");
        saveCustomScaleButton_.setTooltip("Save the custom scale under the name above");
        saveCustomScaleButton_.onClick = [this] { handleSaveCustomScale(); };
    }

    void buildPitchVisibilityControl() {
        scaleContent_.addAndMakeVisible(pitchVisibilityToggle_);
        pitchVisibilityToggle_.setComponentID("scaleAssistPitchVisibilityToggle");
        // Named to match the header chip's tooltip word for word — they are two views of ONE piece of
        // state (the roll's toggleScaleFilter is the single writer), and two names for one switch is
        // how a user ends up believing they are two switches.
        pitchVisibilityToggle_.setButtonText("Show only scale notes");
        pitchVisibilityToggle_.setTooltip("Collapse the out-of-scale rows, and make Up/Down step by scale degree");
        pitchVisibilityToggle_.onClick = [this] {
            pitchVisibilityOn_ = pitchVisibilityToggle_.getToggleState();
            if (onPitchVisibilityChanged)
                onPitchVisibilityChanged(pitchVisibilityOn_);
        };
    }

    void buildRandomGenerationControls() {
        scaleContent_.addAndMakeVisible(rangeLabel_);
        rangeLabel_.setText("Note range", juce::dontSendNotification);
        rangeLabel_.setFont(juce::Font(juce::FontOptions(11.5f)));
        rangeLabel_.setInterceptsMouseClicks(false, false);

        scaleContent_.addAndMakeVisible(rangePicker_);
        rangePicker_.setComponentID("scaleAssistRangePicker");
        rangePicker_.setRange(kDefaultMinPitch, kDefaultMaxPitch);

        scaleContent_.addAndMakeVisible(generateButton_);
        generateButton_.setComponentID("scaleAssistGenerateButton");
        generateButton_.setTooltip("Fill the clip with random notes from the chosen scale and range");
        generateButton_.onClick = [this] {
            if (onGenerate)
                onGenerate(getMinPitchSelection(), getMaxPitchSelection(), isAddToExistingSelected());
        };

        scaleContent_.addAndMakeVisible(addToExistingToggle_);
        addToExistingToggle_.setComponentID("scaleAssistAddToExistingToggle");
        addToExistingToggle_.setButtonText("Add to existing");
        addToExistingToggle_.setTooltip("Off: Generate replaces every note in the clip. On: the generated notes are "
                                        "added on top of what is already there.");
    }

    // The field and the range picker show the key in force; the range follows a change of root (C2-C5 in C reads
    // D2-D5 in D), and keeps its place while there is no key.
    void showSelectedKey() {
        keyField_.setKey(selectedScale_);
        keyField_.setFollowing(following_);
        rangePicker_.setScale(selectedScale_.has_value() ? &*selectedScale_ : nullptr);
        if (selectedScale_.has_value() && selectedScale_->rootPitchClass != rangeRoot_) {
            const auto [lo, hi] = synth::rangeFollowingRoot(rangePicker_.getLow(), rangePicker_.getHigh(), rangeRoot_,
                                                            selectedScale_->rootPitchClass);
            rangePicker_.setRange(lo, hi);
            rangeRoot_ = selectedScale_->rootPitchClass;
        }
    }

    void pickOwnKey(const synth::MusicalScale& key) {
        selectedScale_ = key;
        following_ = false;
        showCustomEditor(false);
        notifyScaleChanged();
    }

    int currentRoot() const noexcept { return selectedScale_.has_value() ? selectedScale_->rootPitchClass : 0; }

    void handleSaveCustomScale() {
        const auto name = customScaleNameEditor_.getText().trim();
        if (name.isEmpty())
            return; // nothing to save under

        std::uint16_t mask = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (customPitchToggles_[(size_t)pc].getToggleState())
                mask = (std::uint16_t)(mask | (1u << pc));
        if (mask == 0)
            return; // a scale with no notes is no key: the picker would not list it

        bool replaced = false;
        for (auto& existing : userScales_) {
            if (existing.name.equalsIgnoreCase(name)) {
                existing.mask = mask;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            userScales_.push_back({name, mask});

        if (propertiesFile_ != nullptr) {
            propertiesFile_->setValue(kUserScalesPropertyKey, synth::serializeUserScales(userScales_));
            propertiesFile_->saveIfNeeded();
        }

        synth::MusicalScale saved;
        saved.rootPitchClass = currentRoot();
        saved.mask = mask;
        saved.name = name;
        pickOwnKey(saved); // a saved scale becomes the clip's own key at the current root
    }

    void showCustomEditor(bool show) {
        customEditorVisible_ = show;
        customFade_.setShown(show); // the block (toggles, name, Save) fades and the rows below slide with it
        resized();
    }

    void notifyScaleChanged() {
        showSelectedKey();
        if (onScaleChanged)
            onScaleChanged(selectedScale_);
        repaint();
    }

    static juce::String pitchClassName(int pitchClass) {
        static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return kNames[juce::jlimit(0, 11, pitchClass)];
    }

    juce::PropertiesFile* propertiesFile_ = nullptr;
    std::vector<synth::UserScale> userScales_;
    std::optional<synth::MusicalScale> selectedScale_;
    std::optional<synth::MusicalScale> songKey_;
    bool following_ = true;
    bool pitchVisibilityOn_ = false;
    int rangeRoot_ = 0; // the root the range picker last followed
    bool customEditorVisible_ = false;
    juce::Component::SafePointer<juce::Component> openPicker_;

    // The scrolled content that holds every control, plus the viewport that shows a vertical
    // scrollbar only when that content is taller than the panel — see resized()/contentNaturalHeight().
    juce::Component scaleContent_;
    juce::Viewport scrollViewport_;
    PaneKeys paneKeys_{*this};

    KeyField keyField_;
    juce::TextButton customScalesButton_{"Custom scales..."};

    // The custom scale editor (the twelve keys, the name field and Save), held in one block so it can fade and be
    // clipped to the height the fade has reached. `customEditorVisible_` is its logical state.
    juce::Component customBlock_;
    synth::ui::FadeVisibility customFade_{&customBlock_};
    std::array<PianoKeyToggle, 12> customPitchToggles_;
    juce::TextEditor customScaleNameEditor_;
    juce::TextButton saveCustomScaleButton_{"Save"};

    juce::ToggleButton pitchVisibilityToggle_;

    juce::Label rangeLabel_;
    NoteRangePicker rangePicker_;
    juce::TextButton generateButton_{"Generate"};
    // Session-only, off by default — see isAddToExistingSelected().
    juce::ToggleButton addToExistingToggle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScaleAssistPanel)
};

} // namespace synth::ui
