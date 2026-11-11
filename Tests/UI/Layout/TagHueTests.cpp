// TagHueTests.cpp -- the type tags on the + Add lists' and Cmd+K's rows ("Module", "Track", "Preset" ...) are drawn
// larger and each kind in its own hue (docs/layout/theming.md#colour-code-kinds). One table says the hue of every tag
// the app can set; the tests list those tags from the code, hold the table to the documented one, check contrast in all
// four themes and see the box on a painted row.
#include "../SourceScan.h"
#include "../Theme/ThemeSweep/ThemeSweepSupport.h"
#include "UI/AddList/AddList.h"
#include "UI/CommandPalette/CommandPaletteRows.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Layout/TagHue.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <regex>
#include <set>

using synth::test::theme_sweep::contrastRatio;
using synth::test::theme_sweep::renderToImage;
using synth::ui::TagHue;

namespace {

// The table in docs/layout/theming.md: tag -> the cable-category slot it takes (Sources, Sequencing, ... Utility), or
// "text" for New.
const std::map<std::string, std::string> kDocumented = {
    {"Module", "Filters"},
    {"Snippet", "Filters"},
    {"Track", "Envelopes"},
    {"Preset", "Modulation FX"},
    {"Track preset", "Modulation FX"},
    {"Bus preset", "Modulation FX"},
    {"Marker", "Sources"},
    {"Action", "Sequencing"},
    {"Project", "Utility"},
    {"View", "Utility"},
    {"Setting", "Utility"},
    {"New", "text"},
};

std::set<std::string> tagsTheCodeSets() {
    std::set<std::string> tags = {"New"};
    for (const auto kind :
         {synth::ui::PaletteKind::Action, synth::ui::PaletteKind::Place, synth::ui::PaletteKind::Module,
          synth::ui::PaletteKind::Track, synth::ui::PaletteKind::Marker, synth::ui::PaletteKind::Setting,
          synth::ui::PaletteKind::Project, synth::ui::PaletteKind::Preset})
        tags.insert(synth::ui::paletteKindTag(kind).toStdString());
    const auto texts = synth::test::source_scan::sourceTexts();
    const auto& addList = texts.at("Source/UI/AddList/AddList.cpp");
    const auto from = addList.find("juce::String tagOfTrackRow");
    const auto body = addList.substr(from, addList.find("\n}\n", from) - from);
    const std::regex assigned(R"re(\btag = "([^"]+)")re");
    const std::regex returned(R"re(return "([^"]+)")re");
    for (const auto* text : {&addList, &body})
        for (std::sregex_iterator it(text->begin(), text->end(), text == &addList ? assigned : returned), end;
             it != end; ++it)
            tags.insert((*it)[1]);
    return tags;
}

juce::Colour rowBackground(const synth::theme::Colors& c) { return c.surface.overlaidWith(c.bg0).withAlpha(1.0f); }

} // namespace

TEST(TagHue, EveryTagTheCodeSetsHasAHueNotTheFallback) {
    const auto tags = tagsTheCodeSets();
    ASSERT_GE(tags.size(), 11u) << "the scan found the palette's and the Add lists' tags";
    for (const auto& tag : tags)
        EXPECT_NE(synth::ui::tagHueOf(tag), TagHue::Unknown) << "'" << tag << "' has no hue in TagHue.cpp";
    EXPECT_EQ(synth::ui::tagHueOf("Nonsense"), TagHue::Unknown);
    const synth::theme::Colors c;
    EXPECT_EQ(synth::ui::tagColour("Nonsense", c), c.textMuted) << "an unknown tag is muted";
}

TEST(TagHue, TheTableIsTheDocumentedOne) {
    const auto doc = juce::File(TESTS_ROOT_DIR).getParentDirectory().getChildFile("docs/layout/theming.md");
    const auto docText = doc.loadFileAsString().toStdString();
    std::map<std::string, std::set<std::string>> tagsOfSlot;
    for (const auto& [tag, slot] : kDocumented) {
        tagsOfSlot[slot].insert(tag);
        EXPECT_NE(docText.find("| " + tag + " |"), std::string::npos) << tag << " is not in docs/layout/theming.md";
    }
    std::map<std::string, std::set<TagHue>> hueOfSlot;
    for (const auto& style : synth::ui::tagTable()) {
        ASSERT_TRUE(kDocumented.count(style.tag)) << style.tag << " is in TagHue.cpp but not documented";
        hueOfSlot[kDocumented.at(style.tag)].insert(style.hue);
    }
    EXPECT_EQ(synth::ui::tagTable().size(), kDocumented.size());
    // Tags of one slot share a hue; two slots never do.
    std::set<TagHue> seen;
    for (const auto& [slot, hues] : hueOfSlot) {
        EXPECT_EQ(hues.size(), 1u) << slot << " is one hue";
        EXPECT_TRUE(seen.insert(*hues.begin()).second) << slot << " shares a hue with another slot";
    }
}

TEST(TagHue, EveryKindIsADistinctColourInEveryTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        std::vector<std::pair<TagHue, juce::Colour>> colours;
        for (const auto& style : synth::ui::tagTable())
            colours.emplace_back(style.hue, synth::ui::tagColour(style.hue, theme.colors));
        for (size_t a = 0; a < colours.size(); ++a)
            for (size_t b = a + 1; b < colours.size(); ++b) {
                if (colours[a].first == colours[b].first)
                    continue;
                const auto ca = colours[a].second, cb = colours[b].second;
                const int dist = std::abs(ca.getRed() - cb.getRed()) + std::abs(ca.getGreen() - cb.getGreen()) +
                                 std::abs(ca.getBlue() - cb.getBlue());
                EXPECT_GE(dist, 60) << theme.id << ": two kinds are nearly the same colour (slots "
                                    << (int)colours[a].first << " and " << (int)colours[b].first << ")";
            }
    }
}

TEST(TagHue, EveryTagIsReadableOnTheRowAndTheHighlightedRowInEveryTheme) {
    for (const auto& theme : synth::theme::builtInThemes()) {
        const auto& c = theme.colors;
        const auto row = rowBackground(c);
        const auto highlighted = c.accent.withAlpha(0.25f).overlaidWith(row);
        for (const auto& style : synth::ui::tagTable()) {
            const auto hue = synth::ui::tagColour(style.hue, c);
            for (const auto bg : {row, highlighted})
                EXPECT_GE(contrastRatio(hue, hue.withAlpha(synth::ui::kTagWashAlpha).overlaidWith(bg)), 4.5)
                    << theme.id << " / " << style.tag << (bg == row ? " on the row" : " on the highlighted row");
        }
    }
}

TEST(TagHue, APaintedTagIsTheNewSizeAndTheRowKeepsItsHeight) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    juce::Component parent;
    parent.setLookAndFeel(&lf);
    synth::ui::ModMatrixPicker::Item item;
    item.id = 1;
    item.category = "Things";
    item.text = "Filter";
    item.tag = "Module";
    auto picker = std::make_unique<synth::ui::ModMatrixPicker>("thing", std::vector{item}, -1, [](int) {});
    parent.setSize(400, 300);
    parent.addAndMakeVisible(*picker);
    picker->setBounds(0, 0, 360, 200);
    const auto image = renderToImage(*picker);
    const auto hue = synth::ui::tagColour("Module", lf.getTheme().colors);
    int top = 1000, bottom = -1, left = 1000, right = -1;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x) {
            const auto p = image.getPixelAt(x, y);
            if (p.getAlpha() == 255 && std::abs(p.getRed() - hue.getRed()) + std::abs(p.getGreen() - hue.getGreen()) +
                                               std::abs(p.getBlue() - hue.getBlue()) <=
                                           6) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
    ASSERT_GE(bottom, 0) << "the tag's outline is drawn in its hue";
    EXPECT_EQ(bottom - top + 1, synth::ui::kTagHeight);
    EXPECT_EQ(right - left + 1, synth::ui::tagWidth("Module"));
    EXPECT_GE(synth::ui::kTagFontSize, 11.0f);
    bool sawRow = false;
    for (auto* child : picker->getRowColumnForTest().getChildren())
        if (child->isVisible() && child->getHeight() == 24)
            sawRow = true;
    EXPECT_TRUE(sawRow) << "item rows stay 24 px tall";
    parent.setLookAndFeel(nullptr);
}

TEST(TagHue, OnlyTagHueDrawsATagOutlineAndNoRowPaintsATagInMuted) {
    const auto texts = synth::test::source_scan::sourceTexts();
    const std::regex outline(R"(drawRoundedRectangle\([^;]*\))");
    const auto& picker = texts.at("Source/UI/Graph/ModMatrixPicker.cpp");
    EXPECT_EQ(picker.find("kTagFontSize"), std::string::npos) << "the tag's size lives in TagHue.h";
    const auto from = picker.find("void paintTag(");
    ASSERT_NE(from, std::string::npos);
    const auto body = picker.substr(from, picker.find("\n    }", from) -
                                              from); // no trailing newline in the needle: Windows checkouts have CRLF
    EXPECT_EQ(body.find("palette_.muted"), std::string::npos) << "paintTag colours by kind, not by muted";
    EXPECT_EQ(body.find("drawRoundedRectangle"), std::string::npos) << "paintTag draws through drawTag";
    EXPECT_NE(body.find("drawTag("), std::string::npos);
    EXPECT_NE(texts.at("Source/UI/Layout/TagHue.cpp").find("drawRoundedRectangle"), std::string::npos);
}
