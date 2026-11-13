// Concern: ViewDoc's JSON form -- the canvas zoom and pan a project persists.
#include "Project/ViewDoc.h"
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

using synth::ViewDoc;

namespace {

ViewDoc customView() {
    ViewDoc v;
    v.zoom = 0.75f;
    v.panX = -320.5f;
    v.panY = 48.0f;
    return v;
}

juce::var with(const char* key, const juce::var& value) {
    auto v = customView().toVar();
    v.getDynamicObject()->setProperty(key, value);
    return v;
}

// The target keeps its value whatever the outcome says, so a caller can trust "false means untouched".
bool rejects(const juce::var& v) {
    ViewDoc target = customView();
    const bool ok = target.fromVar(v);
    EXPECT_EQ(target, customView()) << "a failed fromVar must leave the target untouched";
    return !ok;
}

} // namespace

TEST(ViewDocTest, DefaultIsTheUnzoomedUnpannedView) {
    const ViewDoc d;
    EXPECT_FLOAT_EQ(d.zoom, 1.0f);
    EXPECT_FLOAT_EQ(d.panX, 0.0f);
    EXPECT_FLOAT_EQ(d.panY, 0.0f);
}

TEST(ViewDocTest, RoundTripsThroughVar) {
    ViewDoc parsed;
    ASSERT_TRUE(parsed.fromVar(customView().toVar()));
    EXPECT_EQ(parsed, customView());
}

TEST(ViewDocTest, RoundTripsThroughJsonText) {
    const auto text = juce::JSON::toString(customView().toVar());
    ViewDoc parsed;
    ASSERT_TRUE(parsed.fromVar(juce::JSON::parse(text)));
    EXPECT_EQ(parsed, customView());
}

TEST(ViewDocTest, ZoomClampsToTheWheelRange) {
    ViewDoc parsed;
    ASSERT_TRUE(parsed.fromVar(with("zoom", 50.0)));
    EXPECT_FLOAT_EQ(parsed.zoom, ViewDoc::kMaxZoom);
    ASSERT_TRUE(parsed.fromVar(with("zoom", 0.0)));
    EXPECT_FLOAT_EQ(parsed.zoom, ViewDoc::kMinZoom);
    ASSERT_TRUE(parsed.fromVar(with("zoom", -3.0)));
    EXPECT_FLOAT_EQ(parsed.zoom, ViewDoc::kMinZoom);
}

TEST(ViewDocTest, PanClampsToASaneDistance) {
    ViewDoc parsed;
    ASSERT_TRUE(parsed.fromVar(with("panX", 1.0e12)));
    EXPECT_FLOAT_EQ(parsed.panX, ViewDoc::kMaxPan);
    ASSERT_TRUE(parsed.fromVar(with("panY", -1.0e12)));
    EXPECT_FLOAT_EQ(parsed.panY, -ViewDoc::kMaxPan);
}

TEST(ViewDocTest, IntegerNumbersAreAccepted) {
    ViewDoc parsed;
    auto v = with("panX", 12);
    v.getDynamicObject()->setProperty("zoom", 1);
    ASSERT_TRUE(parsed.fromVar(v));
    EXPECT_FLOAT_EQ(parsed.panX, 12.0f);
    EXPECT_FLOAT_EQ(parsed.zoom, 1.0f);
}

TEST(ViewDocTest, MalformedValuesAreRejectedWithoutTouchingTheTarget) {
    EXPECT_TRUE(rejects(juce::var()));
    EXPECT_TRUE(rejects(juce::var("not an object")));
    EXPECT_TRUE(rejects(juce::var(3.0)));
    EXPECT_TRUE(rejects(with("zoom", "big")));
    EXPECT_TRUE(rejects(with("panX", true)));
    EXPECT_TRUE(rejects(with("panY", juce::var())));
    EXPECT_TRUE(rejects(with("zoom", std::numeric_limits<double>::quiet_NaN())));
    EXPECT_TRUE(rejects(with("panX", std::numeric_limits<double>::infinity())));

    for (const char* key : {"zoom", "panX", "panY"}) {
        auto missing = customView().toVar();
        missing.getDynamicObject()->removeProperty(key);
        EXPECT_TRUE(rejects(missing)) << "missing " << key;
    }
}
