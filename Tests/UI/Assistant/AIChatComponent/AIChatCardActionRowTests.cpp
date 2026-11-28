// AIChatCardActionRowTests.cpp
// The one placement rule for every AI card's action row (CardActionRow.h): quiet link or stop button at the far left,
// an outlined second action left of the primary, the primary filled in the right corner, one row, nothing overlapping.
// Guards the kind "sibling-gap" for the edit-plan, song-plan and build-steps cards; and the plan card's selectable text
// (copy, not a Tab stop, no outline of its own).

#include "../../Accessibility/TabOrderHelpers.h"
#include "AIChatComponentTestFixture.h"
#include "PlanRig.h"
#include "UI/Assistant/AIChatComponent/BuildStepsCard.h"
#include "UI/Assistant/AIChatComponent/CardActionRow.h"
#include "UI/Assistant/AIChatComponent/SelectableChatText.h"
#include "UI/Assistant/AIChatComponent/SongPlanCard.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Layout/StopButton.h"

using namespace plan_rig;

namespace {

constexpr const char* kRowPlan =
    R"({"plan":{"title":"Dark drum and bass","bpm":174,"key":"A minor","special":"",
    "sections":[{"name":"Intro","bars":16,"energy":1,"chords":"Am","description":"pad"},
                {"name":"Drop","bars":32,"energy":5,"chords":"Am F","description":"break"}],
    "tracks":[{"name":"Drum kit","role":"drums","instrument":"Drum Kit","sound":"x","sections":["Intro"]}],
    "lanes":[]},
    "price":{"units":5,"estimatedUsd":0.02},
    "steps":[{"id":"track:Drum kit","kind":"track","label":"Drum kit sound"}],"token":"tok"})";

synth::SongSession rowSession() {
    synth::SongSession session;
    juce::String error;
    EXPECT_TRUE(synth::songsteps::parsePlanReply(kRowPlan, session.reply, error)) << error;
    session.adoptPlan(session.reply, true);
    session.requestText = "dark drum and bass";
    return session;
}

// The card's inner box: what a row may fill.
juce::Rectangle<int> innerOf(juce::Component& card, int pad) { return card.getLocalBounds().reduced(pad); }

void expectOneRow(juce::Component& card, juce::Component& link, juce::Component* middle, juce::Component* primary,
                  const juce::String& label, int pad = 8) {
    SCOPED_TRACE(label.toStdString());
    EXPECT_TRUE(card.getLocalBounds().contains(link.getBounds()));
    EXPECT_EQ(link.getX(), innerOf(card, pad).getX()) << "the quiet link is at the far left";
    std::vector<juce::Component*> parts{&link};
    if (middle != nullptr)
        parts.push_back(middle);
    if (primary != nullptr)
        parts.push_back(primary);
    for (size_t i = 0; i < parts.size(); ++i) {
        EXPECT_TRUE(card.getLocalBounds().contains(parts[i]->getBounds())) << "part " << i << " inside the card";
        EXPECT_EQ(parts[i]->getY(), link.getY()) << "part " << i << " on the same row";
        EXPECT_EQ(parts[i]->getHeight(), link.getHeight());
        if (i > 0) {
            EXPECT_GT(parts[i]->getX(), parts[i - 1]->getX()) << "quiet to loud, left to right";
            EXPECT_FALSE(parts[i]->getBounds().intersects(parts[i - 1]->getBounds()))
                << "no overlap: " << parts[i - 1]->getBounds().toString() << " vs " << parts[i]->getBounds().toString()
                << " card " << card.getWidth();
        }
    }
    if (primary != nullptr)
        EXPECT_EQ(primary->getRight(), innerOf(card, pad).getRight()) << "the primary sits in the right corner";
}

} // namespace

TEST_F(AIChatComponentTest, EditPlanCardActionsShareOneRowAtNarrowAndWideChats) {
    for (const int chatWidth : {280, 340}) {
        for (const int variant : {0, 1, 2}) { // save enabled, save off, refused (no Apply, no Save)
            PlanRig rig;
            rig.chat->setSize(chatWidth, 700);
            rig.provider->answer = variant == 0 ? kPatchOnly : kPlan;
            if (variant == 2)
                rig.provider->answer = R"({"mode": "merge", "nodes": [], "connections": [],
                    "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Theremin"}]})";
            rig.send("make something");
            rig.chat->setSize(chatWidth, 700);
            const juce::String label = "chat " + juce::String(chatWidth) + " variant " + juce::String(variant);
            const auto cards = rig.findTitled("Edit plan");
            const auto links = rig.findTitled("Show details");
            ASSERT_FALSE(cards.empty()) << label;
            ASSERT_FALSE(links.empty()) << label;
            auto* apply = rig.applyButton();
            auto* save = findButtonTitled(rig, "Save patch as preset");
            EXPECT_EQ(apply == nullptr, variant == 2) << label;
            EXPECT_EQ(save == nullptr, variant == 2) << label;
            expectOneRow(*cards.front(), *links.front(), save, apply, label);
        }
    }
}

TEST_F(AIChatComponentTest, EditPlanTabOrderIsDetailsThenSaveThenApply) {
    PlanRig rig;
    rig.provider->answer = kPatchOnly;
    rig.send("make me a simple oscillator patch");
    const auto names = synth::test::walkTabOrder(*rig.chat).names();
    const int details = names.indexOf("Show details");
    const int save = names.indexOf("Save patch as preset");
    const int apply = names.indexOf("Apply edit plan");
    EXPECT_GE(details, 0);
    EXPECT_LT(details, save);
    EXPECT_LT(save, apply);
    EXPECT_EQ(names.indexOf("This answer was helpful"), apply + 1) << "thumbs follow Apply";
    for (const auto* id : {"planHeaderText", "planPreviewText"})
        for (const auto& n : names)
            EXPECT_NE(n, juce::String(id));
}

TEST_F(AIChatComponentTest, EverySongCardKeepsItsQuietLinkLeftmostAndPrimaryRightmost) {
    for (const int width : {280, 340}) {
        auto session = rowSession();
        synth::SongPlanCard plan(session, {});
        plan.setSize(width, plan.getRequiredHeight(width));
        auto& cancel = plan.cancelLinkForTesting();
        juce::Component* build = nullptr;
        for (auto* c : plan.getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c);
                b != nullptr && b->getButtonText().startsWith("Build song"))
                build = b;
        ASSERT_NE(build, nullptr);
        expectOneRow(plan, cancel, nullptr, build, "song plan " + juce::String(width), synth::SongCardBase::kPadding);
        EXPECT_EQ(build->findColour(juce::TextButton::buttonColourId), synth::theme::themeOf(plan).colors.accent)
            << "the primary is filled with the accent";

        auto running = rowSession();
        running.phase = synth::SongSession::Phase::building;
        running.buildStartMs = juce::Time::getMillisecondCounter();
        synth::BuildStepsCard card(running, {});
        card.setSize(width, card.getRequiredHeight(width));
        card.setSize(width, card.getRequiredHeight(width));
        auto& link = card.cancelButtonForTesting();
        EXPECT_EQ(link.getX(), synth::SongCardBase::kPadding) << "build card: Cancel is the far-left link";
        EXPECT_EQ(link.getHeight(), synth::ui::cardactions::kButtonHeight);
        EXPECT_TRUE(card.getLocalBounds().contains(link.getBounds()));
    }
}

// Every card that can be cancelled draws Cancel as the same outlined stop button, 24 px tall in the 28 px row, a Tab
// stop that comes before the primary; the tone reads on the panel in all four themes (the kind: action-drift).
TEST_F(AIChatComponentTest, CancelOnEverySongCardIsTheSameOutlinedStopButton) {
    auto session = rowSession();
    synth::SongPlanCard plan(session, {});
    plan.setSize(340, plan.getRequiredHeight(340));
    auto running = rowSession();
    running.phase = synth::SongSession::Phase::building;
    running.buildStartMs = juce::Time::getMillisecondCounter();
    synth::BuildStepsCard build(running, {});
    build.setSize(340, build.getRequiredHeight(340));
    build.setSize(340, build.getRequiredHeight(340));

    struct Entry {
        juce::Component* card;
        juce::Component* cancel;
        const char* name;
    };
    for (const auto& e : {Entry{&plan, &plan.cancelLinkForTesting(), "song plan"},
                          Entry{&build, &build.cancelButtonForTesting(), "build steps"}}) {
        SCOPED_TRACE(e.name);
        auto* stop = dynamic_cast<synth::ui::StopButton*>(e.cancel);
        ASSERT_NE(stop, nullptr) << "Cancel is a StopButton, not a text link";
        EXPECT_EQ(stop->getHeight(), synth::ui::cardactions::kButtonHeight);
        EXPECT_EQ(stop->getWidth(), stop->getIdealWidth());
        EXPECT_TRUE(stop->getWantsKeyboardFocus());
        const auto names = synth::test::walkTabOrder(*e.card).names();
        EXPECT_TRUE(names.contains(stop->getTitle())) << "Tab reaches Cancel";
        if (e.card == &plan) {
            int buildAt = -1;
            for (int i = 0; i < names.size(); ++i)
                if (names[i].startsWith("Build song"))
                    buildAt = i;
            ASSERT_GE(buildAt, 0);
            EXPECT_LT(names.indexOf(stop->getTitle()), buildAt) << "Cancel comes before the primary";
        }
    }
}

TEST_F(AIChatComponentTest, PlanCardTextIsSelectableCopiesAndIsNotATabStop) {
    PlanRig rig;
    rig.provider->answer = kPatchOnly;
    rig.send("make me a simple oscillator patch");
    juce::Component* card = rig.findTitled("Edit plan").front();
    for (const auto* id : {"planHeaderText", "planPreviewText"}) {
        synth::SelectableChatText* text = nullptr;
        for (auto* c : card->getChildren())
            if (c->getComponentID() == id)
                text = dynamic_cast<synth::SelectableChatText*>(c);
        ASSERT_NE(text, nullptr) << id;
        EXPECT_FALSE(text->getWantsKeyboardFocus()) << "not a Tab stop";
        text->selectAll();
        EXPECT_EQ(text->getHighlightedText(), text->getText()) << id << ": the whole text selects";
        if (clipboardRoundTrips()) { // a headless Linux runner has no system clipboard
            juce::SystemClipboard::copyTextToClipboard("");
            text->keyPressed(juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0));
            EXPECT_EQ(juce::SystemClipboard::getTextFromClipboard(), text->getText()) << id;
        }
        EXPECT_NE(synth::ui::neighbourStop(*card, text, +1), text);
        for (auto* stop : synth::ui::focusableDescendants(*card))
            EXPECT_NE(stop, text) << "skipped by Up/Down stepping";
    }
}

TEST_F(AIChatComponentTest, SelectableChatTextCountsOnlyKeyFocusAsKeyboardFocus) {
    synth::SelectableChatText text;
    EXPECT_FALSE(text.focusedByKeyboard());
    text.focusGained(juce::Component::focusChangedByMouseClick);
    EXPECT_FALSE(text.focusedByKeyboard()) << "a click to select is no reason for a ring";
    text.focusLost(juce::Component::focusChangedByMouseClick);
    text.focusGained(juce::Component::focusChangedByTabKey);
    EXPECT_TRUE(text.focusedByKeyboard());
    text.focusLost(juce::Component::focusChangedByTabKey);
    EXPECT_FALSE(text.focusedByKeyboard());
}

// Bug kind sibling-gap: every rating row sits on the same side, at the card content's right edge, with the same gap.
TEST_F(AIChatComponentTest, TheRatingRowIsRightAlignedOnTheSongCardAndTheEditPlanCard) {
    constexpr int kWidth = 340;
    auto session = rowSession();
    session.phase = synth::SongSession::Phase::built;
    synth::BuildStepsCard song(session, {});
    song.setSize(kWidth, song.getRequiredHeight(kWidth));
    auto& songUp = song.thumbForTesting(true);
    auto& songDown = song.thumbForTesting(false);
    EXPECT_EQ(songDown.getRight(), innerOf(song, synth::SongCardBase::kPadding).getRight());
    EXPECT_EQ(songUp.getY(), songDown.getY());
    EXPECT_EQ(songDown.getX() - songUp.getRight(), synth::ThumbButton::kGap);

    PlanRig rig;
    rig.chat->setSize(kWidth, 700);
    rig.provider->answer = kPatchOnly;
    rig.send("make something");
    rig.chat->setSize(kWidth, 700);
    auto* card = rig.findTitled("Edit plan").front();
    auto* up = rig.findTitled("This answer was helpful").front();
    auto* down = rig.findTitled("This answer missed the mark").front();
    EXPECT_EQ(down->getRight(), innerOf(*card, 8).getRight()) << "the edit-plan card agrees with the song card";
    EXPECT_EQ(up->getY(), down->getY());
    EXPECT_EQ(down->getX() - up->getRight(), synth::ThumbButton::kGap);
}
