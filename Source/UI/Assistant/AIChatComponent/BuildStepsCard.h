#pragma once

#include "SongCardBase.h"
#include "SongSession.h"
#include "StepIcons.h"
#include "UI/Layout/ChevronTurn.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/TextLinkButton.h"
#include <memory>
#include <vector>

namespace synth {

// The one live card of a song build (see docs/ai/song-steps.md): the step running now with its moving icon, a progress
// bar, "step N of M", and "Show all steps", a list that grows open with its rows sliding in one after another. When the
// build ends it becomes the result (a one-line summary that still opens the list) or says what stopped it. A view of a
// SongSession; the chat calls refresh() as events arrive.
class BuildStepsCard : public SongCardBase {
public:
    struct Actions {
        std::function<void()> onCancel;
        std::function<void(bool open)> onListToggled;
    };

    BuildStepsCard(SongSession& session, Actions actions);
    ~BuildStepsCard() override;

    void refresh();

    // Seconds shown in the header; the chat ticks it while a build runs.
    void setElapsedMs(juce::uint32 ms);

    // Test hooks.
    juce::String nowText() const;
    juce::String progressText() const;
    juce::String headerText() const;
    int rowCount() const { return (int)rows.size(); }
    juce::Component* row(int i) const;
    bool listOpenForTesting() const { return openMs > 0.0f; }
    void toggleListForTesting() { toggleButton.onClick(); }
    void pressCancelForTesting() { cancelButton.onClick(); }
    synth::ui::StepIconState rowIconState(int i) const;
    synth::ui::StepIconView& nowIcon() { return nowIconView; }

private:
    class Row;

    int layoutFor(int width, bool apply) override;
    void paint(juce::Graphics& g) override;
    void setListOpen(bool open, bool animate);
    float rowReveal(int index) const;
    float heightReveal() const;
    juce::String stateLine() const;
    void syncRows();

    SongSession& session;
    Actions actions;

    juce::Label headerLabel, clockLabel, nowLabel, subLabel, summaryLabel, outcomeLabel;
    synth::ui::StepIconView nowIconView;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextButton cancelButton{"Cancel"};
    synth::ui::TextLinkButton toggleButton{"Show all steps", juce::Justification::centredRight};
    synth::ui::ChevronTurn chevron{*this, false};
    float progressShown = 0.0f;
    juce::Rectangle<int> barBounds, chevronBounds;

    // The list's openness in milliseconds along its own timeline: height opens over the first part, each row then
    // fades in a little after the one before it. Open and close play the same timeline forwards and backwards.
    float openMs = 0.0f;
    float openTargetMs = 0.0f;
    synth::ui::AnimationDriver listAnim;
    synth::ui::AnimationDriver barAnim;
    synth::ui::FadeVisibility cancelFade_{&cancelButton};
};

} // namespace synth
