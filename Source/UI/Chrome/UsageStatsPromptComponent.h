#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The Welcome screen's one-time question: "Share anonymous usage statistics?". An inset panel with a heading, a
// plain-words body, Share and No thanks as two equal buttons, and a "What we collect" link. It has no close
// control: the only ways out are the two answers. Pure UI: the answer goes out through onChoice and the owner
// (WelcomeScreenComponent, then MainComponent) decides what is written. The content is always laid out at
// kHeight from the panel's top edge, so the owner can shrink the panel's bounds to wipe it away without the text
// re-wrapping. docs/layout/chrome.md, docs/development/usage-statistics.md.
class UsageStatsPromptComponent : public juce::Component {
public:
    /** The panel's full height; the owner gives it exactly this while it is fully open. */
    static constexpr int kHeight = 142;

    UsageStatsPromptComponent();

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Replaces the two buttons and the link with a one-line thanks (Share's confirmation), faded by `alpha`. */
    void showConfirmation(bool show);
    void setConfirmationAlpha(float alpha) { confirmationLabel.setAlpha(alpha); }

    std::function<void(bool share)> onChoice;
    std::function<void()> onLearnMoreRequested;

    juce::Button& getShareButtonForTest() { return shareButton; }
    juce::Button& getNoThanksButtonForTest() { return noThanksButton; }
    juce::Button& getLearnMoreButtonForTest() { return learnMoreButton; }
    juce::Label& getConfirmationLabelForTest() { return confirmationLabel; }

private:
    /** A link-style button: underlined accent text, the shared focus ring, a click callback and no URL of its own
     *  (the owner opens the page through the app's URL seam). */
    class LinkButton : public juce::HyperlinkButton {
    public:
        using juce::HyperlinkButton::HyperlinkButton;
        void paint(juce::Graphics&) override;
        void lookAndFeelChanged() override;
        void parentHierarchyChanged() override { lookAndFeelChanged(); }
    };

    juce::Label headingLabel;
    juce::Label bodyLabel;
    juce::Label confirmationLabel;
    juce::TextButton shareButton{"Share"};
    juce::TextButton noThanksButton{"No thanks"};
    LinkButton learnMoreButton{"What we collect", juce::URL()};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UsageStatsPromptComponent)
};

} // namespace synth::ui
