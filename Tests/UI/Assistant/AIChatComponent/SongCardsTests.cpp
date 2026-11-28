// The step icons and the two song cards on their own (no chat around them): what each state shows, the motion that
// must stop with reduced motion, the list that opens, and the names and tooltips every control carries.
#include "../../Accessibility/AccessibilityAudit.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Assistant/AIChatComponent/BuildStepsCard.h"
#include "UI/Assistant/AIChatComponent/SongFollowUps.h"
#include "UI/Assistant/AIChatComponent/SongPlanCard.h"
#include "UI/Assistant/AIChatComponent/StepIcons.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/BuiltInThemes.h"
#include <cmath>
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
    EXPECT_EQ(card.rowStatus(0), songsteps::StepStatus::done);
    EXPECT_EQ(card.rowStatus(1), songsteps::StepStatus::running);
    EXPECT_EQ(card.rowStatus(2), songsteps::StepStatus::waiting);
    EXPECT_EQ(card.row(0)->getTitle(), juce::String("Drum kit sound, done"));
    auto* tip = dynamic_cast<juce::TooltipClient*>(card.row(0));
    ASSERT_NE(tip, nullptr);
    EXPECT_EQ(tip->getTooltip(), juce::String("Drum kit sound: done in 0:05. punchy kit"))
        << "the tooltip says what the step did";
}

// The Steps fold is the app's standard fold header: its count shows as the header's summary and its state names it.
TEST(BuildStepsCard, TheStepsFoldIsTheStandardHeaderWithTheCountAsItsSummary) {
    auto session = makeSession();
    startBuild(session);
    feed(session, R"({"type":"step","id":"track:Drum kit","status":"done","ms":5000,"summary":"punchy kit"})");
    BuildStepsCard card(session, {});
    card.setSize(280, card.getRequiredHeight(280));
    auto& fold = card.stepsFoldForTesting();
    EXPECT_EQ(fold.getSummary(), juce::String("1 of 3 done"));
    EXPECT_EQ(fold.getHeight(), ui::kFoldSectionHeaderHeight);
    EXPECT_EQ(fold.getTitle(), juce::String("Steps, collapsed"));
    EXPECT_TRUE(fold.getWantsKeyboardFocus());
    card.toggleListForTesting();
    EXPECT_EQ(fold.getTitle(), juce::String("Steps, expanded"));
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
    EXPECT_EQ(card.rowStatus(1), songsteps::StepStatus::skipped);
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
    EXPECT_GE(card.getRequiredHeight(280), closed + card.stepBox().heightFor(280 - 20)) << "the card grows by the box";
    EXPECT_FLOAT_EQ(card.stepBox().getAlpha(), 1.0f) << "off screen the box lands fully shown";
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

TEST(BuildStepsCard, ShowDetailsAppearsOnlyOnceBuiltAndOpensTheSongDetails) {
    auto session = makeSession();
    startBuild(session);
    session.detailsText = "Drum kit: Drum Kit, 12 notes";
    BuildStepsCard building(session, {});
    building.setSize(280, building.getRequiredHeight(280));
    EXPECT_FALSE(building.detailsButtonShownForTesting()) << "nothing to show while the build runs";

    feed(session, R"({"type":"done","envelope":{"mode":"merge"},"summary":{"sections":3,"seconds":200}})");
    session.phase = SongSession::Phase::built;
    BuildStepsCard card(session, {});
    card.setSize(280, card.getRequiredHeight(280));
    EXPECT_TRUE(card.detailsButtonShownForTesting());
    const int closed = card.getRequiredHeight(280);
    card.toggleDetailsForTesting();
    EXPECT_TRUE(card.detailsOpenForTesting());
    EXPECT_EQ(card.detailsShownTextForTesting(), session.detailsText);
    EXPECT_GT(card.getRequiredHeight(280), closed);
    card.toggleDetailsForTesting();
    EXPECT_EQ(card.getRequiredHeight(280), closed);
}

TEST(BuildStepsCard, ABuiltSongWithoutDetailsSaysSoAndThumbsReachTheChat) {
    auto session = makeSession();
    startBuild(session);
    feed(session, R"({"type":"done","envelope":{"mode":"merge"},"summary":{"sections":3,"seconds":200}})");
    session.phase = SongSession::Phase::built;
    int lastRating = -1;
    BuildStepsCard::Actions actions;
    actions.onRate = [&](int rating, const juce::String&) { lastRating = rating; };
    BuildStepsCard card(session, actions);
    card.toggleDetailsForTesting();
    EXPECT_EQ(card.detailsShownTextForTesting(), juce::String("No further details for this song."));
    card.pressThumbForTesting(true);
    EXPECT_EQ(lastRating, 1);
    EXPECT_EQ(session.rating, 1);
    card.pressThumbForTesting(false);
    EXPECT_EQ(lastRating, 2);
}

TEST(BuildStepsCard, TheBuiltCardsControlsAllHaveANameAndATooltip) {
    auto session = makeSession();
    startBuild(session);
    feed(session, R"({"type":"done","envelope":{"mode":"merge"},"summary":{"sections":3,"seconds":200}})");
    session.phase = SongSession::Phase::built;
    session.rating = 1;
    session.detailsOpen = true;
    BuildStepsCard card(session, {});
    card.setSize(280, card.getRequiredHeight(280));
    for (const auto& gap : synth::test::auditAccessibility(card))
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
    EXPECT_EQ(tip->getTooltip(), juce::String::fromUTF8("Break \xC2\xB7 16 bars \xC2\xB7 energy 2 of 5 \xC2\xB7 "
                                                        "pad and a slow lead"));
    EXPECT_TRUE(card.sectionSegment(2)->getWantsKeyboardFocus());
    EXPECT_EQ(session.lengthIndex, 0) << "1:50 opens on 2 min";
}

TEST(SongPlanCard, ListsEachKeyChangeWithItsBarAndShowsNothingForASingleKey) {
    auto session = makeSession();
    {
        SongPlanCard card(session, {});
        card.setSize(280, card.getRequiredHeight(280));
        EXPECT_TRUE(card.keyLineForTesting().isEmpty()) << "one key is already in the meta line";
    }
    juce::String plan(kPlan);
    plan = plan.replace(R"("name":"Drop","bars":32,)", R"("name":"Drop","bars":32,"key":"C major",)")
               .replace(R"("name":"Break","bars":16,)", R"("name":"Break","bars":16,"key":"A minor",)");
    juce::String error;
    SongSession keyed;
    ASSERT_TRUE(songsteps::parsePlanReply(plan, keyed.reply, error)) << error;
    keyed.adoptPlan(keyed.reply, true);
    SongPlanCard withKeys(keyed, {});
    const int plainHeight = SongPlanCard(session, {}).getRequiredHeight(280);
    EXPECT_GT(withKeys.getRequiredHeight(280), plainHeight) << "the line takes its own height";
    withKeys.setSize(280, withKeys.getRequiredHeight(280));
    EXPECT_EQ(withKeys.keyLineForTesting(), juce::String("Key A minor, changes to C major at bar 17 (Drop), then "
                                                         "A minor at bar 49 (Break)"));
}

TEST(SongPlanCard, TheMelodyLineNamesTheLeadOrTheOlderServerAndIsAbsentWithoutAMelody) {
    auto session = makeSession();
    SongPlanCard plain(session, {});
    plain.setSize(280, plain.getRequiredHeight(280));
    EXPECT_TRUE(plain.melodyLineForTesting().isEmpty());

    session.sentMelodyName = "Hum melody";
    SongPlanCard ignored(session, {});
    ignored.setSize(280, ignored.getRequiredHeight(280));
    EXPECT_EQ(ignored.melodyLineForTesting(),
              juce::String("This server can't build around a melody yet, so the song ignores \"Hum melody\"."));
    EXPECT_GT(ignored.getRequiredHeight(280), plain.getRequiredHeight(280)) << "the line takes its own height";

    session.reply.plan.melodyTrack = "Kit";
    session.reply.plan.melodyName = "Hum melody";
    session.reply.plan.melodyBars = 4;
    SongPlanCard built(session, {});
    built.setSize(280, built.getRequiredHeight(280));
    EXPECT_EQ(built.melodyLineForTesting(),
              juce::String("The \"Kit\" track plays your melody \"Hum melody\" (4 bars), note for note."));
}

TEST(SongPlanCard, NoSectionChipShowsASingleLetterAndEachKeepsItsFullNameAndBars) {
    for (const int width : {120, 200, 260, 280, 400}) {
        auto session = makeSession();
        SongPlanCard card(session, {});
        card.setSize(width, card.getRequiredHeight(width));
        for (int i = 0; i < card.sectionSegmentCount(); ++i) {
            const auto label = card.sectionSegmentLabelForTesting(i);
            EXPECT_NE(label.length(), 1) << "width " << width << " chip " << i;
            EXPECT_FALSE(label.isNotEmpty() && label.length() < 3) << "width " << width << " chip " << i;
            auto* segment = card.sectionSegment(i);
            EXPECT_TRUE(segment->getTitle().contains("bars")) << "the accessible name carries the bar count";
            auto* tip = dynamic_cast<juce::TooltipClient*>(segment);
            ASSERT_NE(tip, nullptr);
            EXPECT_TRUE(tip->getTooltip().contains("bars"));
        }
    }
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

// -- the new look: colour per kind, tempo and key, many steps, labelled sections, thumbs ------------------------------

namespace {

double colourDistance(juce::Colour a, juce::Colour b) {
    const double dr = a.getRed() - b.getRed(), dg = a.getGreen() - b.getGreen(), db = a.getBlue() - b.getBlue();
    return std::sqrt(dr * dr + dg * dg + db * db);
}

void feedManySteps(SongSession& session, int count, int runningIndex) {
    juce::String steps;
    const StepKind kinds[] = {StepKind::track,  StepKind::pattern, StepKind::sections, StepKind::automation,
                              StepKind::module, StepKind::connect, StepKind::settings, StepKind::modulation,
                              StepKind::mix,    StepKind::plan};
    for (int i = 0; i < count; ++i) {
        if (i > 0)
            steps << ",";
        steps << "{\"id\":\"s" << i << "\",\"kind\":\"" << songsteps::stepKindToString(kinds[i % 10])
              << "\",\"label\":\"Step number " << i << "\"}";
    }
    feed(session, ("{\"type\":\"steps\",\"steps\":[" + steps + "]}").toRawUTF8());
    session.phase = SongSession::Phase::building;
    for (int i = 0; i < runningIndex; ++i)
        feed(session, ("{\"type\":\"step\",\"id\":\"s" + juce::String(i) + "\",\"status\":\"done\"}").toRawUTF8());
    feed(session,
         ("{\"type\":\"step\",\"id\":\"s" + juce::String(runningIndex) + "\",\"status\":\"running\"}").toRawUTF8());
}

void mouseClick(juce::Component& comp) {
    const auto pos = comp.getLocalBounds().getCentre().toFloat();
    auto event = [&] {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys(), 1.0f,
                                0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), pos,
                                juce::Time::getCurrentTime(), 1, false);
    };
    comp.mouseDown(event());
    comp.mouseUp(event());
}

} // namespace

TEST(StepKindColour, EveryKindHasItsOwnColourInEveryTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        for (int a = 0; a <= (int)StepKind::mix; ++a)
            for (int b = a + 1; b <= (int)StepKind::mix; ++b)
                EXPECT_GE(colourDistance(ui::stepKindColour(theme.colors, (StepKind)a),
                                         ui::stepKindColour(theme.colors, (StepKind)b)),
                          14.0)
                    << theme.id << ": kinds " << a << " and " << b << " look alike";
        for (int k = 0; k <= (int)StepKind::mix; ++k)
            EXPECT_TRUE(ui::stepKindDescription((StepKind)k).isNotEmpty());
    }
}

TEST(StepKindColour, ABlockWearsItsKindsColour) {
    auto session = makeSession();
    startBuild(session);
    BuildStepsCard card(session, {});
    const auto& colours = synth::theme::themeOf(card).colors;
    auto* block = dynamic_cast<StepBlock*>(card.row(1));
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->kindColour(), ui::stepKindColour(colours, StepKind::pattern));
    EXPECT_NE(block->kindColour(), dynamic_cast<StepBlock*>(card.row(0))->kindColour());
}

TEST(BuildStepsCard, TheSongNameComesFirstThenTempoAndKey) {
    auto session = makeSession();
    startBuild(session);
    BuildStepsCard card(session, {});
    card.setSize(280, card.getRequiredHeight(280));
    EXPECT_EQ(card.titleText(), juce::String("Dark drum and bass"));
    EXPECT_TRUE(card.metaText().contains("174 BPM")) << card.metaText();
    EXPECT_TRUE(card.metaText().contains("A minor")) << card.metaText();
}

TEST(SongPlanCard, StartsWithTheSongNameThenTempoAndKeyThenTheSections) {
    auto session = makeSession();
    SongPlanCard card(session, {});
    card.setSize(280, card.getRequiredHeight(280));
    EXPECT_EQ(card.titleTextForTesting(), juce::String("Dark drum and bass"));
    EXPECT_TRUE(card.metaTextForTesting().contains("174 BPM")) << card.metaTextForTesting();
    EXPECT_TRUE(card.metaTextForTesting().contains("A minor")) << card.metaTextForTesting();
    EXPECT_EQ(card.titleLabelForTesting().getY(), SongCardBase::kPadding) << "the song name is the first line";
    EXPECT_LT(card.titleLabelForTesting().getY(), card.metaLabelForTesting().getY());
    EXPECT_LT(card.metaLabelForTesting().getBottom(), card.sectionSegment(0)->getParentComponent()->getY());
}

TEST(BuildStepsCard, ThirtyStepsWrapOrScrollInsideTheBoxAndNeverOverflowSideways) {
    for (const int width : {160, 220, 280, 340, 400}) {
        auto session = makeSession();
        feedManySteps(session, 30, 12);
        BuildStepsCard card(session, {});
        card.toggleListForTesting();
        card.setSize(width, card.getRequiredHeight(width));
        auto& box = card.stepBox();
        ASSERT_EQ(box.blockCount(), 30);
        EXPECT_TRUE(card.getLocalBounds().contains(box.getBounds())) << "width " << width << ": the box is in the card";
        EXPECT_LE(box.getHeight(), StepBlockBox::kMaxHeight) << "the box height is capped";
        const int contentW = box.viewportForTesting().getViewedComponent()->getWidth();
        EXPECT_LE(contentW, box.getWidth()) << "width " << width;
        const bool scrolls = box.scrolls(box.getWidth());
        for (int i = 0; i < 30; ++i) {
            const auto inContent = box.blockBoundsInContent(i);
            EXPECT_GE(inContent.getX(), 0) << "width " << width << " block " << i;
            EXPECT_LE(inContent.getRight(), contentW) << "width " << width << " block " << i << " runs off the side";
            EXPECT_LE(inContent.getBottom(), box.viewportForTesting().getViewedComponent()->getHeight());
            if (!scrolls)
                EXPECT_TRUE(box.getLocalBounds().contains(box.blockBoundsInBox(i)))
                    << "width " << width << " block " << i << " is clipped by the box";
        }
        if (scrolls) {
            box.scrollToShow(*box.block(29));
            EXPECT_TRUE(box.getLocalBounds().contains(box.blockBoundsInBox(29)))
                << "width " << width << ": the last block can be scrolled into view";
        } else {
            EXPECT_EQ(box.getHeight(), box.contentHeightFor(box.getWidth())) << "wrapped onto further rows, no scroll";
        }
    }
}

TEST(BuildStepsCard, ANarrowBoxWithThirtyStepsScrollsAndAWideOneDoesNot) {
    auto session = makeSession();
    feedManySteps(session, 30, 0);
    BuildStepsCard card(session, {});
    EXPECT_TRUE(card.stepBox().scrolls(150));
    EXPECT_FALSE(card.stepBox().scrolls(400));
    EXPECT_EQ(card.stepBox().heightFor(150), StepBlockBox::kMaxHeight);
}

TEST(BuildStepsCard, TheHighlightFollowsTheRunningStep) {
    auto session = makeSession();
    startBuild(session);
    feed(session, R"({"type":"step","id":"track:Drum kit","status":"running"})");
    BuildStepsCard card(session, {});
    card.toggleListForTesting();
    card.setSize(280, card.getRequiredHeight(280));
    EXPECT_EQ(card.stepBox().highlightedIndexForTesting(), 0);
    feed(session, R"({"type":"step","id":"track:Drum kit","status":"done"})");
    feed(session, R"({"type":"step","id":"pattern:Drum kit","status":"running"})");
    card.refresh();
    EXPECT_EQ(card.stepBox().highlightedIndexForTesting(), 1);
    const auto ring = card.stepBox().highlightRectForTesting();
    EXPECT_TRUE(ring.contains(card.stepBox().blockBoundsInContent(1).toFloat()))
        << "the ring sits on the running block";
    feed(session, R"({"type":"step","id":"pattern:Drum kit","status":"done"})");
    feed(session, R"({"type":"step","id":"sections","status":"done"})");
    card.refresh();
    EXPECT_EQ(card.stepBox().highlightedIndexForTesting(), -1);
}

TEST(BuildStepsCard, EveryBlockIsATabStopWithANameAndATooltipWhileTheFoldIsOpen) {
    auto session = makeSession();
    startBuild(session);
    BuildStepsCard card(session, {});
    for (int i = 0; i < card.rowCount(); ++i)
        EXPECT_FALSE(card.row(i)->getWantsKeyboardFocus()) << "folded away, no block is a stop";
    card.toggleListForTesting();
    card.setSize(280, card.getRequiredHeight(280));
    for (int i = 0; i < card.rowCount(); ++i) {
        EXPECT_TRUE(card.row(i)->getWantsKeyboardFocus());
        EXPECT_TRUE(card.row(i)->getTitle().isNotEmpty());
        auto* tip = dynamic_cast<juce::TooltipClient*>(card.row(i));
        ASSERT_NE(tip, nullptr);
        EXPECT_TRUE(tip->getTooltip().isNotEmpty());
    }
    EXPECT_TRUE(synth::test::auditAccessibility(card).empty());
}

TEST(SongPlanCard, EachSectionIsLabelledAndItsTooltipSaysWhatGoesIntoIt) {
    auto session = makeSession();
    SongPlanCard card(session, {});
    card.setSize(400, card.getRequiredHeight(400));
    EXPECT_EQ(card.sectionsCaptionForTesting(), juce::String("Sections"));
    EXPECT_TRUE(card.sectionsTooltipForTesting().contains("bars"));
    for (int i = 0; i < card.sectionSegmentCount(); ++i)
        EXPECT_TRUE(card.sectionSegmentLabelForTesting(i).isNotEmpty()) << "section " << i << " shows its name";
    auto tip = [&](int i) { return dynamic_cast<juce::TooltipClient*>(card.sectionSegment(i))->getTooltip(); };
    EXPECT_TRUE(tip(0).contains("plays: Drum kit")) << tip(0);
    EXPECT_TRUE(tip(1).contains("plays: Reese bass")) << tip(1);
    EXPECT_TRUE(tip(1).contains("full break")) << tip(1);
    EXPECT_TRUE(tip(1).contains("energy 5 of 5")) << tip(1);
}

TEST(SongPlanCard, TheDetailsFoldIsTheStandardHeader) {
    auto session = makeSession();
    SongPlanCard card(session, {});
    card.setSize(280, card.getRequiredHeight(280));
    auto& fold = dynamic_cast<ui::FoldSectionHeader&>(card.detailsFoldForTesting());
    EXPECT_EQ(fold.getHeight(), ui::kFoldSectionHeaderHeight);
    EXPECT_TRUE(fold.getWantsKeyboardFocus());
    EXPECT_EQ(fold.getTitle(), juce::String("Details, collapsed"));
    fold.onClick();
    EXPECT_EQ(fold.getTitle(), juce::String("Details, expanded"));
}

TEST(BuildStepsCard, TheThumbsAreDrawnIconsRightAlignedUnderTheFinishedContent) {
    auto session = makeSession();
    startBuild(session);
    feed(session, R"({"type":"done","envelope":{"mode":"merge"},"summary":{"sections":3,"seconds":200}})");
    session.phase = SongSession::Phase::built;
    int rating = -1;
    BuildStepsCard::Actions actions;
    actions.onRate = [&](int r, const juce::String&) { rating = r; };
    BuildStepsCard card(session, actions);
    card.toggleListForTesting();
    card.toggleDetailsForTesting();
    card.setSize(280, card.getRequiredHeight(280));
    auto& up = card.thumbForTesting(true);
    auto& down = card.thumbForTesting(false);
    EXPECT_TRUE(up.isVisible());
    EXPECT_EQ(down.getRight(), card.getWidth() - SongCardBase::kPadding) << "right-aligned to the content's edge";
    EXPECT_EQ(up.getY(), down.getY()) << "one row";
    EXPECT_EQ(down.getX() - up.getRight(), ThumbButton::kGap) << "the token gap between the two";
    EXPECT_GT(up.getX(), card.getWidth() / 2) << "a small row at the right, not spread across the card";
    EXPECT_GE(up.getY(), card.detailsFoldForTesting().getBottom()) << "under the details";
    EXPECT_GE(up.getY(), card.stepBox().getBottom()) << "under the steps";
    EXPECT_GT(card.followUpForTesting(0).getY(), up.getBottom()) << "the follow-ups come after the thumbs";
    EXPECT_GE(card.getHeight() - card.followUpForTesting(2).getBottom(), SongCardBase::kPadding - 1)
        << "the last thing in the card";
    for (auto* thumb : {&up, &down}) {
        EXPECT_TRUE(thumb->getButtonText().containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"))
            << "no emoji text glyph";
        EXPECT_TRUE(thumb->getWantsKeyboardFocus());
        EXPECT_TRUE(thumb->getTitle().isNotEmpty());
    }
    const auto upPath = ThumbButton::thumbPath(true, {0, 0, 24, 24});
    const auto downPath = ThumbButton::thumbPath(false, {0, 0, 24, 24});
    EXPECT_FALSE(upPath.isEmpty());
    EXPECT_NEAR(upPath.getBounds().getHeight(), downPath.getBounds().getHeight(), 0.01f);
    EXPECT_NE(upPath.getBounds().getCentreY(), downPath.getBounds().getCentreY()) << "the dislike is turned over";

    mouseClick(up);
    EXPECT_EQ(rating, 1) << "a real click on the like thumb rates the song";
    EXPECT_TRUE(up.isSelected());
    mouseClick(down);
    EXPECT_EQ(rating, 2);
    EXPECT_TRUE(down.isSelected());
    EXPECT_FALSE(up.isSelected());
}

TEST(BuildStepsCard, TheBuiltCardsFoldsAndThumbsAreNamed) {
    auto session = makeSession();
    startBuild(session);
    feed(session, R"({"type":"done","envelope":{"mode":"merge"},"summary":{"sections":3,"seconds":200}})");
    session.phase = SongSession::Phase::built;
    BuildStepsCard card(session, {});
    card.toggleListForTesting();
    card.setSize(280, card.getRequiredHeight(280));
    EXPECT_EQ(card.detailsFoldForTesting().getTitle(), juce::String("Details, collapsed"));
    EXPECT_EQ(card.thumbForTesting(true).getTitle(), juce::String("This song was good"));
    EXPECT_EQ(card.thumbForTesting(false).getTitle(), juce::String("This song missed the mark"));
    EXPECT_TRUE(synth::test::auditAccessibility(card).empty());
}

// -- follow-up suggestions ---------------------------------------------------------------------------------------

namespace {

SongSession builtSession() {
    auto session = makeSession();
    startBuild(session);
    feed(session, R"({"type":"done","envelope":{"mode":"merge"},"summary":{"sections":3,"seconds":200}})");
    session.phase = SongSession::Phase::built;
    return session;
}

} // namespace

TEST(SongFollowUps, AreThreeShortPlainSuggestionsThatNeverReadAsANewSong) {
    auto session = makeSession();
    const auto texts = songsteps::songFollowUps(session.reply.plan);
    ASSERT_EQ(texts.size(), 3u);
    EXPECT_EQ(texts[0], juce::String("Make it darker"));
    EXPECT_EQ(texts[1], juce::String("Change the key"));
    EXPECT_EQ(texts[2], juce::String("Why did you pick this tempo?"));
    EXPECT_EQ(songsteps::songFollowUps(session.reply.plan), texts) << "deterministic";
    songsteps::SongPlan bare;
    const auto fallback = songsteps::songFollowUps(bare);
    ASSERT_EQ(fallback.size(), 3u);
    const juce::StringArray modules{"Filter", "Reverb"};
    for (const auto& list : {texts, fallback})
        for (const auto& text : list) {
            EXPECT_TRUE(juce::CharacterFunctions::isUpperCase(text[0])) << text;
            EXPECT_LT(text.length(), 40) << text;
            EXPECT_FALSE(AIChatComponent::shouldPlanSong(text, modules)) << text << " would start a new song";
        }
}

TEST(BuildStepsCard, FollowUpsExistOnlyOnceTheSongIsBuilt) {
    auto session = makeSession();
    {
        BuildStepsCard planning(session, {});
        planning.setSize(280, planning.getRequiredHeight(280));
        for (int i = 0; i < planning.followUpCountForTesting(); ++i)
            EXPECT_FALSE(planning.followUpForTesting(i).isVisible());
    }
    startBuild(session);
    BuildStepsCard building(session, {});
    building.setSize(280, building.getRequiredHeight(280));
    for (int i = 0; i < building.followUpCountForTesting(); ++i)
        EXPECT_FALSE(building.followUpForTesting(i).isVisible()) << "not while building";
    auto built = builtSession();
    BuildStepsCard card(built, {});
    card.setSize(280, card.getRequiredHeight(280));
    for (int i = 0; i < card.followUpCountForTesting(); ++i)
        EXPECT_TRUE(card.followUpForTesting(i).isVisible());
}

TEST(BuildStepsCard, FollowUpsAreTabStopsAfterTheThumbsWithANameAndATooltipAndAPressSendsTheirText) {
    auto session = builtSession();
    juce::String got;
    BuildStepsCard::Actions actions;
    actions.onFollowUp = [&](const juce::String& text) { got = text; };
    BuildStepsCard card(session, actions);
    card.setSize(280, card.getRequiredHeight(280));
    const auto order = card.getChildren();
    auto indexOf = [&](juce::Component* c) { return order.indexOf(c); };
    for (int i = 0; i < 3; ++i) {
        auto& chip = card.followUpForTesting(i);
        EXPECT_TRUE(chip.getWantsKeyboardFocus());
        EXPECT_EQ(chip.getTitle(), chip.getButtonText());
        EXPECT_EQ(chip.getTooltip(), juce::String("Put this in the chat box"));
        EXPECT_EQ(chip.getComponentID(), "songFollowUp" + juce::String(i));
        EXPECT_GT(indexOf(&chip), indexOf(&card.thumbForTesting(false))) << "Tab reaches the thumbs first";
        EXPECT_GT(indexOf(&chip), indexOf(&card.commentFieldForTesting())) << "then the comment row";
    }
    card.followUpForTesting(1).onClick();
    EXPECT_EQ(got, juce::String("Change the key"));
    EXPECT_TRUE(synth::test::auditAccessibility(card).empty());
}

TEST(BuildStepsCard, FollowUpsWrapOntoASecondLineWhenNarrowAndCountInTheCardsHeight) {
    auto session = builtSession();
    BuildStepsCard wide(session, {});
    wide.setSize(600, wide.getRequiredHeight(600));
    EXPECT_EQ(wide.followUpForTesting(0).getY(), wide.followUpForTesting(2).getY()) << "one line when wide";
    auto session2 = builtSession();
    BuildStepsCard narrow(session2, {});
    narrow.setSize(200, narrow.getRequiredHeight(200));
    EXPECT_GT(narrow.followUpForTesting(2).getY(), narrow.followUpForTesting(0).getY()) << "wraps when narrow";
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(narrow.getLocalBounds().contains(narrow.followUpForTesting(i).getBounds()));
}
