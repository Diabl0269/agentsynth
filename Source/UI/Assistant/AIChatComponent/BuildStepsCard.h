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
// SongSession; the chat calls refresh() as events arrive. Once the song is built it also carries "Show details" (what
// was built, track by track, from SongSession::detailsText) and the thumbs feedback, so a song message needs no second
// card.
class BuildStepsCard : public SongCardBase {
public:
    struct Actions {
        std::function<void()> onCancel;
        std::function<void(bool open)> onListToggled;
        // A thumbs pick (1 up, 2 down, 0 none) or a sent comment on the built song.
        std::function<void(int rating, const juce::String& comment)> onRate;
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
    bool detailsButtonShownForTesting() const { return detailsButton.isVisible(); }
    bool detailsOpenForTesting() const { return session.detailsOpen; }
    void toggleDetailsForTesting() { detailsButton.onClick(); }
    juce::String detailsShownTextForTesting() const { return detailsDisplay.getText(); }
    void pressThumbForTesting(bool up) { (up ? thumbsUpButton : thumbsDownButton).onClick(); }

private:
    class Row;

    int layoutFor(int width, bool apply) override;
    void paint(juce::Graphics& g) override;
    void setListOpen(bool open, bool animate);
    float rowReveal(int index) const;
    float heightReveal() const;
    juce::String stateLine() const;
    void syncRows();
    void setDetailsOpen(bool open, bool animate);
    void setRating(int rating);
    void notifyRate();
    void styleFeedbackControls();
    void parentHierarchyChanged() override;
    int feedbackHeight() const;

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

    // The built song's details link and thumbs sit in one row that fades in when the build ends; the comment row and
    // the details panel open under it with a fade and a height tween, like the edit-plan card's.
    synth::ui::TextLinkButton detailsButton{"Show details"};
    juce::TextButton thumbsUpButton, thumbsDownButton, commentSaveButton;
    juce::TextEditor commentField, detailsDisplay;
    bool detailsFilled = false;
    synth::ui::FadeVisibility feedbackRowFade_{&detailsButton, &thumbsUpButton, &thumbsDownButton};
    synth::ui::FadeVisibility commentFade_{&commentField, &commentSaveButton};
    synth::ui::FadeVisibility detailsFade_{&detailsDisplay};
};

} // namespace synth
