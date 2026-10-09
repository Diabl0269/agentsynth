#include "LeavingSurveyPanel.h"
#include "UI/Layout/DialogKeyboard.h"

namespace synth {

const std::array<LeavingSurveyPanel::Reason, LeavingSurveyPanel::kReasonCount>& LeavingSurveyPanel::reasons() {
    // The keys are what the server stores; the order is the order people see.
    static const std::array<Reason, kReasonCount> list{{
        {"too_expensive", "It costs too much"},
        {"unused", "I don't use it enough"},
        {"low_quality", "The AI results weren't good enough"},
        {"missing_features", "It's missing something I need"},
        {"switched_service", "I switched to another tool"},
        {"too_complex", "It's too hard to use"},
        {"bugs", "Bugs or crashes"},
        {"other", "Something else"},
    }};
    return list;
}

juce::String LeavingSurveyPanel::kindKey(Kind kind) { return kind == Kind::cancel ? "cancel" : "delete"; }

LeavingSurveyPanel::LeavingSurveyPanel(Kind kind)
    : kind_(kind) {
    setTitle("Why are you leaving?");
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    addAndMakeVisible(title_);
    title_.setText("Why are you leaving?", juce::dontSendNotification);
    title_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));

    for (size_t i = 0; i < toggles_.size(); ++i) {
        auto toggle = std::make_unique<juce::ToggleButton>(reasons()[i].label);
        toggle->setTitle(reasons()[i].label);
        toggle->setTooltip(juce::String("Tick if this is one of your reasons: ") + reasons()[i].label);
        addAndMakeVisible(*toggle);
        toggles_[i] = std::move(toggle);
    }

    addAndMakeVisible(commentCaption_);
    commentCaption_.setText("Anything else you'd like to tell us? (optional)", juce::dontSendNotification);

    addAndMakeVisible(comment_);
    comment_.setMultiLine(true, true);
    comment_.setReturnKeyStartsNewLine(true);
    comment_.setInputRestrictions(kMaxCommentChars);
    comment_.setTitle("Anything else you'd like to tell us? (optional)");
    comment_.setTooltip("Optional. Up to 2000 characters.");
    synth::ui::removeHiddenTabStops(comment_);
    synth::ui::bubbleEscapeToParents(comment_);

    addAndMakeVisible(skip_);
    skip_.setTitle("Skip");
    skip_.setTooltip("Carry on without answering");
    skip_.onClick = [this] {
        if (onSkip)
            onSkip();
    };

    addAndMakeVisible(continue_);
    continue_.setTitle("Continue");
    continue_.setTooltip("Send your answer and carry on");
    continue_.onClick = [this] {
        if (onContinue)
            onContinue(currentAnswer());
    };

    setSize(kPreferredWidth, kPreferredHeight);
}

LeavingSurveyPanel::Answer LeavingSurveyPanel::currentAnswer() const {
    Answer answer;
    for (size_t i = 0; i < toggles_.size(); ++i)
        if (toggles_[i]->getToggleState())
            answer.reasons.emplace_back(reasons()[i].key);
    answer.comment = comment_.getText().trim().substring(0, kMaxCommentChars);
    return answer;
}

void LeavingSurveyPanel::paint(juce::Graphics& g) { g.fillAll(findColour(juce::ResizableWindow::backgroundColourId)); }

void LeavingSurveyPanel::resized() {
    auto area = getLocalBounds().reduced(14);
    title_.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);
    for (auto& toggle : toggles_)
        toggle->setBounds(area.removeFromTop(26));
    area.removeFromTop(8);
    commentCaption_.setBounds(area.removeFromTop(22));

    auto buttons = area.removeFromBottom(30);
    area.removeFromBottom(8);
    comment_.setBounds(area);

    continue_.setBounds(buttons.removeFromRight(96));
    buttons.removeFromRight(8);
    skip_.setBounds(buttons.removeFromRight(80));
}

} // namespace synth
