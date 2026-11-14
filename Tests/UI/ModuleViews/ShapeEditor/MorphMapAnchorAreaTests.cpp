// MorphMapAnchorAreaTests.cpp -- where the dot menu and the Colour picker of a morph dot point: beside the
// dot, never on it. The arrow of a call-out lands on the edge of the area it is given, so that area must clear the
// dot's drawn bounds, ring included, wherever the dot sits in the map. The map alone is enough, so this file stays
// clear of the curve model's wide include reach.
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include <gtest/gtest.h>

using synth::ui::MorphMap;

namespace {

juce::MouseEvent rightClickAt(MorphMap& map, juce::Point<float> at) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                            juce::ModifierKeys::rightButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &map, &map, now, at,
                            now, 1, false);
}

struct AnchorRig {
    MorphMap map;
    std::vector<juce::Rectangle<int>> areas;

    AnchorRig() {
        map.setSize(252, 150);
        map.showMenu = [this](juce::PopupMenu&, juce::Rectangle<int> area) { areas.push_back(area); };
        // One dot in each corner of the plot and one in the middle.
        map.setDots({{"a", 0.0f, 0.0f, std::nullopt, 0},
                     {"b", 1.0f, 0.0f, std::nullopt, 1},
                     {"c", 0.0f, 1.0f, std::nullopt, 2},
                     {"d", 1.0f, 1.0f, std::nullopt, 3},
                     {"e", 0.5f, 0.25f, std::nullopt, 4}});
    }

    /** The dot with its selected ring, in screen coordinates. */
    juce::Rectangle<int> dotWithRing(int index) {
        const float r = MorphMap::kDotRadius + MorphMap::kDotRingGap;
        return map.localAreaToGlobal(juce::Rectangle<float>(r * 2.0f, r * 2.0f)
                                         .withCentre(map.getDotCentre(index))
                                         .getSmallestIntegerContainer());
    }
};

} // namespace

// Regression test for FRO1661: the arrow of the colour picker landed on the dot because the area was smaller than the
// dot with its ring.
TEST(MorphMapAnchorArea, TheAreaAroundADotHoldsItsRingWithAGapAtEveryCornerAndTheMiddle) {
    AnchorRig rig;
    ASSERT_EQ(rig.map.getNumDots(), 5);
    for (int i = 0; i < 5; ++i) {
        const auto ring = rig.dotWithRing(i);
        const auto area = rig.map.dotAnchorArea(i);
        EXPECT_TRUE(area.contains(ring)) << "dot " << i << ": area " << area.toString() << " must hold the ring "
                                         << ring.toString();
        EXPECT_GE(area.getWidth() - ring.getWidth(), 6) << "a gap each side so the arrow stands clear of the ring";
        EXPECT_EQ(area.getCentre(), ring.getCentre());
    }
}

TEST(MorphMapAnchorArea, TheDotMenuPointsAtTheSameAreaTheColourPickerUses) {
    AnchorRig rig;
    for (int i = 0; i < 5; ++i) {
        rig.areas.clear();
        const auto at = rig.map.getDotCentre(i);
        rig.map.mouseDown(rightClickAt(rig.map, at));
        ASSERT_EQ(rig.areas.size(), 1u) << "dot " << i;
        EXPECT_EQ(rig.areas.front(), rig.map.dotAnchorArea(i));
        EXPECT_TRUE(rig.areas.front().contains(rig.dotWithRing(i)));
    }
}
