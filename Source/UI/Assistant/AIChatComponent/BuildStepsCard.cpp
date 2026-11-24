#include "BuildStepsCard.h"
#include "AIChatComponent.h"
#include "DetailsPanelStyle.h"
#include "EditPlanDetailsText.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>

namespace synth {

using songsteps::StepStatus;
using synth::ui::StepIconState;

namespace {

constexpr int kBarHeight = 5;
constexpr int kThumbRowHeight = ThumbButton::kSize;
constexpr int kClockWidth = 44;
constexpr int kNowIconSize = 34;
constexpr float kHeightMs = 300.0f;
constexpr int kFeedbackRowHeight = 24;
constexpr int kDetailsPanelHeight = 160;

juce::String dot() { return juce::String::fromUTF8(" \xC2\xB7 "); }

int wrapped(const juce::Font& font, const juce::String& text, int width) {
    return AIChatComponent::computeWrappedTextHeight(font, text, width);
}

void styleLabel(juce::Label& label, float size, bool bold) {
    label.setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain));
    label.setBorderSize(juce::BorderSize<int>(0));
    label.setMinimumHorizontalScale(1.0f);
    label.setJustificationType(juce::Justification::topLeft);
    label.setInterceptsMouseClicks(false, false);
}

} // namespace

// -- the card
// ------------------------------------------------------------------------------------------------------------

BuildStepsCard::BuildStepsCard(SongSession& sessionIn, Actions actionsIn)
    : SongCardBase(sessionIn.fadeInPending)
    , session(sessionIn)
    , actions(std::move(actionsIn)) {
    session.fadeInPending = false;
    setTitle("Song build");

    for (auto* l : {&titleLabel, &metaLabel, &headerLabel, &clockLabel, &nowLabel, &subLabel, &outcomeLabel})
        addAndMakeVisible(*l);
    styleLabel(titleLabel, 15.0f, true);
    styleLabel(metaLabel, 12.0f, false);
    styleLabel(headerLabel, 12.0f, true);
    styleLabel(clockLabel, 12.0f, false);
    clockLabel.setJustificationType(juce::Justification::topRight);
    styleLabel(nowLabel, 14.0f, true);
    styleLabel(subLabel, 12.0f, false);
    styleLabel(outcomeLabel, 12.0f, false);
    clockLabel.setAccessible(false);
    outcomeLabel.setVisible(false);
    titleLabel.setTitle("Song name");
    metaLabel.setTitle("Tempo, key and length");
    headerLabel.setTitle("Build state");
    nowLabel.setTitle("Current step");
    subLabel.setTitle("Progress");
    addAndMakeVisible(nowIconView);

    stepsFold.setComponentID("songStepsFold");
    addAndMakeVisible(stepsFold);
    stepsFold.setTooltip("Show or hide the steps of this build");
    stepsFold.onOpenRequested = [this](bool open) {
        setListOpen(open, true);
        if (actions.onListToggled)
            actions.onListToggled(openTargetMs > 0.0f);
    };
    addAndMakeVisible(box);
    addAndMakeVisible(cancelButton);
    cancelButton.setTitle("Cancel song build");
    cancelButton.setTooltip("Stop building; the uses come back");
    cancelButton.onClick = [this] {
        if (actions.onCancel)
            actions.onCancel();
    };
    cancelFade_.onFrame = [this] { layoutChanged(); };

    detailsFold.setComponentID("songDetailsFold");
    addChildComponent(detailsFold);
    detailsFold.setTooltip("Show or hide what was built, track by track");
    detailsFold.onOpenRequested = [this](bool open) { setDetailsOpen(open, true); };

    thumbsUpButton.setComponentID("songThumbUp");
    thumbsDownButton.setComponentID("songThumbDown");
    addChildComponent(thumbsUpButton);
    thumbsUpButton.setTitle("This song was good");
    thumbsUpButton.setTooltip("This song was good");
    thumbsUpButton.onClick = [this] { setRating(session.rating == 1 ? 0 : 1); };
    addChildComponent(thumbsDownButton);
    thumbsDownButton.setTitle("This song missed the mark");
    thumbsDownButton.setTooltip("This song missed the mark");
    thumbsDownButton.onClick = [this] { setRating(session.rating == 2 ? 0 : 2); };

    addChildComponent(commentField);
    commentField.setText(session.ratingComment, juce::dontSendNotification);
    commentField.setTitle("Feedback comment");
    commentField.setTooltip("Optional: say why, then press Enter to send");
    commentField.onReturnKey = [this] { notifyRate(); };
    synth::ui::removeHiddenTabStops(commentField);
    addChildComponent(commentSaveButton);
    commentSaveButton.setButtonText("Send");
    commentSaveButton.setTitle("Send feedback comment");
    commentSaveButton.setTooltip("Send your feedback comment");
    commentSaveButton.onClick = [this] { notifyRate(); };

    addChildComponent(detailsDisplay);
    detailsDisplay.setMultiLine(true);
    detailsDisplay.setReadOnly(true);
    detailsDisplay.setTitle("Song details");
    detailsDisplay.setTooltip("What was built, track by track");
    synth::ui::removeHiddenTabStops(detailsDisplay);

    detailsFoldFade_.onFrame = [this] { layoutChanged(); };
    thumbsFade_.onFrame = [this] { layoutChanged(); };
    commentFade_.onFrame = [this] { layoutChanged(); };
    detailsFade_.onFrame = [this] { layoutChanged(); };

    syncBlocks();
    setListOpen(session.listOpen, false);
    detailsFold.setOpen(false);
    progressShown = session.model.progress();
    refresh();
    if (session.phase == SongSession::Phase::built) {
        if (session.detailsOpen)
            setDetailsOpen(true, false);
        commentFade_.setShown(session.rating != 0);
    }
}

BuildStepsCard::~BuildStepsCard() {
    listAnim.stop(vblank);
    barAnim.stop(vblank);
}

juce::String BuildStepsCard::nowText() const { return nowLabel.getText(); }
juce::String BuildStepsCard::progressText() const { return subLabel.getText(); }
juce::String BuildStepsCard::headerText() const { return headerLabel.getText(); }

void BuildStepsCard::setElapsedMs(juce::uint32 ms) {
    const auto text = formatClock(ms);
    if (clockLabel.getText() != text)
        clockLabel.setText(text, juce::dontSendNotification);
}

void BuildStepsCard::syncBlocks() { box.setSteps(session.model.steps()); }

juce::String BuildStepsCard::stateLine() const {
    const auto& m = session.model;
    const juce::String count = "step " + juce::String(m.currentNumber()) + " of " + juce::String(m.total());
    if (m.isFixing())
        return "Checking the result and trying again" + dot() + count;
    return m.total() > 0 ? count : juce::String();
}

void BuildStepsCard::refresh() {
    syncBlocks();
    const auto& m = session.model;
    const auto& plan = session.reply.plan;
    const auto phase = session.phase;
    const bool building = phase == SongSession::Phase::building;
    auto& icon = nowIconView;
    const auto& c = colours();

    // The song's name first, then its tempo and key: the same two lines the plan card starts with.
    titleLabel.setText(plan.title.isNotEmpty() ? plan.title : juce::String("Your song"), juce::dontSendNotification);
    metaLabel.setText(songsteps::planMetaText(plan), juce::dontSendNotification);

    if (building) {
        headerLabel.setText("Building your song", juce::dontSendNotification);
        const int cur = m.currentIndex();
        if (cur >= 0) {
            const auto& step = m.steps()[(size_t)cur];
            nowLabel.setText(step.info.label, juce::dontSendNotification);
            const auto kind = m.isFixing() ? songsteps::StepKind::fix : step.info.kind;
            icon.setKind(kind);
            icon.setColour(synth::ui::stepKindColour(c, kind));
            icon.setState(m.isFixing() ? StepIconState::fixing : StepIconState::running);
        } else {
            // No running step: either the step list hasn't arrived yet, or every step is done and the
            // server is assembling the song.
            nowLabel.setText(m.steps().empty() ? "Getting ready" : "Putting your song together",
                             juce::dontSendNotification);
            icon.setKind(songsteps::StepKind::plan);
            icon.setColour(synth::ui::stepKindColour(c, songsteps::StepKind::plan));
            icon.setState(StepIconState::running);
        }
        subLabel.setText(stateLine(), juce::dontSendNotification);
    } else if (phase == SongSession::Phase::built) {
        headerLabel.setText("Your song is ready", juce::dontSendNotification);
        nowLabel.setText(m.resultLine(), juce::dontSendNotification);
        subLabel.setText("Applied as one undo step.", juce::dontSendNotification);
        icon.setColour(c.success);
        icon.setState(StepIconState::done);
    } else {
        const bool cancelled = session.cancelled;
        headerLabel.setText(cancelled ? "Build cancelled" : "Build stopped", juce::dontSendNotification);
        nowLabel.setText(cancelled ? juce::String("You cancelled this build.") : session.failureText,
                         juce::dontSendNotification);
        const int units = session.reply.units;
        subLabel.setText(m.wasRefunded() || cancelled
                             ? "Your " + juce::String(units) + (units == 1 ? " use was" : " uses were") + " returned."
                             : juce::String(),
                         juce::dontSendNotification);
        icon.setKind(songsteps::StepKind::fix);
        icon.setColour(c.warning);
        icon.setState(StepIconState::still);
    }
    titleLabel.setColour(juce::Label::textColourId, c.textPrimary);
    metaLabel.setColour(juce::Label::textColourId, c.textMuted);
    nowLabel.setColour(juce::Label::textColourId, c.textPrimary);
    headerLabel.setColour(juce::Label::textColourId, c.textMuted);
    clockLabel.setColour(juce::Label::textColourId, c.textMuted);
    subLabel.setColour(juce::Label::textColourId, c.textMuted);

    const int done = m.finishedCount();
    stepsFold.setSummary(m.total() == 0 ? juce::String()
                                        : juce::String(done) + " of " + juce::String(m.total()) + " done");
    stepsFold.setVisible(m.total() > 0);
    box.setVisible(m.total() > 0 && openMs > 0.0f);
    cancelFade_.setShown(building);
    detailsFoldFade_.setShown(phase == SongSession::Phase::built);
    thumbsFade_.setShown(phase == SongSession::Phase::built);
    styleFeedbackControls();
    setElapsedMs(session.elapsedMs(juce::Time::getMillisecondCounter()));

    const float target = m.progress();
    if (std::abs(target - progressShown) > 0.001f) {
        const float from = progressShown;
        barAnim.start(
            vblank, synth::ui::motionMs(200.0, 80.0), synth::ui::easeOutCubic,
            [this, from, target](float t) {
                progressShown = from + (target - from) * t;
                repaint();
            },
            [this, target] {
                progressShown = target;
                repaint();
            });
    }
    layoutChanged();
}

void BuildStepsCard::setListOpen(bool open, bool animate) {
    openTargetMs = open ? kHeightMs : 0.0f;
    session.listOpen = open;
    stepsFold.setOpen(open);
    box.setBlocksFocusable(open);

    const bool canAnimate = animate && synth::ui::FadeVisibility::canAnimateIn(this);
    if (!canAnimate) {
        listAnim.stop(vblank);
        openMs = openTargetMs;
        layoutChanged();
        return;
    }
    // Reduce Motion plays the same timeline as a short fade; the height still follows it.
    const float from = openMs;
    const float to = openTargetMs;
    const double length = synth::ui::motionMs((double)std::abs(to - from), 80.0);
    listAnim.start(
        vblank, juce::jmax(1.0, length), [](float t) { return t; },
        [this, from, to](float t) {
            openMs = from + (to - from) * t;
            layoutChanged();
        },
        [this, to] {
            openMs = to;
            layoutChanged();
        });
}

void BuildStepsCard::parentHierarchyChanged() {
    SongCardBase::parentHierarchyChanged();
    styleFeedbackControls();
}

void BuildStepsCard::styleFeedbackControls() {
    const auto& c = colours();
    thumbsUpButton.setSelected(session.rating == 1);
    thumbsDownButton.setSelected(session.rating == 2);
    commentField.setTextToShowWhenEmpty("Optional: why? (Enter to send)", c.textMuted);
    synth::detailspanel::style(detailsDisplay, c);
}

void BuildStepsCard::setRating(int rating) {
    session.rating = rating;
    styleFeedbackControls();
    commentFade_.setShown(rating != 0);
    layoutChanged();
    notifyRate();
}

void BuildStepsCard::notifyRate() {
    if (actions.onRate)
        actions.onRate(session.rating, commentField.getText());
}

// The details text is laid out the first time the panel opens, not when the card is built.
void BuildStepsCard::setDetailsOpen(bool open, bool animate) {
    session.detailsOpen = open;
    if (open && !detailsFilled) {
        detailsFilled = true;
        detailsDisplay.setText(buildEditPlanDetailsText(session.detailsText.isNotEmpty()
                                                            ? session.detailsText
                                                            : juce::String("No further details for this song.")),
                               juce::dontSendNotification);
    }
    detailsFold.setOpen(open);
    detailsFade_.setShown(open);
    if (animate)
        layoutChanged();
}

// The Details fold, its panel, the thumbs row and the comment row: each opens with its own fade and a height that
// follows it.
int BuildStepsCard::feedbackHeight() const {
    const float fold = detailsFoldFade_.progress();
    const float thumbs = thumbsFade_.progress();
    return juce::roundToInt((float)(synth::ui::kFoldSectionHeaderHeight + kGap) * fold) +
           juce::roundToInt((float)(kDetailsPanelHeight + kGap) * detailsFade_.progress() * fold) +
           juce::roundToInt((float)(kThumbRowHeight + kGap) * thumbs) +
           juce::roundToInt((float)(kFeedbackRowHeight + kGap) * commentFade_.progress() * thumbs);
}

float BuildStepsCard::heightReveal() const {
    return synth::ui::easeOutCubic(juce::jlimit(0.0f, 1.0f, openMs / kHeightMs));
}

int BuildStepsCard::layoutFor(int width, bool apply) {
    const int inner = juce::jmax(60, width - kPadding * 2);
    int y = kPadding;

    // The song's name first (with the clock at the right), then tempo and key.
    const int titleW = inner - kClockWidth - kGap;
    const int titleH = juce::jmax(18, wrapped(titleLabel.getFont(), titleLabel.getText(), titleW));
    if (apply) {
        titleLabel.setBounds(kPadding, y, titleW, titleH);
        clockLabel.setBounds(kPadding + inner - kClockWidth, y, kClockWidth, 16);
    }
    y += titleH + 2;
    const int metaH = juce::jmax(16, wrapped(metaLabel.getFont(), metaLabel.getText(), inner));
    if (apply)
        metaLabel.setBounds(kPadding, y, inner, metaH);
    y += metaH + kGap;

    if (apply)
        headerLabel.setBounds(kPadding, y, inner, 16);
    y += 16 + kGap;

    const int textX = kNowIconSize + kGap;
    const int textW = inner - textX;
    const int nowH = wrapped(nowLabel.getFont(), nowLabel.getText(), textW);
    const int subH = subLabel.getText().isEmpty() ? 0 : wrapped(subLabel.getFont(), subLabel.getText(), textW);
    const int nowBlock = juce::jmax(kNowIconSize, nowH + (subH > 0 ? subH + 2 : 0));
    if (apply) {
        nowIconView.setBounds(kPadding, y + (nowBlock - kNowIconSize) / 2, kNowIconSize, kNowIconSize);
        nowLabel.setBounds(kPadding + textX, y + (nowBlock - nowH - (subH > 0 ? subH + 2 : 0)) / 2, textW, nowH);
        subLabel.setBounds(kPadding + textX, nowLabel.getBottom() + 2, textW, subH);
    }
    y += nowBlock + kGap;

    if (apply)
        barBounds = {kPadding, y, inner, kBarHeight};
    y += kBarHeight + kGap;

    // Fold one: Steps. The box grows and fades with the fold's timeline.
    const bool hasSteps = !session.model.steps().empty();
    if (apply)
        stepsFold.setBounds(kPadding, y, inner, synth::ui::kFoldSectionHeaderHeight);
    if (hasSteps) {
        y += synth::ui::kFoldSectionHeaderHeight;
        const float reveal = heightReveal();
        const int boxFull = box.heightFor(inner);
        const int boxNow = juce::roundToInt((float)boxFull * reveal);
        if (apply) {
            box.setBounds(kPadding, y + juce::roundToInt(4.0f * reveal), inner, boxNow);
            box.setAlpha(reveal);
            box.setVisible(boxNow > 0);
        }
        if (boxNow > 0)
            y += juce::roundToInt(4.0f * reveal) + boxNow;
        y += kGap;
    }

    // Fold two (built): Details, and under everything the thumbs and their comment.
    if (apply) {
        detailsFold.setBounds(kPadding, y, inner, synth::ui::kFoldSectionHeaderHeight);
        const float fold = detailsFoldFade_.progress();
        const int panelY = y + juce::roundToInt((float)(synth::ui::kFoldSectionHeaderHeight + kGap) * fold);
        detailsDisplay.setBounds(kPadding, panelY, inner, kDetailsPanelHeight);
        const int thumbsY =
            panelY + juce::roundToInt((float)(kDetailsPanelHeight + kGap) * detailsFade_.progress() * fold);
        // Left-aligned, icon-only, at the end of the card.
        thumbsUpButton.setBounds(kPadding, thumbsY, ThumbButton::kSize, kThumbRowHeight);
        thumbsDownButton.setBounds(kPadding + ThumbButton::kSize + 4, thumbsY, ThumbButton::kSize, kThumbRowHeight);
        const int commentY = thumbsY + kThumbRowHeight + kGap;
        auto commentRow = juce::Rectangle<int>(kPadding, commentY, inner, kFeedbackRowHeight);
        commentSaveButton.setBounds(commentRow.removeFromRight(55).reduced(2));
        commentField.setBounds(commentRow.reduced(2));
    }
    y += feedbackHeight();

    const int cancelH = juce::roundToInt((float)(28 + kGap) * cancelFade_.progress());
    if (apply)
        cancelButton.setBounds(kPadding, y, inner, 28);
    y += cancelH;
    return y - kGap + kPadding;
}

void BuildStepsCard::paint(juce::Graphics& g) {
    SongCardBase::paint(g);
    const auto& c = colours();
    const auto bar = barBounds.toFloat();
    g.setColour(c.border);
    g.fillRoundedRectangle(bar, bar.getHeight() / 2.0f);
    g.setColour(session.phase == SongSession::Phase::failed ? c.warning : c.accent);
    g.fillRoundedRectangle(bar.withWidth(juce::jmax(bar.getHeight(), bar.getWidth() * progressShown)),
                           bar.getHeight() / 2.0f);
}

} // namespace synth
