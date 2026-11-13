// ThemeSweepPopoverTests.cpp -- the popovers (the content of a juce::CallOutBox, opened through PopupHost) in every
// built-in theme. A popover's content must paint an opaque themed panel of its own: the call-out's bubble behind it is
// JUCE's translucent dark scheme, so a content that paints nothing showed dark rows on a dark ghost in the light theme
// (the pen's shape flyout). Every content class named in PopoverRegistry.h must be listed here, either swept or
// exempt with a reason; a new popover content that is in neither fails ThemeSweepPopovers.EveryPopoverContentIsListed
// (docs/layout/theming.md, "Theme sweep guard").
#include "../../Layout/PopoverRegistry.h"
#include "ThemeSweepSupport.h"
#include "ThemeSweepSurfaces.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <string>

using namespace synth::test::theme_sweep;
using synth::ui::DrawShape;
using synth::ui::DrawShapeFlyout;

namespace {

struct PopoverSurface {
    std::string content;                                    // the class name PopoverRegistry.h gives
    std::function<std::unique_ptr<juce::Component>()> make; // empty for an exempt content
    std::string exemptReason;
};

const std::vector<PopoverSurface>& popoverSurfaces() {
    static const std::vector<PopoverSurface> surfaces = [] {
        std::vector<PopoverSurface> list;
        list.push_back({"DrawShapeFlyout",
                        [] {
                            return std::make_unique<DrawShapeFlyout>(
                                DrawShape::Sine,
                                [](DrawShape shape) { return "Shift+" + juce::String((int)shape + 1); },
                                [](DrawShape) {});
                        },
                        {}});
        // Not rendered here yet: each needs its own fixture (a graph, a project, a service). They join the sweep one
        // by one; this list only shrinks (an entry that gains a maker moves up).
        for (const char* name : {"ColourPickerPopup", "DrumIdeasPanel", "CardLayoutControlPanel", "ControlStylePanel",
                                 "HostedOutputsPanel", "ModMatrixPicker", "CardLayoutOnCardEditor", "KeyPicker",
                                 "ActionPickerComponent", "AddControllerPopover", "EQBandTypeMenu", "ShapeStackPanel",
                                 "StepGridChordPicker", "StepGridRandomizePanel", "juce::ColourSelector",
                                 "DualIOPerModulePopupContent", "AccountFlowPanel", "MidiDestinationPicker"})
            list.push_back({name, {}, "needs a fixture the sweep does not build yet"});
        return list;
    }();
    return surfaces;
}

double luminance(juce::Colour c) {
    return 0.2126 * c.getFloatRed() + 0.7152 * c.getFloatGreen() + 0.0722 * c.getFloatBlue();
}

} // namespace

TEST(ThemeSweepPopovers, EveryPopoverContentIsListed) {
    std::set<std::string> listed;
    for (const auto& surface : popoverSurfaces()) {
        listed.insert(surface.content);
        if (!surface.make)
            EXPECT_FALSE(surface.exemptReason.empty()) << surface.content << " is exempt with no reason";
    }
    std::set<std::string> used;
    for (const auto& user : synth::test::popoverUsers())
        for (const auto* content : user.contents)
            used.insert(content);
    for (const auto& content : used)
        EXPECT_TRUE(listed.count(content))
            << content << " opens in a popover but is not in the theme sweep's popover "
            << "list (ThemeSweepPopoverTests.cpp): render it, or exempt it with a reason";
    for (const auto& content : listed)
        EXPECT_TRUE(used.count(content)) << content << " is in the sweep's popover list but no file opens it any more";
}

// The panel is opaque at its corners (inside the rounded ends) and in the middle, is not JUCE's default dark or grey,
// follows the theme (Daylight is light, the rest differ from it), and every row's name and shortcut text read on what
// is drawn behind them (4.5:1, the same bar the text sweep holds Labels to).
TEST(ThemeSweepPopovers, TheShapeFlyoutIsAnOpaqueThemedPanelWithReadableRowsInEveryTheme) {
    juce::MessageManager::getInstance();
    std::map<std::string, std::vector<juce::Colour>> probesByTheme;
    for (const auto& theme : synth::theme::builtInThemes()) {
        SweepEnvironment env(theme);
        auto content = popoverSurfaces().front().make();
        env.anchor().addAndMakeVisible(*content);
        auto& flyout = static_cast<DrawShapeFlyout&>(*content);
        const auto image = renderToImage(flyout);
        writeSweepPng("popover-shape-flyout", theme.id, image);

        const int w = flyout.getWidth(), h = flyout.getHeight();
        const std::vector<juce::Point<int>> probes{{5, 5},         {w - 6, 5}, {5, h - 6},
                                                   {w - 6, h - 6}, {w / 2, 1}, {w / 2, h - 5}};
        std::vector<juce::Colour> pixels;
        for (const auto& p : probes) {
            const auto pixel = image.getPixelAt(p.x, p.y);
            pixels.push_back(pixel);
            EXPECT_EQ(pixel.getAlpha(), 255) << theme.id << ": probe at " << p.x << "," << p.y << " is not opaque";
        }
        probesByTheme[theme.id.toStdString()] = pixels;
        // The panel is the theme's window colour, not the call-out's dark scheme.
        EXPECT_EQ(pixels[5], synth::theme::windowBackgroundFor(theme))
            << theme.id; // bottom edge, under no lit row, clear of the text

        for (const auto shape : synth::ui::kAllDrawShapes) {
            auto* row = flyout.getRow(shape);
            ASSERT_NE(row, nullptr);
            // A pixel inside the row, right of the shortcut text: what the text is drawn over (the current row's lit
            // fill included).
            const auto behind = image.getPixelAt(row->getRight() - 4, row->getBounds().getCentreY());
            const auto& colours = theme.colors;
            EXPECT_GE(contrastRatio(colours.textPrimary, behind), kMinTextContrast)
                << theme.id << ": the " << drawShapeName(shape) << " row's name";
            const auto hint = flyout.getCurrentShape() == shape ? colours.textPrimary : colours.textMuted;
            EXPECT_GE(contrastRatio(hint, behind), kMinTextContrast)
                << theme.id << ": the " << drawShapeName(shape) << " row's shortcut";
        }
        content->setLookAndFeel(nullptr);
    }
    // Daylight is the light theme; Obsidian and Neon are dark: the panel follows the theme.
    ASSERT_TRUE(probesByTheme.count("daylight"));
    EXPECT_GT(luminance(probesByTheme["daylight"].front()), 0.6f) << "the light theme's panel is light";
    for (const auto& [id, pixels] : probesByTheme)
        if (id != "daylight")
            EXPECT_NE(pixels.front(), probesByTheme["daylight"].front()) << id << ": the panel never reached the theme";
}
