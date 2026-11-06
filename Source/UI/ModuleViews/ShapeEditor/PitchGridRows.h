#pragma once

// PitchGridRows.h -- the Filter shape window's grid control in two rows: the kind (Off, Octaves, Notes, Harmonics)
// with its Snap pill and the Level step with its own, then under them what the kind needs: Steps for Octaves, the
// shared Key field for Notes, the Root box for Harmonics. Every choice is remembered in the user's settings.
#include "PitchGrid.h"
#include "ShapeListAdapter.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FollowKeyField.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/** Message thread only. The rows paint nothing of their own: each row's controls are children, and the right end of
 *  the second row is left free for the owner's toggles (`secondRowWidth`). */
class PitchGridRows : public juce::Component {
public:
    static constexpr int kRowHeight = 26;
    static constexpr int kRowGap = 6;
    static constexpr int kHeight = 2 * kRowHeight + kRowGap;
    /** How much of the second row the rows use, from the left. */
    static constexpr int kSecondRowWidth = 330;

    /** `adapter` must outlive the rows. Restores the remembered choice (or migrates the old one-row choice). */
    explicit PitchGridRows(ShapeListAdapter& adapter);
    ~PitchGridRows() override;

    /** Fired whenever the grid the curve should show changed (a pick, the song key, a new played note). */
    std::function<void()> onGridChanged;

    /** The curve grid for the current choice; nullopt when it is free. */
    std::optional<CurveEditorComponent::CurveGrid> grid() const;
    pitchgrid::Choice getChoice() const { return choice_; }
    /** Shows `choice` in the rows, as a click would, and remembers it. */
    void setChoice(const pitchgrid::Choice& choice);
    /** The Notes key in force: the song key while following, else the own key (nullopt: every note). */
    std::optional<MusicalScale> activeKey() const;
    /** The Harmonics root in hertz, resolved from the Root box (where a glide is heading). */
    double rootHz() const;
    /** The root the lines are drawn at: rootHz(), or where the played-note glide has got to. */
    double shownRootHz() const { return glideHz_.value_or(rootHz()); }
    /** The played-note line eases to a new note over this long (under Reduce Motion it jumps). */
    static constexpr double kGlideMs = 160.0;
    AnimationDriver& getGlide() noexcept { return glide_; }

    /** Root box entries besides the notes. */
    static constexpr int kRootPlaying = 1;
    static constexpr int kRootSongKey = 2;
    /** The Root box id of MIDI note `midi`. */
    static int rootIdOf(int midi) { return kRootNoteBase + midi; }
    void setRootId(int id);

    /** Re-reads the song key and the played note (the window's 10 Hz tick); fires onGridChanged when the lines move. */
    void poll();

    CardSegmentedSwitch& getKindSwitch() noexcept { return kind_; }
    CardSegmentedSwitch& getStepsSwitch() noexcept { return steps_; }
    CardSegmentedSwitch& getLevelSwitch() noexcept { return level_; }
    juce::ToggleButton& getSnapAcross() noexcept { return snapAcross_; }
    juce::ToggleButton& getSnapLevel() noexcept { return snapLevel_; }
    FollowKeyField& getKeyField() noexcept { return key_; }
    juce::ComboBox& getRootBox() noexcept { return root_; }

    void resized() override;
    void lookAndFeelChanged() override;

private:
    static constexpr int kRootNoteBase = 100;
    void load();
    void save() const;
    void showChoice();
    void changed(bool remember);
    void refreshKeyField();
    void updateRootTooltip();

    ShapeListAdapter& adapter_;
    pitchgrid::Choice choice_;
    bool followSongKey_ = true;
    std::optional<MusicalScale> ownKey_;
    std::optional<MusicalScale> songKey_;
    float playedHz_ = 0.0f;
    std::optional<double> glideHz_; ///< set while the played-note line is gliding
    juce::VBlankAnimatorUpdater glideUpdater_{this};
    AnimationDriver glide_;
    void glideTo(double fromHz, double toHz);

    juce::Label gridLabel_, levelLabel_, stepsLabel_, keyLabel_, rootLabel_;
    CardSegmentedSwitch kind_;
    juce::ToggleButton snapAcross_{"Snap"};
    CardSegmentedSwitch level_;
    juce::ToggleButton snapLevel_{"Snap"};
    CardSegmentedSwitch steps_;
    FollowKeyField key_;
    juce::ComboBox root_;
    std::unique_ptr<FadeVisibility> stepsFade_, keyFade_, rootFade_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchGridRows)
};

} // namespace synth::ui
