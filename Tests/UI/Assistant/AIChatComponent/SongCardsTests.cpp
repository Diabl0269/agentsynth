// The step icons and the two song cards on their own (no chat around them): what each state shows, the motion that
// must stop with reduced motion, the list that opens, and the names and tooltips every control carries.
#include "../../Accessibility/AccessibilityAudit.h"
#include "UI/Assistant/AIChatComponent/BuildStepsCard.h"
#include "UI/Assistant/AIChatComponent/SongPlanCard.h"
#include "UI/Assistant/AIChatComponent/StepIcons.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

using namespace synth;
using songsteps::StepKind;
using synth::ui::StepIconState;

namespace {

juce::Image render(StepKind kind, StepIconState state, float phase) {
    juce::Image image(juce::Image::ARGB, 48, 48, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    ui::paintStepIcon(g, {0.0f, 0.0f, 48.0f, 48.0f}, kind, state, phase, juce::Colours::white);
    return image;
}

bool identical(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y))
                return false;
    return true;
}

bool hasInk(const juce::Image& a) {
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt(x, y).getAlpha() > 40)
                return true;
    return false;
}

constexpr const char* kPlan =
    R"({"plan":{"title":"Dark drum and bass","bpm":174,"key":"A minor","special":"A rolling break.",
    "sections":[{"name":"Intro","bars":16,"energy":1,"chords":"Am","description":"pad and filtered drums"},
                {"name":"Drop","bars":32,"energy":5,"chords":"Am F","description":"full break"},
                {"name":"Break","bars":16,"energy":2,"chords":"Am","description":"pad and a slow lead"}],
    "tracks":[{"name":"Drum kit","role":"drums","instrument":"Drum Kit","sound":"x","sections":["Intro"]},
              {"name":"Reese bass","role":"bass","instrument":"Oscillator","sound":"y","sections":["Drop"]}],
    "lanes":[{"track":"Drum kit","what":"filter"}]},
    "price":{"units":5,"estimatedUsd":0.02},
    "steps":[{"id":"track:Drum kit","kind":"track","label":"Drum kit sound"},{"id":"pattern:Drum kit","kind":"pattern","label":"Writing the drum break"},
             {"id":"sections","kind":"sections","label":"Laying out 3 sections"}],"token":"tok"})";

SongSession makeSession() {
    SongSession session;
    juce::String error;
    EXPECT_TRUE(songsteps::parsePlanReply(kPlan, session.reply, error)) << error;
    session.adoptPlan(session.reply, true);
    session.requestText = "dark drum and bass";
    return session;
}

void feed(SongSession& session, const char* json) {
    const auto event = songsteps::parseStreamLine(json);
    ASSERT_TRUE(event.has_value()) << json;
    session.model.apply(*event);
}

void startBuild(SongSession& session) {
    session.phase = SongSession::Phase::building;
    session.buildStartMs = juce::Time::getMillisecondCounter();
    feed(session, R"({"type":"steps","steps":[{"id":"track:Drum kit","kind":"track","label":"Drum kit sound"},
        {"id":"pattern:Drum kit","kind":"pattern","label":"Writing the drum break"},{"id":"sections","kind":"sections","label":"Laying out 3 sections"}]})");
}

} // namespace

TEST(StepIcons, EveryKindDrawsInEveryState) {
    for (int k = 0; k <= (int)StepKind::mix; ++k)
        for (auto state : {StepIconState::still, StepIconState::running, StepIconState::fixing, StepIconState::done,
                           StepIconState::waiting, StepIconState::skipped})
            EXPECT_TRUE(hasInk(render((StepKind)k, state, 0.4f))) << "kind " << k;
}

TEST(StepIcons, ARunningIconMovesAndAStillOrFinishedOneDoesNot) {
    for (auto kind : {StepKind::plan, StepKind::track, StepKind::pattern, StepKind::sections, StepKind::automation,
                      StepKind::module, StepKind::connect, StepKind::settings, StepKind::modulation, StepKind::fix,
                      StepKind::mix}) {
        EXPECT_FALSE(identical(render(kind, StepIconState::running, 0.1f), render(kind, StepIconState::running, 0.6f)))
            << "kind " << (int)kind << " should move while running";
        EXPECT_TRUE(identical(render(kind, StepIconState::still, 0.1f), render(kind, StepIconState::still, 0.6f)));
        EXPECT_TRUE(identical(render(kind, StepIconState::done, 0.1f), render(kind, StepIconState::done, 0.6f)));
    }
}

TEST(StepIcons, ARunningViewSitsStillUnderReducedMotionAndOffScreen) {
    ui::setReducedMotionForTest(true);
    ui::StepIconView view;
    view.setSize(24, 24);
    view.setKind(StepKind::pattern);
    view.setState(StepIconState::running);
    EXPECT_FLOAT_EQ(view.phaseForTesting(), 0.0f) << "no loop starts when motion is reduced or the view is off screen";
    ui::setReducedMotionForTest(std::nullopt);
}

TEST(StepIcons, APlayedIconSettlesIntoACheckAtOnceWhenItCannotAnimate) {
    ui::StepIconView view;
    view.setState(StepIconState::still);
    view.playThenSettle(100);
    EXPECT_EQ(view.getState(), StepIconState::done);
}

TEST(BuildStepsCard, ShowsTheRunningStepWithItsCountAndATimeInTheList) {
    auto session = makeSession();
    startBuild(session);
    feed(session, R"({"type":"step","id":"track:Drum kit","status":"done","ms":5000,"summary":"punchy kit"})");
    feed(session, R"({"type":"step","id":"pattern:Drum kit","status":"running"})");
    BuildStepsCard card(session, {});
    card.setSize(280, card.getRequiredHeight(280));

    EXPECT_EQ(card.headerText(), juce::String("Building your song"));
    EXPECT_EQ(card.nowText(), juce::String("Writing the drum break"));
    EXPECT_EQ(card.progressText(), juce::String("step 2 of 3"));
    EXPECT_EQ(card.nowIcon().getKind(), StepKind::pattern);
    EXPECT_EQ(card.nowIcon().getState(), StepIconState::running);
    ASSERT_EQ(card.rowCount(), 3);
    EXPECT_EQ(card.rowIconState(0), StepIconState::done);
    EXPECT_EQ(card.rowIconState(1), StepIconState::running);
    EXPECT_EQ(card.rowIconState(2), StepIconState::waiting);
    EXPECT_EQ(card.row(0)->getTitle(), juce::String("Drum kit sound, done"));
    auto* tip = dynamic_cast<juce::TooltipClient*>(card.row(0));
    ASSERT_NE(tip, nullptr);
    EXPECT_EQ(tip->getTooltip(), juce::String("punchy kit")) << "the tooltip says what the step did";
}

TEST(BuildStepsCard, AFixingStepSaysSoInPlainWordsAndUsesTheWarningIcon) {
    auto session = makeSession();
    startBuild(session);
    feed(session, R"({"type":"step","id":"track:Drum kit","status":"fixing"})");
    BuildStepsCard card(session, {});
    EXPECT_EQ(card.nowIcon().getKind(), StepKind::fix);
    EXPECT_EQ(card.nowIcon().getState(), StepIconState::fixing);
    EXPECT_TRUE(card.progressText().startsWith("Checking the result and trying again")) << card.progressText();
    feed(session, R"({"type":"step","id":"track:Drum kit","status":"done"})");
    card.refresh();
    EXPECT_NE(card.nowIcon().getState(), StepIconState::fixing);
}

TEST(BuildStepsCard, ASkippedStepIsMarkedAndTheFinishedCardBecomesTheResult) {
    auto session = makeSession();
    startBuild(session);
    feed(session, R"({"type":"step","id":"track:Drum kit","status":"done"})");
    feed(session, R"({"type":"step","id":"pattern:Drum kit","status":"skipped"})");
    feed(session, R"({"type":"step","id":"sections","status":"done"})");
    feed(session, R"({"type":"done","envelope":{"mode":"merge"},"summary":{"sections":3,"seconds":200}})");
    session.phase = SongSession::Phase::built;
    BuildStepsCard card(session, {});
    EXPECT_EQ(card.rowIconState(1), StepIconState::skipped);
    EXPECT_EQ(card.row(1)->getTitle(), juce::String("Writing the drum break, left out"));
    EXPECT_EQ(card.headerText(), juce::String("Your song is ready"));
    EXPECT_EQ(card.nowText(), juce::String("3 steps, 3 sections, 3:20 (1 detail left out)"));
    EXPECT_EQ(card.nowIcon().getState(), StepIconState::done);
}

TEST(BuildStepsCard, ACancelledOrFailedBuildSaysSoAndThatTheUsesCameBack) {
    auto session = makeSession();
    startBuild(session);
    session.phase = SongSession::Phase::failed;
    session.cancelled = true;
    session.model.markFailed("Cancelled", true);
    BuildStepsCard cancelled(session, {});
    EXPECT_EQ(cancelled.headerText(), juce::String("Build cancelled"));
    EXPECT_EQ(cancelled.progressText(), juce::String("Your 5 uses were returned."));

    auto failed = makeSession();
    startBuild(failed);
    failed.phase = SongSession::Phase::failed;
    failed.failureText = "The build failed";
    failed.model.markFailed("The build failed", true);
    BuildStepsCard stopped(failed, {});
    EXPECT_EQ(stopped.headerText(), juce::String("Build stopped"));
    EXPECT_EQ(stopped.nowText(), juce::String("The build failed"));
}

TEST(BuildStepsCard, ShowAllStepsOpensTheListAndGrowsTheCardAtOnceOffScreen) {
    auto session = makeSession();
    startBuild(session);
    bool toggled = false;
    BuildStepsCard::Actions actions;
    actions.onListToggled = [&](bool open) { toggled = open; };
    BuildStepsCard card(session, actions);
    const int closed = card.getRequiredHeight(280);
    card.toggleListForTesting();
    EXPECT_TRUE(toggled);
    EXPECT_TRUE(card.listOpenForTesting());
    EXPECT_GE(card.getRequiredHeight(280), closed + 3 * 24) << "the card grows by the rows";
    for (int i = 0; i < card.rowCount(); ++i)
        EXPECT_FLOAT_EQ(card.row(i)->getAlpha(), 1.0f) << "off screen the rows land fully shown";
    card.toggleListForTesting();
    EXPECT_EQ(card.getRequiredHeight(280), closed);
}

TEST(BuildStepsCard, CancelCallsBackAndEveryControlHasANameAndATooltip) {
    auto session = makeSession();
    startBuild(session);
    int cancels = 0;
    BuildStepsCard::Actions actions;
    actions.onCancel = [&] { ++cancels; };
    BuildStepsCard card(session, actions);
    card.setSize(280, card.getRequiredHeight(280));
    card.pressCancelForTesting();
    EXPECT_EQ(cancels, 1);
    card.toggleListForTesting();
    card.setSize(280, card.getRequiredHeight(280));
    const auto gaps = synth::test::auditAccessibility(card);
    for (const auto& gap : gaps)
        ADD_FAILURE() << (gap.kind == synth::test::Gap::Kind::MissingName ? "missing name " : "missing tooltip ")
                      << gap.path;
}

TEST(SongPlanCard, ShowsTheFactsAndThePriceInUses) {
    auto session = makeSession();
    session.usesLeft = 412;
    session.usesLimit = 500;
    SongPlanCard card(session, {});
    card.setSize(280, card.getRequiredHeight(280));
    EXPECT_EQ(card.buildButtonText(), juce::String::fromUTF8("Build song \xC2\xB7 5 uses"));
    EXPECT_EQ(card.sectionSegmentCount(), 3);
    auto* tip = dynamic_cast<juce::TooltipClient*>(card.sectionSegment(2));
    ASSERT_NE(tip, nullptr);
    EXPECT_EQ(tip->getTooltip(), juce::String::fromUTF8("Break \xC2\xB7 16 bars \xC2\xB7 pad and a slow lead"));
    EXPECT_TRUE(card.sectionSegment(2)->getWantsKeyboardFocus());
    EXPECT_EQ(session.lengthIndex, 0) << "1:50 opens on 2 min";
}

TEST(SongPlanCard, ChangingLengthOrDetailAsksToReplanAndBuildCallsBack) {
    auto session = makeSession();
    int lengthPicked = -1, detailPicked = -1, builds = 0;
    SongPlanCard::Actions actions;
    actions.onReplan = [&](int l, int d) {
        lengthPicked = l;
        detailPicked = d;
    };
    actions.onBuild = [&] { ++builds; };
    SongPlanCard card(session, actions);
    card.chooseForTesting(2, session.detailIndex);
    EXPECT_EQ(lengthPicked, 2);
    EXPECT_EQ(detailPicked, session.detailIndex);
    card.chooseForTesting(session.lengthIndex, 2);
    EXPECT_EQ(detailPicked, 2);
    card.pressBuildForTesting();
    EXPECT_EQ(builds, 1);
}

TEST(SongPlanCard, ANewPriceShowsAfterARefreshAndOnePerUseIsSingular) {
    auto session = makeSession();
    SongPlanCard card(session, {});
    session.reply.units = 1;
    card.refresh();
    EXPECT_EQ(card.buildButtonText(), juce::String::fromUTF8("Build song \xC2\xB7 1 use"));
}

TEST(SongPlanCard, ReplanningDisablesBuildAndADismissedPlanOffersNothing) {
    auto session = makeSession();
    SongPlanCard card(session, {});
    session.phase = SongSession::Phase::replanning;
    card.refresh();
    card.setSize(280, card.getRequiredHeight(280));
    for (auto* c : card.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(c))
            EXPECT_FALSE(b->isEnabled());
    session.phase = SongSession::Phase::dismissed;
    card.refresh();
    const int dismissedHeight = card.getRequiredHeight(280);
    session.phase = SongSession::Phase::ready;
    card.refresh();
    EXPECT_LT(dismissedHeight, card.getRequiredHeight(280)) << "a dismissed plan is just its title and a line";
}

TEST(SongPlanCard, EveryControlHasANameAndATooltip) {
    auto session = makeSession();
    session.usesLeft = 3;
    session.usesLimit = 500;
    SongPlanCard card(session, {});
    card.setSize(280, card.getRequiredHeight(280));
    for (const auto& gap : synth::test::auditAccessibility(card))
        ADD_FAILURE() << (gap.kind == synth::test::Gap::Kind::MissingName ? "missing name " : "missing tooltip ")
                      << gap.path;
}

TEST(SongPlanCard, TheTextIsNeverAllCaps) {
    auto session = makeSession();
    SongPlanCard card(session, {});
    BuildStepsCard build(session, {});
    for (auto* c : std::initializer_list<juce::Component*>{&card, &build})
        for (auto* child : c->getChildren())
            if (auto* label = dynamic_cast<juce::Label*>(child);
                label != nullptr && label->getText().containsAnyOf("abcdefghijklmnopqrstuvwxyz"))
                EXPECT_NE(label->getText(), label->getText().toUpperCase()) << label->getText();
}
