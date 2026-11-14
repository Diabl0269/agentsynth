// CardBodyAdsrLooksTests.cpp
//
// An ADSR card's Sync (Time/Tempo) swaps its stage controls in place: the card keeps its size in both layouts
// (Shared swap cells, Separate looks), only the active look's controls show, no two controls overlap in either
// look, and the swap itself is the leaving controls shrinking, THEN the arriving ones growing, never at once
// (Source/UI/Graph/CardBody/CardBodySwapMotion.cpp, the plan's alternative group in CardBodyPlan.cpp and
// CardBodyLayout.cpp). Every flip goes through the real parameter listener, flushed as the message loop would.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "Modules/ADSRModule.h"
#include "Modules/Envelope/EnvelopeTempoSync.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardBody/DefaultLayouts/AdsrTimeTempo.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Graph/CardWidgets/NoteLengthFader.h"
#include "UI/Layout/ControlMotion.h"
#include "UI/Layout/ReducedMotion.h"

using namespace cardbody_test;
using synth::AdsrTimeTempo;
using synth::CardWidget;

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

// The jacks the card draws in the gutter, where they are (card pixels).
std::vector<std::pair<int, juce::Point<int>>> drawnJacks(CardCanvas& canvas, NodeID id) {
    std::vector<std::pair<int, juce::Point<int>>> jacks;
    auto* card = canvas.card(id);
    for (int index : card->drawnInputJackIndices())
        jacks.emplace_back(index, card->getPortCenter(index, true));
    return jacks;
}

// A jack drawn inside the envelope graph covers it: none may sit there in any look of either layout, and the
// Separate layout draws the very jacks the Shared one does (the stage CV jacks stay on their knobs in both looks).
TEST(AdsrLooks, NoCvJackIsDrawnOverTheEnvelopeAndSeparateDrawsTheJacksSharedDoes) {
    for (const bool tempo : {false, true}) {
        Rig shared(adsrLayout(AdsrTimeTempo::Shared));
        Rig separate(adsrLayout(AdsrTimeTempo::Separate));
        if (tempo) {
            flipSync(shared.canvas, shared.id, true);
            flipSync(separate.canvas, separate.id, true);
        }
        auto* envelope = separate.body().findView(synth::CardView::Envelope);
        ASSERT_NE(envelope, nullptr);
        ASSERT_TRUE(envelope->isVisible());
        for (const auto& [index, at] : drawnJacks(separate.canvas, separate.id))
            EXPECT_FALSE(envelope->getBounds().contains(at)) << "jack " << index << " at " << at.x << "," << at.y
                                                             << (tempo ? " in the Tempo look" : " in the Time look");
        EXPECT_EQ(separate.canvas.card(separate.id)->drawnInputJackIndices(),
                  shared.canvas.card(shared.id)->drawnInputJackIndices())
            << (tempo ? "Tempo" : "Time");
    }
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

// The Time look's four stage CV jacks stay on their knobs while the swap runs: an arriving knob is held hidden for the
// shrink, and its jack must not drop to the gutter, where it would sit over the envelope graph.
TEST(AdsrLooksMotion, NoCvJackIsDrawnOverTheEnvelopeAtAnyPointOfASwap) {
    for (const auto mode : {AdsrTimeTempo::Shared, AdsrTimeTempo::Separate}) {
        MotionRig rig(mode);
        auto* envelope = rig.body().findView(synth::CardView::Envelope);
        ASSERT_NE(envelope, nullptr);
        const auto jacks = rig.canvas.card(rig.id)->drawnInputJackIndices();
        const auto check = [&](const juce::String& when) {
            EXPECT_EQ(rig.canvas.card(rig.id)->drawnInputJackIndices(), jacks) << (int)mode << " " << when;
            for (const auto& [index, at] : drawnJacks(rig.canvas, rig.id))
                EXPECT_FALSE(envelope->getBounds().contains(at))
                    << (int)mode << " " << when << ": jack " << index << " at " << at.x << "," << at.y;
        };
        for (const bool tempo : {true, false, true, false}) {
            flipSync(rig.canvas, rig.id, tempo);
            for (const double ms : {0.0, 95.0, 189.0, 200.0, 300.0, 400.0}) {
                rig.body().stepSwapMotionForTest(ms);
                check((tempo ? "Tempo at " : "Time at ") + juce::String(ms) + " ms");
            }
        }
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

// The Tempo look's division faders (their own namespace: the helpers above share names).
namespace tempo_faders {
namespace {

constexpr const char* kDivisionIds[] = {"attackDiv", "holdDiv", "decayDiv", "releaseDiv"};
constexpr const char* kFamily[] = {"ADSR", "Amp Env", "Filter Env"};

synth::CardLayout layoutFor(const char* type, AdsrTimeTempo mode) {
    return synth::withAdsrTimeTempo(synth::DefaultCardLayouts::builtIn().find(type)->layout, mode);
}

const synth::CardParamItem* itemIn(const synth::CardLayout& layout, const juce::String& paramId) {
    for (const auto& section : layout.sections)
        for (const auto& item : section.items)
            if (const auto* param = std::get_if<synth::CardParamItem>(&item);
                param != nullptr && param->paramId == paramId)
                return param;
    return nullptr;
}

void setTempo(CardCanvas& canvas, NodeID id, bool tempo) {
    findParameterByID(canvas.processor(id), "tempoSync")->setValueNotifyingHost(tempo ? 1.0f : 0.0f);
    canvas.card(id)->getCardBody()->flushPendingConditionUpdate();
}

struct Drawn {
    synth::CardBodyItem::Kind kind;
    juce::Rectangle<int> bounds;
    bool operator==(const Drawn&) const = default;
};

// What the card draws now for `paramId`: the plan's kind and the widget's own bounds.
Drawn drawn(CardCanvas& canvas, NodeID id, const juce::String& paramId) {
    auto* body = canvas.card(id)->getCardBody();
    const auto& plan = body->getPlan();
    const auto& item = plan.items[(size_t)plan.findParam(paramId)];
    return {item.kind, item.widget->getBounds()};
}

struct Rig {
    CardCanvas canvas;
    NodeID id;
    Rig(const char* type, AdsrTimeTempo mode, bool tempo) {
        id = canvas.add(synth::AIStateMapper::createModule(type), 100, 100, layoutFor(type, mode));
        canvas.editor.updateComponents();
        setTempo(canvas, id, tempo);
    }
    synth::CardBody& body() { return *canvas.card(id)->getCardBody(); }
};

} // namespace

// The sibling-gap guard: every ADSR-family module, both Controls modes, Sync on Tempo: the four divisions draw as
// vertical faders of one size, and a Shared card's draw the same kind and size as a Separate card's.
TEST(AdsrTempoFaders, EveryAdsrFamilyModuleDrawsTheTempoLooksDivisionsAsTheSameFadersInBothControlsModes) {
    for (const char* type : kFamily) {
        Rig shared(type, AdsrTimeTempo::Shared, true);
        Rig separate(type, AdsrTimeTempo::Separate, true);
        for (const char* division : kDivisionIds) {
            SCOPED_TRACE(juce::String(type) + " " + division);
            const auto s = drawn(shared.canvas, shared.id, division);
            const auto p = drawn(separate.canvas, separate.id, division);
            EXPECT_EQ(s.kind, synth::CardBodyItem::Kind::FaderV) << "Shared";
            EXPECT_EQ(p.kind, synth::CardBodyItem::Kind::FaderV) << "Separate";
            EXPECT_EQ(s.bounds.getWidth(), p.bounds.getWidth());
            EXPECT_EQ(s.bounds.getHeight(), p.bounds.getHeight());
            EXPECT_TRUE(shared.body().findWidget(division)->isVisible());
            EXPECT_TRUE(separate.body().findWidget(division)->isVisible());
        }
        const auto s = drawn(shared.canvas, shared.id, "sustain");
        const auto p = drawn(separate.canvas, separate.id, "sustain");
        EXPECT_EQ(s.kind, p.kind);
        EXPECT_EQ(s.bounds.getWidth(), p.bounds.getWidth());
        EXPECT_EQ(s.bounds.getHeight(), p.bounds.getHeight());
    }
}

TEST(AdsrTempoFaders, TheCardKeepsItsHeightWhenSyncFlipsInBothControlsModes) {
    for (const char* type : kFamily)
        for (const auto mode : {AdsrTimeTempo::Shared, AdsrTimeTempo::Separate}) {
            Rig rig(type, mode, false);
            const auto bounds = rig.canvas.card(rig.id)->getBounds();
            setTempo(rig.canvas, rig.id, true);
            EXPECT_EQ(rig.canvas.card(rig.id)->getBounds(), bounds) << type << " " << (int)mode;
        }
}

TEST(AdsrTempoFaders, TheSharedDefaultSetsEveryDivisionToAFaderAndConversionsKeepWidgets) {
    for (const char* type : kFamily) {
        const auto shared = layoutFor(type, AdsrTimeTempo::Shared);
        for (const char* division : kDivisionIds) {
            ASSERT_NE(itemIn(shared, division), nullptr) << type << " " << division;
            EXPECT_EQ(itemIn(shared, division)->widget, CardWidget::FaderV) << type << " " << division;
        }
        // Shared -> Separate -> Shared keeps each stage's own widget (here Atk's time as a long fader).
        auto edited = shared;
        for (auto& section : edited.sections)
            for (auto& item : section.items)
                if (auto* param = std::get_if<synth::CardParamItem>(&item);
                    param != nullptr && param->paramId == "attack")
                    param->widget = CardWidget::FaderVLong;
        for (auto& section : edited.sections)
            for (auto& item : section.items)
                if (auto* param = std::get_if<synth::CardParamItem>(&item);
                    param != nullptr && param->paramId == "attackDiv")
                    param->widget = CardWidget::FaderVLong;
        const auto trip =
            synth::withAdsrTimeTempo(synth::withAdsrTimeTempo(edited, AdsrTimeTempo::Separate), AdsrTimeTempo::Shared);
        EXPECT_EQ(itemIn(trip, "attack")->widget, CardWidget::FaderVLong) << type;
        EXPECT_EQ(itemIn(trip, "attackDiv")->widget, CardWidget::FaderVLong) << type;
        EXPECT_EQ(itemIn(trip, "holdDiv")->widget, CardWidget::FaderV) << type << ": not turned back to Auto";
        const auto separate = synth::withAdsrTimeTempo(shared, AdsrTimeTempo::Separate);
        EXPECT_EQ(itemIn(separate, "holdDiv")->widget, CardWidget::FaderV) << type;
    }
}

// A layout saved before the default set the widget: its divisions are Auto in a swap pair beside a fader, and the
// card still draws faders (decided by the swap-pair rule, not by what the save says).
TEST(AdsrTempoFaders, ASavedSharedLayoutWithAutoDivisionsStillDrawsFaders) {
    auto old = layoutFor("ADSR", AdsrTimeTempo::Shared);
    for (auto& section : old.sections)
        for (auto& item : section.items)
            if (auto* param = std::get_if<synth::CardParamItem>(&item);
                param != nullptr && param->paramId.endsWith("Div"))
                param->widget = CardWidget::Auto;
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<ADSRModule>(), 100, 100, old);
    canvas.editor.updateComponents();
    setTempo(canvas, id, true);
    for (const char* division : kDivisionIds)
        EXPECT_EQ(drawn(canvas, id, division).kind, synth::CardBodyItem::Kind::FaderV) << division;
}

// A division set to Menu stays a combo in the Shared look (Show as keeps both), and a pair with no fader beside it
// (the Delay's knob and division) is not turned into a fader.
TEST(AdsrTempoFaders, AMenuDivisionStaysAMenuAndAKnobPairKeepsItsMenu) {
    auto layout = layoutFor("ADSR", AdsrTimeTempo::Shared);
    for (auto& section : layout.sections)
        for (auto& item : section.items)
            if (auto* param = std::get_if<synth::CardParamItem>(&item); param != nullptr && param->paramId == "holdDiv")
                param->widget = CardWidget::Choice;
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<ADSRModule>(), 100, 100, layout);
    canvas.editor.updateComponents();
    setTempo(canvas, id, true);
    EXPECT_EQ(drawn(canvas, id, "holdDiv").kind, synth::CardBodyItem::Kind::Choice);
    EXPECT_EQ(drawn(canvas, id, "attackDiv").kind, synth::CardBodyItem::Kind::FaderV);
}

// The picker: a vertical division fader steps through the eight lengths and an S | T | D picker under it picks the
// feel; both write the one division parameter (no new parameter), each as its own change.
TEST(AdsrTempoFaders, TheFaderStepsLengthsAndThePickerPicksTheFeelBothWritingTheDivision) {
    for (const auto mode : {AdsrTimeTempo::Shared, AdsrTimeTempo::Separate}) {
        Rig rig("ADSR", mode, true);
        auto* fader = dynamic_cast<synth::ui::NoteLengthFader*>(rig.body().findWidget("holdDiv"));
        ASSERT_NE(fader, nullptr) << (int)mode;
        auto* param =
            dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(rig.canvas.processor(rig.id), "holdDiv"));
        ASSERT_NE(param, nullptr);
        const auto indexOf = [&](const char* name) { return synth::envelopeNoteDivisionIndex(name); };

        param->setValueNotifyingHost(param->convertTo0to1((float)indexOf("1/4")));
        EXPECT_EQ(fader->getPicker().getSelectedIndex(), 0) << "straight";
        EXPECT_EQ(fader->getPicker().getNumSegments(), 3);
        // Nothing (the slider's value box, say) may sit on top of the picker.
        auto* topmost = fader->getComponentAt(fader->getPicker().getBounds().getCentre());
        EXPECT_TRUE(topmost == &fader->getPicker() || fader->getPicker().isParentOf(topmost)) << (int)mode;

        fader->getPicker().setSelectedIndex(1, juce::sendNotificationSync); // T
        EXPECT_EQ(param->getCurrentChoiceName(), "1/4T") << "the picker keeps the length";
        fader->setValue((double)indexOf("1/8T"), juce::sendNotificationSync); // move the fader to the 1/8 step
        EXPECT_EQ(param->getCurrentChoiceName(), "1/8T") << "the fader keeps the feel";
        fader->getPicker().setSelectedIndex(2, juce::sendNotificationSync); // D
        EXPECT_EQ(param->getCurrentChoiceName(), "1/8D");
        EXPECT_EQ(fader->getMinimum(), 0.0);
        // A key step past the end of the lengths stays in the feel.
        param->setValueNotifyingHost(param->convertTo0to1((float)indexOf("1/1D")));
        fader->setValue((double)indexOf("1/1D") + 1.0, juce::sendNotificationSync);
        EXPECT_EQ(param->getCurrentChoiceName(), "1/1D");
        // Automation or undo moves both parts.
        param->setValueNotifyingHost(param->convertTo0to1((float)indexOf("1/16T")));
        fader->setValue((double)indexOf("1/16T"), juce::dontSendNotification); // what the card's reflect does
        EXPECT_EQ(fader->getPicker().getSelectedIndex(), 1);
        EXPECT_EQ((int)fader->getValue() % 8, indexOf("1/16T") % 8);
    }
}

} // namespace tempo_faders
