#pragma once

#include "DrumIdeas.h"
#include "UI/Layout/CalloutReveal.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

/** What the Ideas panel needs to know about the clip it works on, read fresh each time it generates. */
struct DrumIdeasContext {
    drumideas::Grid grid;
    double secondsPerBeat = 0.5;
    std::vector<synth::MidiNote> notes;
};

/** One small picture of a take: a few pixels per step, a row per drum. A button, so a take is a Tab stop and Space or
 *  a click plays it. */
class DrumMiniGrid : public juce::Button {
public:
    DrumMiniGrid();
    void setNotes(std::vector<synth::MidiNote> notes, const drumideas::Grid& grid);
    bool isEmptyTake() const noexcept { return notes_.empty(); }
    const std::vector<synth::MidiNote>& getNotes() const noexcept { return notes_; }
    void setPlaying(bool playing);
    void paintButton(juce::Graphics& g, bool over, bool down) override;

private:
    std::vector<synth::MidiNote> notes_;
    drumideas::Grid grid_;
    bool playing_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumMiniGrid)
};

/** Plays one pass of a take through the single-note audition path, in time, from a juce::Timer. It stops after the
 *  pass, on the next start, or when destroyed. */
class DrumTakeAudition : private juce::Timer {
public:
    static constexpr int kTickMs = 10;
    DrumTakeAudition() = default;
    ~DrumTakeAudition() override { stop(); }

    std::function<void(int pitch, int velocity)> onHit;
    std::function<void()> onFinished;

    void start(const std::vector<synth::MidiNote>& notes, double lengthBeats, double secondsPerBeat);
    void stop();
    bool isPlaying() const noexcept { return isTimerRunning(); }
    /** Fires every hit due by `elapsedSeconds` (the timer's own tick, callable by a test). */
    void advanceTo(double elapsedSeconds);

private:
    void timerCallback() override;

    std::vector<synth::MidiNote> notes_;
    size_t next_ = 0;
    double spb_ = 0.5;
    double totalSeconds_ = 0.0;
    double startedMs_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumTakeAudition)
};

/**
 * The Ideas popover of the drum machine: starters by style, a Vary amount with three takes to hear and pick, and a
 * Randomize with a density and "Keep kick and snare", and Fills (a snare roll or tom fill at the end of the last bar,
 * or of every 4th bar). It lives in a juce::CallOutBox grown out of the Ideas button and stays open between results. It
 * holds no clip: the owner applies each result as one undo step. Esc closes it. docs/timeline/drum-machine.md#ideas.
 */
class DrumIdeasPanel : public juce::Component {
public:
    static constexpr int kWidth = 452;
    static constexpr int kHeight = 446;
    static constexpr int kTakes = 3;

    explicit DrumIdeasPanel(std::function<DrumIdeasContext()> context);
    ~DrumIdeasPanel() override;

    /** A style was chosen: fill the clip with its starter. */
    std::function<void(drumideas::Style)> onStarter;
    /** A fill button was pressed; the bool is the "Every 4th bar" toggle. */
    std::function<void(drumideas::Fill, bool everyFourthBar)> onFill;
    /** Randomize was pressed. */
    std::function<void(double density, bool keepKickAndSnare, int seed)> onRandomize;
    /** A take was picked: `replace` swaps the clip's notes, otherwise the owner adds a clip after it. */
    std::function<void(const std::vector<synth::MidiNote>& notes, bool replace, int takeIndex)> onPick;
    /** A take's hits play through this, one note on then off. */
    std::function<void(int pitch, int velocity)> onAudition;
    /** The panel closed (Escape, click-away, or destroyed): the owner returns the focus to the Ideas button. */
    std::function<void()> onClosed;

    /** Seeds the generators so results repeat (each generation takes the next number); real use seeds from time. */
    void setSeedForTest(int seed) noexcept {
        seed_ = seed;
        seeded_ = true;
    }

    juce::TextButton& getStarterButton(drumideas::Style style) { return *starters_[(size_t)style]; }
    juce::TextButton& getSnareRollButton() noexcept { return snareRoll_; }
    juce::TextButton& getTomFillButton() noexcept { return tomFill_; }
    juce::ToggleButton& getEveryFourthToggle() noexcept { return everyFourth_; }
    juce::Slider& getAmountSlider() noexcept { return amount_; }
    juce::TextButton& getMakeTakesButton() noexcept { return makeTakes_; }
    DrumMiniGrid& getTakeGrid(int index) noexcept { return *takeGrids_[(size_t)index]; }
    juce::TextButton& getPickButton(int index) noexcept { return *picks_[(size_t)index]; }
    juce::ToggleButton& getReplaceToggle() noexcept { return replace_; }
    juce::Slider& getDensitySlider() noexcept { return density_; }
    juce::ToggleButton& getKeepToggle() noexcept { return keep_; }
    juce::TextButton& getRandomizeButton() noexcept { return randomize_; }
    DrumTakeAudition& getAudition() noexcept { return audition_; }

    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void parentHierarchyChanged() override;
    void lookAndFeelChanged() override;

private:
    int nextSeed();
    void makeTakes();
    void playTake(int index);
    void applyColours();
    void notifyClosed();

    std::function<DrumIdeasContext()> context_;
    std::array<std::unique_ptr<juce::TextButton>, drumideas::kStyleCount> starters_;
    std::array<std::unique_ptr<DrumMiniGrid>, kTakes> takeGrids_;
    std::array<std::unique_ptr<juce::TextButton>, kTakes> picks_;
    juce::Label title_{"title", "Ideas"};
    juce::Label startersLabel_{"startersLabel", "Starters by style"};
    juce::Label fillsLabel_{"fillsLabel", "Fills"};
    juce::Label varyLabel_{"varyLabel", "Vary"};
    juce::Label subtleLabel_{"subtle", "Subtle"};
    juce::Label wildLabel_{"wild", "Wild"};
    juce::Label randomizeLabel_{"randomizeLabel", "Randomize"};
    juce::Label sparseLabel_{"sparse", "Sparse"};
    juce::Label busyLabel_{"busy", "Busy"};
    juce::Slider amount_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    juce::TextButton snareRoll_{"Snare roll"};
    juce::TextButton tomFill_{"Tom fill"};
    juce::ToggleButton everyFourth_{"Every 4th bar"};
    juce::TextButton makeTakes_{"Make 3 takes"};
    juce::ToggleButton replace_{"Replace this clip"};
    juce::Slider density_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    juce::ToggleButton keep_{"Keep kick and snare"};
    juce::TextButton randomize_{"Randomize"};
    std::array<std::vector<synth::MidiNote>, kTakes> takes_;
    DrumTakeAudition audition_;
    CalloutReveal reveal_{*this};
    int seed_ = 0;
    bool seeded_ = false;
    bool closedNotified_ = false;
    int playingTake_ = -1;
    std::array<int, 3> dividerY_{0, 0, 0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumIdeasPanel)
};

} // namespace synth::ui
