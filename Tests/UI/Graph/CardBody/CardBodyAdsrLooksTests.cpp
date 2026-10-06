// CardBodyAdsrLooksTests.cpp
//
// An ADSR card's Sync (Time/Tempo) swaps its stage controls in place: the card keeps its size in both layouts
// (Shared swap cells, Separate looks), only the active look's controls show, no two controls overlap in either
// look, and the swap itself is the leaving controls shrinking, THEN the arriving ones growing, never at once
// (Source/UI/Graph/CardBody/CardBodySwapMotion.cpp, the plan's alternative group in CardBodyPlan.cpp and
// CardBodyLayout.cpp). Every flip goes through the real parameter listener, flushed as the message loop would.

#include "CardBodyTestHelpers.h"
#include "Modules/ADSRModule.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardBody/DefaultLayouts/AdsrTimeTempo.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Layout/ControlMotion.h"
#include "UI/Layout/ReducedMotion.h"

using namespace cardbody_test;
using synth::AdsrTimeTempo;

namespace {

constexpr const char* kTimeIds[] = {"attack", "hold", "decay", "release"};
constexpr const char* kDivisionIds[] = {"attackDiv", "holdDiv", "decayDiv", "releaseDiv"};

synth::CardLayout adsrLayout(AdsrTimeTempo mode) {
    return synth::withAdsrTimeTempo(synth::DefaultCardLayouts::builtIn().find("ADSR")->layout, mode);
}

// The old Separate form: both groups always shown, each item dimmed while the other mode is on.
synth::CardLayout firstSeparateForm() {
    auto layout = adsrLayout(AdsrTimeTempo::Separate);
    for (auto& section : layout.sections) {
        if (section.id != "stages-time" && section.id != "stages-tempo")
            continue;
        const bool tempo = section.id == "stages-tempo";
        section.visibleWhen = std::nullopt;
        std::erase_if(section.items, [&](const synth::CardItem& item) {
            return tempo && std::get<synth::CardParamItem>(item).paramId == "sustain";
        });
        for (auto& item : section.items)
            std::get<synth::CardParamItem>(item).when =
                synth::CardCondition{"tempoSync", {tempo ? "true" : "false"}, synth::CardConditionEffect::Dim};
    }
    return layout;
}

void flipSync(CardCanvas& canvas, NodeID id, bool tempo) {
    findParameterByID(canvas.processor(id), "tempoSync")->setValueNotifyingHost(tempo ? 1.0f : 0.0f);
    canvas.card(id)->getCardBody()->flushPendingConditionUpdate();
}

juce::Component* widgetOf(CardCanvas& canvas, NodeID id, const juce::String& paramId) {
    return canvas.card(id)->getCardBody()->findWidget(paramId);
}

bool shows(CardCanvas& canvas, NodeID id, const juce::String& paramId) {
    return widgetOf(canvas, id, paramId)->isVisible();
}

// The cells (widget and caption) of every control the card draws now, as a person sees them: a control with no
// opacity is not there, a scaled one is its transformed size.
std::vector<std::pair<juce::String, juce::Rectangle<float>>> drawnCells(CardCanvas& canvas, NodeID id,
                                                                        bool transformed = true) {
    std::vector<std::pair<juce::String, juce::Rectangle<float>>> cells;
    for (const auto& item : canvas.card(id)->getCardBody()->getPlan().items) {
        if (item.widget == nullptr || item.kind == synth::CardBodyItem::Kind::View || !item.widget->isVisible() ||
            item.widget->getAlpha() <= 0.0f)
            continue;
        auto rect = (transformed ? item.widget->getBoundsInParent() : item.widget->getBounds()).toFloat();
        if (item.label != nullptr && item.label->isVisible())
            rect = rect.getUnion((transformed ? item.label->getBoundsInParent() : item.label->getBounds()).toFloat());
        cells.emplace_back(item.param->paramID, rect);
    }
    return cells;
}

// The first pair of drawn cells that overlap (shrunk a little: abutting is not overlapping), or an empty string.
// Cells are compared where the layout puts them (the grow's 8% overshoot is a paint-only bounce of a few pixels
// into the card's own gaps); a leaving picture is compared with the arriving controls as they are drawn.
juce::String firstOverlap(CardCanvas& canvas, NodeID id) {
    const auto cells = drawnCells(canvas, id, false);
    const auto drawn = drawnCells(canvas, id, true);
    auto ghosts = canvas.card(id)->getCardBody()->swapGhostRectsForTest();
    for (size_t i = 0; i < cells.size(); ++i) {
        for (size_t j = i + 1; j < cells.size(); ++j)
            if (cells[i].second.reduced(1.0f).intersects(cells[j].second.reduced(1.0f)))
                return cells[i].first + " and " + cells[j].first;
        for (const auto& ghost : ghosts)
            if (drawn[i].second.reduced(1.0f).intersects(ghost.reduced(1.0f)))
                return cells[i].first + " and a leaving control";
    }
    return {};
}

struct Rig {
    CardCanvas canvas;
    NodeID id;
    explicit Rig(std::optional<synth::CardLayout> layout = std::nullopt) {
        id = canvas.add(std::make_unique<ADSRModule>(), 100, 100, std::move(layout));
        canvas.editor.updateComponents();
    }
    synth::CardBody& body() { return *canvas.card(id)->getCardBody(); }
};

} // namespace

TEST(AdsrLooks, TheCardKeepsItsSizeWhenSyncFlipsWhetherTheStagesAreSharedOrSeparate) {
    for (const auto mode : {AdsrTimeTempo::Shared, AdsrTimeTempo::Separate}) {
        Rig rig(adsrLayout(mode));
        const auto bounds = rig.canvas.card(rig.id)->getBounds();
        flipSync(rig.canvas, rig.id, true);
        EXPECT_EQ(rig.canvas.card(rig.id)->getBounds(), bounds) << (int)mode << ": Tempo";
        flipSync(rig.canvas, rig.id, false);
        EXPECT_EQ(rig.canvas.card(rig.id)->getBounds(), bounds) << (int)mode << ": back to Time";
    }
}

TEST(AdsrLooks, SeparateAndSharedCardsAreTheSameSizeWhicheverLookIsOn) {
    Rig shared(adsrLayout(AdsrTimeTempo::Shared));
    Rig separate(adsrLayout(AdsrTimeTempo::Separate));
    EXPECT_EQ(separate.canvas.card(separate.id)->getHeight(), shared.canvas.card(shared.id)->getHeight());
    EXPECT_EQ(separate.canvas.card(separate.id)->getWidth(), shared.canvas.card(shared.id)->getWidth());
}

TEST(AdsrLooks, InSeparateOnlyTheActiveLooksControlsShow) {
    Rig rig(adsrLayout(AdsrTimeTempo::Separate));
    for (const auto* id : kTimeIds)
        EXPECT_TRUE(shows(rig.canvas, rig.id, id)) << id << " in the Time look";
    for (const auto* id : kDivisionIds)
        EXPECT_FALSE(shows(rig.canvas, rig.id, id)) << id << " is not in the Time look";
    EXPECT_TRUE(shows(rig.canvas, rig.id, "sustain"));

    flipSync(rig.canvas, rig.id, true);
    for (const auto* id : kTimeIds)
        EXPECT_FALSE(shows(rig.canvas, rig.id, id)) << id << " is not in the Tempo look";
    for (const auto* id : kDivisionIds)
        EXPECT_TRUE(shows(rig.canvas, rig.id, id)) << id << " in the Tempo look";
    EXPECT_TRUE(shows(rig.canvas, rig.id, "sustain")) << "Sustain has no division: it stands in both looks";
    EXPECT_TRUE(shows(rig.canvas, rig.id, "tempoSync"));
}

TEST(AdsrLooks, TheTempoLooksDivisionsAreFadersByDefault) {
    Rig rig(adsrLayout(AdsrTimeTempo::Separate));
    for (const auto* id : kDivisionIds)
        EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(widgetOf(rig.canvas, rig.id, id)), nullptr) << id;
}

TEST(AdsrLooks, NoControlOverlapsAnotherInEitherLookOfEitherLayout) {
    for (const auto mode : {AdsrTimeTempo::Shared, AdsrTimeTempo::Separate}) {
        Rig rig(adsrLayout(mode));
        EXPECT_EQ(firstOverlap(rig.canvas, rig.id), "") << (int)mode << ": Time look";
        EXPECT_GE(drawnCells(rig.canvas, rig.id).size(), 6u);
        flipSync(rig.canvas, rig.id, true);
        EXPECT_EQ(firstOverlap(rig.canvas, rig.id), "") << (int)mode << ": Tempo look";
    }
}

TEST(AdsrLooks, ASharedControlHasItsOwnPlaceInEachLook) {
    auto layout = adsrLayout(AdsrTimeTempo::Separate);
    for (auto& section : layout.sections)
        for (auto& item : section.items)
            if (auto* param = std::get_if<synth::CardParamItem>(&item); param != nullptr && param->paramId == "sustain")
                param->at = section.id == "stages-tempo" ? juce::Point<int>(0, 0) : juce::Point<int>(120, 0);
    Rig rig(layout);
    const int timeX = widgetOf(rig.canvas, rig.id, "sustain")->getX();
    flipSync(rig.canvas, rig.id, true);
    const int tempoX = widgetOf(rig.canvas, rig.id, "sustain")->getX();
    EXPECT_LT(tempoX, timeX) << "the Tempo look puts Sustain at its own position";
    flipSync(rig.canvas, rig.id, false);
    EXPECT_EQ(widgetOf(rig.canvas, rig.id, "sustain")->getX(), timeX);
}

TEST(AdsrLooks, ACardSavedWithTheOldSeparateFormShowsOnlyTheActiveLookAndKeepsItsSize) {
    Rig rig(firstSeparateForm());
    const auto bounds = rig.canvas.card(rig.id)->getBounds();
    for (const auto* id : kTimeIds)
        EXPECT_TRUE(shows(rig.canvas, rig.id, id)) << id;
    for (const auto* id : kDivisionIds)
        EXPECT_FALSE(shows(rig.canvas, rig.id, id)) << id;
    flipSync(rig.canvas, rig.id, true);
    for (const auto* id : kDivisionIds)
        EXPECT_TRUE(shows(rig.canvas, rig.id, id)) << id;
    EXPECT_TRUE(shows(rig.canvas, rig.id, "sustain")) << "reachable in the Tempo look";
    EXPECT_EQ(rig.canvas.card(rig.id)->getBounds(), bounds);
    EXPECT_EQ(firstOverlap(rig.canvas, rig.id), "");
}

// ---- The motion ---------------------------------------------------------------------------------------

namespace {

/** Full motion, and the card animating as if it were on screen. */
struct MotionRig : Rig {
    explicit MotionRig(AdsrTimeTempo mode)
        : Rig(adsrLayout(mode)) {
        synth::ui::setReducedMotionForTest(false);
        body().setForceAnimateForTest(true);
    }
    ~MotionRig() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

} // namespace

TEST(AdsrLooksMotion, TheLeavingControlsShrinkThenTheArrivingOnesGrowNeverTogether) {
    for (const auto mode : {AdsrTimeTempo::Shared, AdsrTimeTempo::Separate}) {
        MotionRig rig(mode);
        const auto bounds = rig.canvas.card(rig.id)->getBounds();
        flipSync(rig.canvas, rig.id, true);
        ASSERT_TRUE(rig.body().isSwapMotionRunning()) << (int)mode;

        const auto stepAndRead = [&](double ms) {
            rig.body().stepSwapMotionForTest(ms);
            EXPECT_EQ(firstOverlap(rig.canvas, rig.id), "") << (int)mode << " at " << ms << " ms";
            EXPECT_EQ(rig.canvas.card(rig.id)->getBounds(), bounds) << "the card keeps its size at " << ms << " ms";
            return std::make_pair(rig.body().swapGhostRectsForTest(), drawnCells(rig.canvas, rig.id));
        };
        const auto arrivingShown = [&](const auto& cells) {
            return std::any_of(cells.begin(), cells.end(), [](const auto& cell) { return cell.first == "attackDiv"; });
        };

        // The first 190 ms: the Time look's controls are pictures shrinking, none of the Tempo look is on yet.
        const auto [startGhosts, startCells] = stepAndRead(0.0);
        EXPECT_FALSE(startGhosts.empty()) << (int)mode;
        EXPECT_FALSE(arrivingShown(startCells));
        float widest = 0.0f;
        for (const auto& ghost : startGhosts)
            widest = std::max(widest, ghost.getHeight());
        const auto [midGhosts, midCells] = stepAndRead(95.0);
        EXPECT_FALSE(arrivingShown(midCells));
        float midWidest = 0.0f;
        for (const auto& ghost : midGhosts)
            midWidest = std::max(midWidest, ghost.getHeight());
        EXPECT_LT(midWidest, widest) << "shrinking";
        const auto [lateGhosts, lateCells] = stepAndRead(189.0);
        EXPECT_FALSE(arrivingShown(lateCells)) << "still the leaving phase one millisecond before it ends";

        // From 190 ms: the pictures are gone and the Tempo look's controls grow.
        const auto [growGhosts, growCells] = stepAndRead(200.0);
        EXPECT_TRUE(growGhosts.empty()) << (int)mode << ": nothing shrinking once the arrivals grow";
        EXPECT_TRUE(arrivingShown(growCells));

        float tallest = 0.0f;
        for (double ms = 190.0; ms < 390.0; ms += 5.0) {
            rig.body().stepSwapMotionForTest(ms);
            tallest =
                std::max(tallest, (float)widgetOf(rig.canvas, rig.id, "attackDiv")->getBoundsInParent().getHeight());
            EXPECT_EQ(firstOverlap(rig.canvas, rig.id), "") << (int)mode << " at " << ms << " ms";
        }
        const float natural = (float)widgetOf(rig.canvas, rig.id, "attackDiv")->getHeight();
        EXPECT_GT(tallest, natural * 1.05f) << "grows past its size";
        EXPECT_LT(tallest, natural * 1.1f) << "by about 8%";

        rig.body().stepSwapMotionForTest(400.0);
        EXPECT_FALSE(rig.body().isSwapMotionRunning());
        for (const auto* id : kDivisionIds) {
            auto* widget = widgetOf(rig.canvas, rig.id, id);
            EXPECT_TRUE(widget->isVisible()) << id;
            EXPECT_EQ(widget->getAlpha(), 1.0f) << id;
            EXPECT_TRUE(widget->getTransform().isIdentity()) << id;
        }
        for (const auto* id : kTimeIds)
            EXPECT_FALSE(shows(rig.canvas, rig.id, id)) << id;
    }
}

TEST(AdsrLooksMotion, ASecondFlipMidSwapLandsTheFirstAndStartsTheNext) {
    MotionRig rig(AdsrTimeTempo::Separate);
    flipSync(rig.canvas, rig.id, true);
    rig.body().stepSwapMotionForTest(100.0);
    flipSync(rig.canvas, rig.id, false);
    for (const auto* id : kDivisionIds)
        EXPECT_FALSE(shows(rig.canvas, rig.id, id)) << id;
    rig.body().stepSwapMotionForTest(500.0);
    for (const auto* id : kTimeIds) {
        EXPECT_TRUE(shows(rig.canvas, rig.id, id)) << id;
        EXPECT_TRUE(widgetOf(rig.canvas, rig.id, id)->getTransform().isIdentity()) << id;
    }
}

TEST(AdsrLooksMotion, UnderReduceMotionTheSwapIsInstant) {
    MotionRig rig(AdsrTimeTempo::Separate);
    synth::ui::setReducedMotionForTest(true);
    flipSync(rig.canvas, rig.id, true);
    EXPECT_FALSE(rig.body().isSwapMotionRunning());
    EXPECT_TRUE(rig.body().swapGhostRectsForTest().empty());
    for (const auto* id : kDivisionIds) {
        EXPECT_TRUE(shows(rig.canvas, rig.id, id)) << id;
        EXPECT_EQ(widgetOf(rig.canvas, rig.id, id)->getAlpha(), 1.0f) << id;
    }
}

TEST(AdsrLooksMotion, ACardThatIsNotOnScreenSwapsAtOnce) {
    Rig rig(adsrLayout(AdsrTimeTempo::Separate));
    synth::ui::setReducedMotionForTest(false);
    flipSync(rig.canvas, rig.id, true);
    synth::ui::setReducedMotionForTest(std::nullopt);
    EXPECT_FALSE(rig.body().isSwapMotionRunning());
    EXPECT_TRUE(shows(rig.canvas, rig.id, "attackDiv"));
}

TEST(AdsrLooksMotion, TheSwapPhasesAreTheDocumentedLengths) {
    EXPECT_DOUBLE_EQ(synth::ui::control_motion::kSwapShrinkMs, 190.0);
    EXPECT_NEAR(synth::ui::control_motion::growScale(0.7f), 1.08f, 0.04f);
}
