#pragma once

#include "UI/Layout/ArrowKeyNavigation.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth {

/**
 * @class LeavingSurveyPanel
 * @brief "Why are you leaving?": reason toggles, an optional comment, and Skip / Continue.
 *
 * Knows nothing about the network: Continue hands the answer to onContinue and Skip calls onSkip,
 * and the owner decides what happens next (the survey never blocks leaving). Escape is not handled
 * here; it travels up to the hosting panel like any other control's.
 */
class LeavingSurveyPanel : public juce::Component {
public:
    enum class Kind { cancel, deleteAccount };

    struct Answer {
        std::vector<juce::String> reasons; // keys, in the order the toggles are listed
        juce::String comment;              // trimmed, at most kMaxCommentChars; empty when none
    };

    struct Reason {
        const char* key;
        const char* label;
    };

    static constexpr int kReasonCount = 8;
    static constexpr int kMaxCommentChars = 2000;
    static constexpr int kPreferredWidth = 360;
    static constexpr int kPreferredHeight = 430;

    static const std::array<Reason, kReasonCount>& reasons();

    // The `kind` string the server expects.
    static juce::String kindKey(Kind kind);

    explicit LeavingSurveyPanel(Kind kind);

    std::function<void()> onSkip;
    std::function<void(const Answer&)> onContinue;

    Kind getKind() const { return kind_; }
    Answer currentAnswer() const;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Testing hooks
    juce::ToggleButton& getReasonToggleForTest(int index) { return *toggles_[(size_t)index]; }
    juce::TextEditor& getCommentEditorForTest() { return comment_; }
    juce::TextButton& getSkipButtonForTest() { return skip_; }
    juce::TextButton& getContinueButtonForTest() { return continue_; }

private:
    Kind kind_;
    juce::Label title_;
    std::array<std::unique_ptr<juce::ToggleButton>, kReasonCount> toggles_;
    juce::Label commentCaption_;
    juce::TextEditor comment_;
    juce::TextButton skip_{"Skip"};
    juce::TextButton continue_{"Continue"};

    // Last, so it is destroyed before the controls it listens on.
    synth::ui::ArrowKeyNavigation arrowKeys_{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LeavingSurveyPanel)
};

} // namespace synth
