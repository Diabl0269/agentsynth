#pragma once

// Scale-assist panel for the piano roll: root/scale pickers, a custom-scale editor, the pitch-
// visibility toggle, the quantize action and a random-generation block. Deliberately dumb — it
// holds no reference to the roll, no TimelineDoc and no undo manager. Every action the roll needs
// to react to travels OUT through a std::function callback, and every piece of state the roll
// needs to push BACK in (switching clips, restoring a persisted scale) travels IN through
// setSelection() — the same "panel owns presentation, owner owns the doc" split
// PianoRollComponent itself follows for TimelineViewState.
//
// User scales persist through synth::serializeUserScales/parseUserScales under one
// juce::PropertiesFile key ("pianoRollUserScales"), the same one-key idiom NoteColour.h's own
// persistence follows. setPropertiesFile(nullptr) is a legal, PERMANENT state: Save still works,
// it is simply session-only (nothing survives past this juce::PropertiesFile instance) — the same
// null-degrades-gracefully contract every other timeline sub-component's setter follows.

#include "../Timeline/MusicalScale.h"
#include <array>
#include <cstdint>
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace synth::ui {

class ScaleAssistPanel : public juce::Component {
public:
    ScaleAssistPanel() {
        setComponentID("scaleAssistPanel");
        buildRootAndScaleControls();
        buildCustomScaleEditor();
        buildPitchVisibilityAndQuantizeControls();
        buildRandomGenerationControls();
        rebuildScaleCombo();
        showCustomEditor(false);
    }
    ~ScaleAssistPanel() override = default;

    void paint(juce::Graphics& g) override {
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        // A hairline against whatever sits to our right (the roll's keys column) — enough to read
        // as a separate panel without a themed border token of its own.
        g.setColour(findColour(juce::Label::textColourId).withAlpha(0.15f));
        g.drawVerticalLine(getWidth() - 1, 0.0f, (float)getHeight());
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(6);

        auto rootRow = bounds.removeFromTop(22);
        rootLabel_.setBounds(rootRow.removeFromLeft(34));
        rootCombo_.setBounds(rootRow);
        bounds.removeFromTop(4);

        scaleCombo_.setBounds(bounds.removeFromTop(22));
        bounds.removeFromTop(6);

        if (customEditorVisible_) {
            auto row1 = bounds.removeFromTop(20);
            auto row2 = bounds.removeFromTop(20);
            const int toggleWidth = juce::jmax(1, row1.getWidth() / 6);
            for (int pc = 0; pc < 6; ++pc)
                customPitchToggles_[(size_t)pc].setBounds(row1.removeFromLeft(toggleWidth).reduced(1, 0));
            for (int pc = 6; pc < 12; ++pc)
                customPitchToggles_[(size_t)pc].setBounds(row2.removeFromLeft(toggleWidth).reduced(1, 0));
            bounds.removeFromTop(4);

            auto nameRow = bounds.removeFromTop(22);
            saveCustomScaleButton_.setBounds(nameRow.removeFromRight(56));
            nameRow.removeFromRight(4);
            customScaleNameEditor_.setBounds(nameRow);
            bounds.removeFromTop(6);
        }

        pitchVisibilityToggle_.setBounds(bounds.removeFromTop(22));
        bounds.removeFromTop(6);
        quantizeButton_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(10);

        auto minRow = bounds.removeFromTop(22);
        minNoteLabel_.setBounds(minRow.removeFromLeft(28));
        minNoteCombo_.setBounds(minRow);
        bounds.removeFromTop(4);

        auto maxRow = bounds.removeFromTop(22);
        maxNoteLabel_.setBounds(maxRow.removeFromLeft(28));
        maxNoteCombo_.setBounds(maxRow);
        bounds.removeFromTop(6);

        generateButton_.setBounds(bounds.removeFromTop(24));
    }

    // ---- Persistence (user scales only — panel visibility is the ROLL's own key) ----

    // Null-safe: with no PropertiesFile, Save still appends/overwrites userScales_ for the
    // session, it just never reaches disk. Re-parses and REPLACES the in-memory list from `props`
    // when non-null, since a freshly-wired properties file is the canonical on-disk state.
    void setPropertiesFile(juce::PropertiesFile* props) {
        propertiesFile_ = props;
        if (propertiesFile_ != nullptr)
            userScales_ = synth::parseUserScales(propertiesFile_->getValue(kUserScalesPropertyKey, {}));
        rebuildScaleCombo();
    }

    // ---- State the owner pushes IN (never fires a callback — this is a REFLECTION, not an
    // edit the user made) ----
    void setSelection(std::optional<synth::MusicalScale> scale, bool pitchVisibilityOn) {
        selectedScale_ = scale;
        pitchVisibilityOn_ = pitchVisibilityOn;
        if (selectedScale_.has_value()) {
            rootPitchClass_ = selectedScale_->rootPitchClass;
            rootCombo_.setSelectedId(rootPitchClass_ + 1, juce::dontSendNotification);
        }
        selectScaleComboForCurrentSelection();
        pitchVisibilityToggle_.setToggleState(pitchVisibilityOn_, juce::dontSendNotification);
        quantizeButton_.setEnabled(selectedScale_.has_value());
        showCustomEditor(false);
        repaint();
    }

    std::optional<synth::MusicalScale> getSelectedScale() const noexcept { return selectedScale_; }
    bool isPitchVisibilityOn() const noexcept { return pitchVisibilityOn_; }

    int getMinPitchSelection() const noexcept { return juce::jlimit(0, 127, minNoteCombo_.getSelectedId() - 1); }
    int getMaxPitchSelection() const noexcept { return juce::jlimit(0, 127, maxNoteCombo_.getSelectedId() - 1); }

    // ---- Callbacks the owner wires (see the class comment) ----
    std::function<void(std::optional<synth::MusicalScale>)> onScaleChanged;
    std::function<void(bool)> onPitchVisibilityChanged;
    std::function<void()> onQuantizePitches;
    std::function<void(int minPitch, int maxPitch)> onGenerate;

    // ---- Test accessors (every interactive child also carries its own componentID) ----
    juce::ComboBox& getRootCombo() noexcept { return rootCombo_; }
    juce::ComboBox& getScaleCombo() noexcept { return scaleCombo_; }
    juce::ToggleButton& getPitchVisibilityToggle() noexcept { return pitchVisibilityToggle_; }
    juce::TextButton& getQuantizeButton() noexcept { return quantizeButton_; }
    juce::TextButton& getGenerateButton() noexcept { return generateButton_; }
    juce::ComboBox& getMinNoteCombo() noexcept { return minNoteCombo_; }
    juce::ComboBox& getMaxNoteCombo() noexcept { return maxNoteCombo_; }
    juce::TextEditor& getCustomScaleNameEditor() noexcept { return customScaleNameEditor_; }
    juce::TextButton& getSaveCustomScaleButton() noexcept { return saveCustomScaleButton_; }
    juce::ToggleButton& getCustomPitchToggle(int pitchClass) noexcept {
        return customPitchToggles_[(size_t)juce::jlimit(0, 11, pitchClass)];
    }
    bool isCustomEditorVisibleForTest() const noexcept { return customEditorVisible_; }
    const std::vector<synth::UserScale>& getUserScalesForTest() const noexcept { return userScales_; }

private:
    static constexpr int kNoScaleId = 1;
    // Far past any realistic preset+user-scale id range (14 presets + however many a session
    // accumulates), so it can never collide with a real scale's combo id.
    static constexpr int kCustomRowId = 9000;
    static constexpr int kDefaultMinPitch = 36; // C2
    static constexpr int kDefaultMaxPitch = 72; // C5
    static constexpr const char* kUserScalesPropertyKey = "pianoRollUserScales";

    void buildRootAndScaleControls() {
        addAndMakeVisible(rootLabel_);
        rootLabel_.setText("Root", juce::dontSendNotification);
        rootLabel_.setFont(juce::Font(juce::FontOptions(11.5f)));

        addAndMakeVisible(rootCombo_);
        rootCombo_.setComponentID("scaleAssistRootCombo");
        for (int pc = 0; pc < 12; ++pc)
            rootCombo_.addItem(pitchClassName(pc), pc + 1);
        rootCombo_.setSelectedId(1, juce::dontSendNotification);
        rootCombo_.onChange = [this] {
            rootPitchClass_ = rootCombo_.getSelectedId() - 1;
            // Only a REAL scale has a root worth re-firing over; "No scale" ignores it.
            if (selectedScale_.has_value()) {
                selectedScale_->rootPitchClass = rootPitchClass_;
                notifyScaleChanged();
            }
        };

        addAndMakeVisible(scaleCombo_);
        scaleCombo_.setComponentID("scaleAssistScaleCombo");
        scaleCombo_.onChange = [this] { handleScaleComboChanged(); };
    }

    void buildCustomScaleEditor() {
        for (int pc = 0; pc < 12; ++pc) {
            auto& toggle = customPitchToggles_[(size_t)pc];
            addChildComponent(toggle);
            toggle.setComponentID("scaleAssistCustomToggle" + juce::String(pc));
            toggle.setButtonText(pitchClassName(pc));
        }

        addChildComponent(customScaleNameEditor_);
        customScaleNameEditor_.setComponentID("scaleAssistCustomNameEditor");
        customScaleNameEditor_.setTextToShowWhenEmpty("Scale name", juce::Colours::grey);

        addChildComponent(saveCustomScaleButton_);
        saveCustomScaleButton_.setComponentID("scaleAssistCustomSaveButton");
        saveCustomScaleButton_.onClick = [this] { handleSaveCustomScale(); };
    }

    void buildPitchVisibilityAndQuantizeControls() {
        addAndMakeVisible(pitchVisibilityToggle_);
        pitchVisibilityToggle_.setComponentID("scaleAssistPitchVisibilityToggle");
        pitchVisibilityToggle_.setButtonText("Show only scale pitches");
        pitchVisibilityToggle_.onClick = [this] {
            pitchVisibilityOn_ = pitchVisibilityToggle_.getToggleState();
            if (onPitchVisibilityChanged)
                onPitchVisibilityChanged(pitchVisibilityOn_);
        };

        addAndMakeVisible(quantizeButton_);
        quantizeButton_.setComponentID("scaleAssistQuantizeButton");
        quantizeButton_.setEnabled(false); // no scale selected yet — nothing to quantize into
        quantizeButton_.onClick = [this] {
            if (onQuantizePitches)
                onQuantizePitches();
        };
    }

    void buildRandomGenerationControls() {
        addAndMakeVisible(minNoteLabel_);
        minNoteLabel_.setText("Min", juce::dontSendNotification);
        minNoteLabel_.setFont(juce::Font(juce::FontOptions(11.5f)));
        addAndMakeVisible(minNoteCombo_);
        minNoteCombo_.setComponentID("scaleAssistMinNoteCombo");

        addAndMakeVisible(maxNoteLabel_);
        maxNoteLabel_.setText("Max", juce::dontSendNotification);
        maxNoteLabel_.setFont(juce::Font(juce::FontOptions(11.5f)));
        addAndMakeVisible(maxNoteCombo_);
        maxNoteCombo_.setComponentID("scaleAssistMaxNoteCombo");

        for (int pitch = 0; pitch <= 127; ++pitch) {
            const auto label = noteNameWithOctave(pitch);
            minNoteCombo_.addItem(label, pitch + 1);
            maxNoteCombo_.addItem(label, pitch + 1);
        }
        minNoteCombo_.setSelectedId(kDefaultMinPitch + 1, juce::dontSendNotification);
        maxNoteCombo_.setSelectedId(kDefaultMaxPitch + 1, juce::dontSendNotification);

        addAndMakeVisible(generateButton_);
        generateButton_.setComponentID("scaleAssistGenerateButton");
        generateButton_.onClick = [this] {
            if (onGenerate)
                onGenerate(getMinPitchSelection(), getMaxPitchSelection());
        };
    }

    // "No scale" first, then every built-in preset (synth::builtInScalePresets order), then the
    // user's saved scales, then the "Edit custom scales..." affordance — see the class comment.
    void rebuildScaleCombo() {
        scaleCombo_.clear(juce::dontSendNotification);
        scaleCombo_.addItem("No scale", kNoScaleId);
        const auto& presets = synth::builtInScalePresets();
        for (size_t i = 0; i < presets.size(); ++i)
            scaleCombo_.addItem(presets[i].name, (int)(2 + i));
        for (size_t i = 0; i < userScales_.size(); ++i)
            scaleCombo_.addItem(userScales_[i].name, (int)(2 + presets.size() + i));
        scaleCombo_.addSeparator();
        scaleCombo_.addItem("Edit custom scales...", kCustomRowId);
        selectScaleComboForCurrentSelection();
    }

    void selectScaleComboForCurrentSelection() {
        scaleCombo_.setSelectedId(findComboIdForScale(selectedScale_), juce::dontSendNotification);
    }

    int findComboIdForScale(const std::optional<synth::MusicalScale>& scale) const {
        if (!scale.has_value())
            return kNoScaleId;
        const auto& presets = synth::builtInScalePresets();
        for (size_t i = 0; i < presets.size(); ++i)
            if (scale->name == presets[i].name)
                return (int)(2 + i);
        for (size_t i = 0; i < userScales_.size(); ++i)
            if (scale->name == userScales_[i].name)
                return (int)(2 + presets.size() + i);
        return kNoScaleId; // a scale this panel no longer knows (e.g. a deleted user scale)
    }

    std::optional<synth::MusicalScale> scaleFromComboId(int id) const {
        if (id <= 0 || id == kNoScaleId)
            return std::nullopt;
        const auto& presets = synth::builtInScalePresets();
        if (id >= 2 && id < (int)(2 + presets.size()))
            return synth::makeScale(rootPitchClass_, id - 2);
        const int userIndex = id - 2 - (int)presets.size();
        if (userIndex >= 0 && userIndex < (int)userScales_.size()) {
            synth::MusicalScale s;
            s.rootPitchClass = rootPitchClass_;
            s.mask = userScales_[(size_t)userIndex].mask;
            s.name = userScales_[(size_t)userIndex].name;
            return s;
        }
        return std::nullopt; // the custom row (or an unknown id) never reaches here — see caller
    }

    void handleScaleComboChanged() {
        const int id = scaleCombo_.getSelectedId();
        if (id == kCustomRowId) {
            // Not itself a scale choice: reveal the editor and put the combo BACK to whatever was
            // actually selected, so the dropdown never shows "Edit custom scales..." as if it were
            // a scale.
            showCustomEditor(true);
            selectScaleComboForCurrentSelection();
            return;
        }
        showCustomEditor(false);
        selectedScale_ = scaleFromComboId(id);
        notifyScaleChanged();
    }

    void handleSaveCustomScale() {
        const auto name = customScaleNameEditor_.getText().trim();
        if (name.isEmpty())
            return; // nothing to save under

        std::uint16_t mask = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (customPitchToggles_[(size_t)pc].getToggleState())
                mask = (std::uint16_t)(mask | (1u << pc));

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
        saved.rootPitchClass = rootPitchClass_;
        saved.mask = mask;
        saved.name = name;
        selectedScale_ = saved;

        rebuildScaleCombo(); // includes selectScaleComboForCurrentSelection()
        showCustomEditor(false);
        notifyScaleChanged();
    }

    void showCustomEditor(bool show) {
        customEditorVisible_ = show;
        for (auto& toggle : customPitchToggles_)
            toggle.setVisible(show);
        customScaleNameEditor_.setVisible(show);
        saveCustomScaleButton_.setVisible(show);
        resized();
    }

    void notifyScaleChanged() {
        quantizeButton_.setEnabled(selectedScale_.has_value());
        if (onScaleChanged)
            onScaleChanged(selectedScale_);
        repaint();
    }

    static juce::String pitchClassName(int pitchClass) {
        static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return kNames[juce::jlimit(0, 11, pitchClass)];
    }

    static juce::String noteNameWithOctave(int pitch) {
        // Octave numbering matches PianoRollComponent::keyLabelFor: pitch / 12 - 1.
        const int pitchClass = ((pitch % 12) + 12) % 12;
        const int octave = pitch / 12 - 1;
        return pitchClassName(pitchClass) + juce::String(octave);
    }

    juce::PropertiesFile* propertiesFile_ = nullptr;
    std::vector<synth::UserScale> userScales_;
    std::optional<synth::MusicalScale> selectedScale_;
    bool pitchVisibilityOn_ = false;
    int rootPitchClass_ = 0;
    bool customEditorVisible_ = false;

    juce::Label rootLabel_;
    juce::ComboBox rootCombo_;
    juce::ComboBox scaleCombo_;

    std::array<juce::ToggleButton, 12> customPitchToggles_;
    juce::TextEditor customScaleNameEditor_;
    juce::TextButton saveCustomScaleButton_{"Save"};

    juce::ToggleButton pitchVisibilityToggle_;
    juce::TextButton quantizeButton_{"Quantize pitches"};

    juce::Label minNoteLabel_;
    juce::Label maxNoteLabel_;
    juce::ComboBox minNoteCombo_;
    juce::ComboBox maxNoteCombo_;
    juce::TextButton generateButton_{"Generate"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScaleAssistPanel)
};

} // namespace synth::ui
