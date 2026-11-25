#pragma once

#include "SongCardBase.h"
#include "SongCardThumbs.h"
#include "SongSession.h"
#include "StepBlocks.h"
#include "StepIcons.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FoldSectionHeader.h"
#include "UI/Layout/TextLinkButton.h"
#include <memory>

namespace synth {

// The one live card of a song build (see docs/ai/song-steps.md#the-song-card). From the top: the song's name, its tempo
// and key, the state ("Building your song"), the step running now with its moving icon, a progress bar, then two
// folds: "Steps" (a sunken box with one block per step, the running one highlighted) and, once built, "Details" (what
// was built, track by track). A running build ends with Cancel; a built song ends with the thumbs, a small left-aligned
// row under the finished content, like the chat apps do. A view of a SongSession; the chat calls refresh() as events
// arrive.
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
    juce::String titleText() const { return titleLabel.getText(); }
    juce::String metaText() const { return metaLabel.getText(); }
    juce::String nowText() const;
    juce::String progressText() const;
    juce::String headerText() const;
    int rowCount() const { return box.blockCount(); }
    juce::Component* row(int i) const { return box.block(i); }
    songsteps::StepStatus rowStatus(int i) const { return box.block(i)->status(); }
    StepBlockBox& stepBox() { return box; }
    bool listOpenForTesting() const { return openMs > 0.0f; }
    void toggleListForTesting() { stepsFold.onClick(); }
    void pressCancelForTesting() { cancelButton.onClick(); }
    synth::ui::StepIconView& nowIcon() { return nowIconView; }
    bool detailsButtonShownForTesting() const { return detailsFold.isVisible(); }
    bool detailsOpenForTesting() const { return session.detailsOpen; }
    void toggleDetailsForTesting() { detailsFold.onClick(); }
    juce::String detailsShownTextForTesting() const { return detailsDisplay.getText(); }
    synth::ui::FoldSectionHeader& stepsFoldForTesting() { return stepsFold; }
    synth::ui::FoldSectionHeader& detailsFoldForTesting() { return detailsFold; }
    ThumbButton& thumbForTesting(bool up) { return up ? thumbsUpButton : thumbsDownButton; }
    juce::Component& commentFieldForTesting() { return commentField; }
    juce::Component& cancelButtonForTesting() { return cancelButton; }
    void pressThumbForTesting(bool up) { (up ? thumbsUpButton : thumbsDownButton).onClick(); }

private:
    int layoutFor(int width, bool apply) override;
    void paint(juce::Graphics& g) override;
    void setListOpen(bool open, bool animate);
    float heightReveal() const;
    juce::String stateLine() const;
    void syncBlocks();
    void setDetailsOpen(bool open, bool animate);
    void setRating(int rating);
    void notifyRate();
    void styleFeedbackControls();
    void parentHierarchyChanged() override;
    int feedbackHeight() const;

    SongSession& session;
    Actions actions;

    juce::Label titleLabel, metaLabel, headerLabel, clockLabel, nowLabel, subLabel, outcomeLabel;
    synth::ui::StepIconView nowIconView;
    StepBlockBox box;
    synth::ui::FoldSectionHeader stepsFold{"Steps"};
    synth::ui::TextLinkButton cancelButton{"Cancel", juce::Justification::centredLeft};
    float progressShown = 0.0f;
    juce::Rectangle<int> barBounds;

    // The Steps fold's openness in milliseconds along its own timeline: the box grows and fades in over it, and plays
    // it backwards to close.
    float openMs = 0.0f;
    float openTargetMs = 0.0f;
    synth::ui::AnimationDriver listAnim;
    synth::ui::AnimationDriver barAnim;
    synth::ui::FadeVisibility cancelFade_{&cancelButton};

    // Once built: the Details fold, and under it the thumbs row, which fades in when the build ends; the comment row
    // and the details panel open with a fade and a height tween, like the edit-plan card's.
    synth::ui::FoldSectionHeader detailsFold{"Details"};
    ThumbButton thumbsUpButton{true}, thumbsDownButton{false};
    juce::TextButton commentSaveButton;
    juce::TextEditor commentField, detailsDisplay;
    bool detailsFilled = false;
    synth::ui::FadeVisibility detailsFoldFade_{&detailsFold};
    synth::ui::FadeVisibility thumbsFade_{&thumbsUpButton, &thumbsDownButton};
    synth::ui::FadeVisibility commentFade_{&commentField, &commentSaveButton};
    synth::ui::FadeVisibility detailsFade_{&detailsDisplay};
};

} // namespace synth
