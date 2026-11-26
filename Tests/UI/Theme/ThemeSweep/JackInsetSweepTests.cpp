// JackInsetSweepTests.cpp -- the guard for the pair-asymmetry bug kind on a card's two jack edges: for every module
// type in every theme (at every Depth level), an input jack on the left and an output jack on the right sit the same
// distance from their edge, in the coordinates the card reports AND in the ink it paints. The ink half is the one that
// caught the real cause: the Rich rim light drew a second line just inside the left edge only, so the left jacks read
// closer to the edge than the right ones although every coordinate was equal.
// A macro-port widget (Macro In, Macro Out, Macro MIDI In/Out) is exempt from kJackInset by name and held to its own
// symmetric kMacroPortWidgetJackInset instead. A jack's label gap is the one rule in JackInset.h (jackLabelBounds).
#include "ThemeSweepSupport.h"
#include "ThemeSweepSurfaces.h"
#include "UI/Graph/JackInset.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <algorithm>
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

using namespace synth::test::theme_sweep;

namespace {

constexpr int kEdgeBand = 5;     // the columns between a card's edge and the left of a jack dot (centre 10, radius 5)
constexpr int kMaxEdgeDelta = 8; // per channel: a faint texture or shadow is fine, a second line (14 to 32) is not
constexpr int kCornerClearance = 16; // rows kept clear of the rounded bottom corners and the bottom shade

int channelDelta(juce::Colour a, juce::Colour b) {
    return std::max({std::abs(a.getRed() - b.getRed()), std::abs(a.getGreen() - b.getGreen()),
                     std::abs(a.getBlue() - b.getBlue()), std::abs(a.getAlpha() - b.getAlpha())});
}

// The jacks of one side that really draw a gutter dot: a jack whose hit-test finds it at its own centre. A knob-bound
// jack draws no dot and answers no press, so it is not an edge jack.
std::vector<juce::Point<int>> drawnJackCentres(ModuleComponent& card, bool isInput) {
    std::vector<juce::Point<int>> centres;
    auto* module = card.getModule();
    auto* mb = dynamic_cast<ModuleBase*>(module);
    const int count = mb != nullptr
                          ? (isInput ? mb->getVisibleInputPortCount() : mb->getVisibleOutputPortCount())
                          : (isInput ? module->getTotalNumInputChannels() : module->getTotalNumOutputChannels());
    for (int i = 0; i < count; ++i) {
        const auto centre = card.getPortCenter(i, isInput);
        const auto hit = card.getPortForPoint(centre);
        if (hit && !hit->isMidi && hit->isInput == isInput && hit->index == i)
            centres.push_back(centre);
    }
    return centres;
}

// The inset a card's jacks use: kJackInset, except a macro-port widget's own.
int expectedInset(ModuleComponent& card) {
    if (auto* mb = dynamic_cast<ModuleBase*>(card.getModule()))
        for (const auto t :
             {ModuleType::MacroInlet, ModuleType::MacroOutlet, ModuleType::MacroMidiInlet, ModuleType::MacroMidiOutlet})
            if (mb->getModuleType() == t)
                return ModuleComponent::kMacroPortWidgetJackInset;
    return synth::ui::kJackInset;
}

// How far from its edge the painted dot starts: the run of pixels like the one at the dot's centre, on the dot's row.
int dotInkDistance(const juce::Image& image, juce::Point<int> centre, bool isInput) {
    const auto ink = image.getPixelAt(centre.x, centre.y);
    const int step = isInput ? -1 : 1;
    int x = centre.x;
    while (x + step >= 0 && x + step < image.getWidth() && channelDelta(image.getPixelAt(x + step, centre.y), ink) < 40)
        x += step;
    return isInput ? x : image.getWidth() - 1 - x;
}

} // namespace

// Coordinates: the card's own numbers, theme-independent, so it runs once per type.
TEST(JackInsetSweep, EveryCardsInputAndOutputJacksSitTheSameDistanceFromTheirEdge) {
    juce::MessageManager::getInstance();
    int typesWithBothSides = 0;
    for (const auto& type : moduleTypeNames()) {
        std::string whyNot;
        auto rig = buildCard(type, whyNot);
        ASSERT_NE(rig, nullptr) << type << ": " << whyNot;
        auto* card = dynamic_cast<ModuleComponent*>(&rig->card());
        ASSERT_NE(card, nullptr) << type;
        const int width = card->getWidth();
        const int inset = expectedInset(*card);

        const auto ins = drawnJackCentres(*card, true);
        const auto outs = drawnJackCentres(*card, false);
        for (const auto& p : outs)
            EXPECT_EQ(width - p.x, inset) << type << ": an output jack's distance from the right edge";
        // The first input column sits `inset` in (a second column of a wide card sits further right).
        if (!ins.empty()) {
            const auto first =
                *std::min_element(ins.begin(), ins.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
            EXPECT_EQ(first.x, inset) << type << ": an input jack's distance from the left edge";
        }
        if (!ins.empty() && !outs.empty())
            ++typesWithBothSides;

        // The MIDI jacks, the same inset on their own side.
        if (card->getModule()->acceptsMidi())
            EXPECT_EQ(card->getMidiPortCenter(false).x, inset) << type << ": Midi In";
        if (card->getModule()->producesMidi())
            EXPECT_EQ(width - card->getMidiPortCenter(true).x, inset) << type << ": Midi Out";

        // The label gap: the one rule the card paints with.
        const juce::Point<int> centre(inset, 60);
        const auto inLabel = synth::ui::jackLabelBounds(centre, true);
        const auto outLabel = synth::ui::jackLabelBounds({width - inset, 60}, false);
        EXPECT_EQ(inLabel.getX() - (centre.x + 5), (width - inset - 5) - outLabel.getRight())
            << type << ": the gap between a jack dot and its label";
    }
    EXPECT_GT(typesWithBothSides, 5) << "the sweep found almost no card with an input and an output jack";
}

// Ink: what the card paints. Between a card's edge and the jack dots (the columns a jack never covers) the left edge
// and the right edge must show the same lines, whatever the theme or Depth level; and the first input dot and the first
// output dot, found in the painted pixels, are the same distance from their edges.
TEST(JackInsetSweep, EveryCardPaintsTheSameInkAtItsLeftAndRightEdgeInEveryTheme) {
    juce::MessageManager::getInstance();
    for (const auto& type : moduleTypeNames()) {
        std::string whyNot;
        auto rig = buildCard(type, whyNot);
        ASSERT_NE(rig, nullptr) << type << ": " << whyNot;
        auto* card = dynamic_cast<ModuleComponent*>(&rig->card());
        ASSERT_NE(card, nullptr) << type;
        const int w = card->getWidth();
        const int top = ModuleComponent::kPortGutterHeaderHeight; // below the header: no title, no corner radius
        const int bottom = card->getHeight() - kCornerClearance;
        const auto ins = drawnJackCentres(*card, true);
        const auto outs = drawnJackCentres(*card, false);

        for (const auto& theme : sweepThemes()) {
            SweepEnvironment env(theme);
            card->setLookAndFeel(&env.lookAndFeel());
            const auto image = renderToImage(*card);
            writeSweepPng("Jack inset " + type, theme.id, image);

            int worst = 0;
            int worstX = 0;
            int worstY = 0;
            for (int y = top; y < bottom; ++y)
                for (int x = 0; x < kEdgeBand; ++x) {
                    const int d = channelDelta(image.getPixelAt(x, y), image.getPixelAt(w - 1 - x, y));
                    if (d > worst) {
                        worst = d;
                        worstX = x;
                        worstY = y;
                    }
                }
            EXPECT_LE(worst, kMaxEdgeDelta)
                << type << " / " << theme.id << ": the pixel " << worstX
                << " in from the left edge differs from its mirror on the right (row " << worstY << ")";

            // A macro-port widget paints its own tinted row (paintMacroPortWidget), whose dot has no flat run to
            // measure.
            if (!ins.empty() && !outs.empty() && expectedInset(*card) == synth::ui::kJackInset) {
                const auto first =
                    *std::min_element(ins.begin(), ins.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
                EXPECT_EQ(dotInkDistance(image, first, true), dotInkDistance(image, outs.front(), false))
                    << type << " / " << theme.id << ": painted input dot vs painted output dot, from their edges";
            }
            card->setLookAndFeel(nullptr);
        }
    }
}
