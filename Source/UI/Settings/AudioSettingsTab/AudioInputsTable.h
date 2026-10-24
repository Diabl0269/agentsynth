#pragma once

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/IconButton.h"
#include "UI/Settings/AudioSettingsTab/AudioInputLevelCell.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsSection.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsSource.h"
#include "UI/Settings/AudioSettingsTab/LeavingRows.h"
#include <functional>
#include <memory>
#include <vector>

// The Inputs section: one row per mono jack or per paired stereo input of the open interface (columns Jacks, Pair,
// Name, Kind, Level, Use), for every input the engine can take. Jacks come in neighbouring pairs; the chain button
// spanning a pair's rows joins them into one stereo input or splits them again. Pairs and names live in the
// interface's entry of the AudioInputLayoutStore, so they follow the interface, not the project. Level reads the
// engine's input peaks on a 15 Hz timer that runs only while the table is on screen.
class AudioInputsTable
    : public AudioSettingsSection
    , private juce::Timer {
public:
    explicit AudioInputsTable(AudioSettingsSource& source);
    ~AudioInputsTable() override;

    int preferredHeight(int width) const override;
    juce::String summary() const override;
    void refresh() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

    /** Called with every name field the table makes, so the window can offer it the tab-switch keys. */
    std::function<void(juce::TextEditor&)> onEditorCreated;

    int getNumRowsForTest() const { return (int)rows_.size(); }
    int getNumPairButtonsForTest() const { return (int)pairButtons_.size(); }
    synth::ui::IconButton* getPairButtonForTest(int pair);
    juce::TextEditor* getNameEditorForTest(int row);
    juce::ToggleButton* getUseTickForTest(int row);
    AudioInputLevelCell* getLevelCellForTest(int row);
    juce::String getJacksTextForTest(int row) const;
    juce::String getKindTextForTest(int row) const;
    juce::Rectangle<int> getRowBoundsForTest(int row) const;
    void pollMetersForTest() { pollMeters(); }
    bool isTimerRunningForTest() const { return isTimerRunning(); }
    juce::String getCaptionTextForTest() const { return caption_.getText(); }
    void commitNameForTest(int row) { commitName(row); }

private:
    struct Row : juce::Component {
        Row(const juce::String& jacksText, int first, int count, bool paired);
        void resized() override;
        void paint(juce::Graphics& g) override;
        int firstJack, numJacks;
        bool paired;
        juce::Label jacks, kind;
        juce::TextEditor name;
        std::unique_ptr<AudioInputLevelCell> level;
        juce::ToggleButton use;
    };

    struct Spec {
        int first = 0, count = 1;
        bool operator==(const Spec& o) const { return first == o.first && count == o.count; }
    };

    void timerCallback() override { pollMeters(); }
    void pollMeters();
    void rebuild(const std::vector<Spec>& specs, int numJacks);
    void updateInPlace(const AudioInterfaceInfo& info);
    void commitName(int row);
    void updateTimer();
    int rowTop(int row) const;
    static juce::String jacksText(int first, int count);

    AudioSettingsSource& source_;
    AudioInterfaceInfo info_;
    std::vector<Spec> specs_;
    std::vector<std::unique_ptr<Row>> rows_;
    std::vector<std::unique_ptr<synth::ui::IconButton>> pairButtons_; // by pair index; null when the pair is not whole
    LeavingRows leaving_;
    std::unique_ptr<synth::ui::FadeVisibility> arriveFade_;
    bool built_ = false;
    juce::Label none_;
    juce::Label caption_;
};
