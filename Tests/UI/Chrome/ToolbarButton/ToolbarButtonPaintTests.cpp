// ToolbarButtonPaintTests.cpp -- the top bar's buttons paint their group colour on the chip and in the
// glyph (dark and Daylight themes), a lit toggle's chip is solid, a disabled button has no chip, captions
// are never coloured, a theme switch re-colours from the originals, and splitting an icon into body and
// moving parts draws exactly what the whole icon draws (docs/layout/chrome.md#toolbar). The inline layouts of the
// Timeline and Mixer bars (docs/layout/chrome.md#inline-buttons) are at the end: chip and caption in one row, an
// icon-only chip, width from the caption, and the lit pill.
#include "ToolbarButtonTestHelpers.h"
#include "UI/Chrome/ToolbarComponent.h"
#include <array>

using namespace toolbartest;
using synth::ui::ToolbarButtonLayout;

namespace {
constexpr float kPillAlpha = 0.12f; // a lit inline button's pill, as a share of its group colour
}

namespace {

// Every painted caption pixel lies on the blend from the bar colour to `text`: no hue anywhere in it.
::testing::AssertionResult captionIsPlainText(const juce::Image& img, juce::Rectangle<int> area, juce::Colour bg,
                                              juce::Colour text) {
    int inked = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x) {
            const auto p = img.getPixelAt(x, y);
            if (near(p, bg, 1))
                continue;
            ++inked;
            // The closest point on the bg -> text segment, per channel average.
            const float dr = (float)text.getRed() - bg.getRed(), dg = (float)text.getGreen() - bg.getGreen(),
                        db = (float)text.getBlue() - bg.getBlue();
            const float len2 = dr * dr + dg * dg + db * db;
            const float t = juce::jlimit(0.0f, 1.0f,
                                         ((p.getRed() - bg.getRed()) * dr + (p.getGreen() - bg.getGreen()) * dg +
                                          (p.getBlue() - bg.getBlue()) * db) /
                                             juce::jmax(1.0f, len2));
            const auto onLine = bg.interpolatedWith(text, t);
            if (!near(p, onLine, 6))
                return ::testing::AssertionFailure()
                       << "caption pixel (" << x << "," << y << ") " << p.toDisplayString(true) << " is off the "
                       << bg.toDisplayString(true) << " -> " << text.toDisplayString(true) << " blend";
        }
    if (inked == 0)
        return ::testing::AssertionFailure() << "no caption drawn";
    return ::testing::AssertionSuccess();
}

void expectGroupColours(ToolbarButtonTest& t, const synth::theme::Theme& theme, float restChipAlpha) {
    const auto bg = theme.colors.bg0;
    for (size_t i = 0; i < allToolbarIcons().size(); ++i) {
        const auto& spec = allToolbarIcons()[i];
        auto& b = *t.buttons_[i];
        const auto hue = synth::ui::toolbarGroupHue(theme, spec.group);
        const auto img = t.render(b);
        EXPECT_TRUE(pixelNear(img, chipOnlyPixel(b), bg.overlaidWith(hue.withAlpha(restChipAlpha))))
            << theme.name << " chip of " << spec.caption;
        // Save is a floppy in its own colours, so its glyph carries no group colour.
        if (spec.icon != Icon::ActionSave)
            EXPECT_TRUE(areaContains(img, iconArea(b), hue))
                << theme.name << ": the glyph of " << spec.caption << " has no pixel in its group colour";
    }
}

// The screen pixels covered by `r`, a rectangle in icon-grid units.
juce::Rectangle<int> iconRect(const ToolbarButton& b, juce::Rectangle<float> r) {
    return r.transformed(synth::theme::toolbarIconTransform(b)).getSmallestIntegerContainer();
}

} // namespace

TEST_F(ToolbarButtonTest, EveryButtonPaintsItsGroupColourOnADarkTheme) {
    for (const auto& spec : allToolbarIcons())
        make(spec.icon, spec.group, spec.caption);
    ASSERT_TRUE(theme().isDark);
    expectGroupColours(*this, theme(), 0.16f);
}

TEST_F(ToolbarButtonTest, EveryButtonPaintsItsGroupColourOnDaylight) {
    for (const auto& spec : allToolbarIcons())
        make(spec.icon, spec.group, spec.caption);
    useTheme(synth::theme::makeDaylight());
    ASSERT_FALSE(theme().isDark);
    expectGroupColours(*this, theme(), 0.10f);
}

TEST_F(ToolbarButtonTest, GroupsTakeTheirTokens) {
    const auto& c = theme().colors;
    EXPECT_EQ(synth::ui::toolbarGroupHue(theme(), ToolbarGroup::File), c.hueGreen);
    EXPECT_EQ(synth::ui::toolbarGroupHue(theme(), ToolbarGroup::Edit), c.hueAmber);
    EXPECT_EQ(synth::ui::toolbarGroupHue(theme(), ToolbarGroup::View), c.accent);
    EXPECT_EQ(synth::ui::toolbarGroupHue(theme(), ToolbarGroup::AI), c.hueRose);
    EXPECT_EQ(synth::ui::toolbarGroupHue(theme(), ToolbarGroup::Housekeeping), c.hueViolet);
    EXPECT_EQ(synth::ui::toolbarGroupHue(theme(), ToolbarGroup::Feedback), c.hueGreen);
}

TEST_F(ToolbarButtonTest, SavePaintsAFloppyInItsOwnColoursOnEveryTheme) {
    for (const auto& themeFn : {synth::theme::makeObsidian, synth::theme::makeDaylight}) {
        useTheme(themeFn());
        auto& b = make(Icon::ActionSave, ToolbarGroup::File, "Save");
        const auto img = render(b);
        EXPECT_TRUE(pixelNear(img, iconPixel(b, {5.0f, 12.0f}), juce::Colour(0xff1a1c20), 6))
            << theme().name << ": black body";
        EXPECT_TRUE(pixelNear(img, iconPixel(b, {9.0f, 7.0f}), juce::Colour(0xffc5cad2), 6))
            << theme().name << ": silver shutter";
        EXPECT_TRUE(areaContains(img, iconRect(b, {6.5f, 12.6f, 11.0f, 7.0f}), juce::Colours::white, 3))
            << theme().name << ": white label";
    }
}

TEST_F(ToolbarButtonTest, FeedbackIsAGreenRingWithAWhiteBubbleNotViolet) {
    for (const auto& themeFn : {synth::theme::makeObsidian, synth::theme::makeDaylight}) {
        useTheme(themeFn());
        auto& b = make(Icon::ActionFeedback, ToolbarGroup::Feedback, "Feedback");
        const auto img = render(b);
        const auto& c = theme().colors;
        EXPECT_TRUE(pixelNear(img, iconPixel(b, {12.0f, 1.8f}), c.hueGreen, 12)) << theme().name << ": ring";
        EXPECT_TRUE(areaContains(img, iconRect(b, {9.0f, 9.5f, 6.0f, 1.5f}), c.hueGreen, 24))
            << theme().name << ": lines";
        EXPECT_TRUE(areaContains(img, iconRect(b, {6.5f, 7.6f, 11.0f, 7.0f}), c.iconPaper, 6))
            << theme().name << ": bubble";
        EXPECT_FALSE(areaContains(img, iconArea(b), c.hueViolet, 6)) << theme().name << ": no violet left";
    }
}

TEST_F(ToolbarButtonTest, ALitToggleFillsItsChipAndDrawsTheGlyphInInk) {
    for (const auto& themeFn : {synth::theme::makeObsidian, synth::theme::makeDaylight}) {
        useTheme(themeFn());
        auto& b = make(Icon::ActionNew, ToolbarGroup::File, "New");
        b.setToggleState(true, juce::dontSendNotification);
        ASSERT_FLOAT_EQ(b.getLitAmount(), 1.0f);
        const auto hue = theme().colors.hueGreen;
        const auto img = render(b);
        EXPECT_TRUE(pixelNear(img, chipOnlyPixel(b), hue)) << theme().name;
        // New's plus disc, clear of the plus itself: the colour role, drawn ink on a lit chip.
        EXPECT_TRUE(pixelNear(img, iconPixel(b, {13.0f, 14.0f}), theme().colors.iconInk)) << theme().name;
    }
}

TEST_F(ToolbarButtonTest, ADisabledButtonHasNoChipAndAFadedGlyph) {
    auto& b = make(Icon::ActionNew, ToolbarGroup::File, "New");
    b.setEnabled(false);
    const auto img = render(b);
    const auto bg = theme().colors.bg0;
    EXPECT_TRUE(pixelNear(img, chipOnlyPixel(b), bg, 1));
    const auto hue = theme().colors.hueGreen;
    EXPECT_TRUE(pixelNear(img, iconPixel(b, {13.0f, 14.0f}), bg.overlaidWith(hue.withAlpha(0.4f)), 4));
}

TEST_F(ToolbarButtonTest, CaptionsAreNeverColoured) {
    for (const auto& themeFn : {synth::theme::makeObsidian, synth::theme::makeDaylight}) {
        useTheme(themeFn());
        for (const auto& spec : allToolbarIcons()) {
            auto& b = make(spec.icon, spec.group, spec.caption);
            b.setSize(120, 44);
            const auto& c = theme().colors;
            EXPECT_TRUE(captionIsPlainText(render(b), captionArea(b), c.bg0, c.textMuted))
                << theme().name << " rest caption of " << spec.caption;
            b.setToggleState(true, juce::dontSendNotification);
            EXPECT_TRUE(captionIsPlainText(render(b), captionArea(b), c.bg0, c.textPrimary))
                << theme().name << " lit caption of " << spec.caption;
            b.setToggleState(false, juce::dontSendNotification);
            b.setEnabled(false);
            EXPECT_TRUE(captionIsPlainText(render(b), captionArea(b), c.bg0, c.textDisabled))
                << theme().name << " disabled caption of " << spec.caption;
        }
    }
}

TEST_F(ToolbarButtonTest, AThemeSwitchRecoloursFromTheOriginals) {
    auto& b = make(Icon::ActionNew, ToolbarGroup::File, "New");
    // Neon then back: a recolour of an already-recoloured glyph would keep Neon's green somewhere.
    useTheme(synth::theme::makeNeon());
    useTheme(synth::theme::makeNeon());
    useTheme(synth::theme::makeObsidian());
    const auto img = render(b);
    EXPECT_TRUE(pixelNear(img, iconPixel(b, {13.0f, 14.0f}), theme().colors.hueGreen));
    EXPECT_FALSE(areaContains(img, iconArea(b), synth::theme::makeNeon().colors.hueGreen, 2));
}

TEST_F(ToolbarButtonTest, SplitArtDrawsExactlyTheWholeIcon) {
    for (const auto& spec : allToolbarIcons()) {
        const synth::theme::IconRoleColours roles{juce::Colours::orange, juce::Colours::orange.withAlpha(0.45f),
                                                  juce::Colours::navy, juce::Colours::white};
        auto whole = lf_.getRoleIcon(spec.icon, roles);
        ASSERT_NE(whole, nullptr) << spec.caption;
        const auto art = synth::ui::splitToolbarIconArt(lf_.getRoleIcon(spec.icon, roles));
        const auto toScreen = juce::AffineTransform::scale(2.0f);

        juce::Image a(juce::Image::ARGB, 48, 48, true, juce::SoftwareImageType());
        juce::Image s(juce::Image::ARGB, 48, 48, true, juce::SoftwareImageType());
        {
            juce::Graphics g(a);
            whole->draw(g, 1.0f, toScreen);
        }
        {
            juce::Graphics g(s);
            art.draw(g, toScreen, {}, 1.0f);
        }
        int differing = 0;
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 48; ++x)
                if (!near(a.getPixelAt(x, y), s.getPixelAt(x, y), 2) ||
                    std::abs(a.getPixelAt(x, y).getAlpha() - s.getPixelAt(x, y).getAlpha()) > 2)
                    ++differing;
        EXPECT_EQ(differing, 0) << spec.caption;
    }
}

TEST_F(ToolbarButtonTest, EveryIconWithAHoverMotionHasItsMovingPart) {
    for (const auto& spec : allToolbarIcons()) {
        auto& b = make(spec.icon, spec.group, spec.caption);
        const auto motion = synth::ui::toolbarIconMotion(spec.icon);
        for (size_t i = 0; i < 2; ++i) {
            const bool moves =
                motion[i].dx != 0.0f || motion[i].dy != 0.0f || motion[i].degrees != 0.0f || motion[i].scale != 1.0f;
            EXPECT_EQ(b.getArt(false).parts[i] != nullptr, moves) << spec.caption << " part " << i;
        }
    }
}

TEST(ToolbarLayoutTest, FeedbackSlotIsWideEnoughForItsCaption) {
    juce::Component parent;
    ToolbarComponent toolbar;
    parent.addAndMakeVisible(toolbar);
    std::array<std::unique_ptr<ToolbarButton>, ToolbarComponent::NumSlots> owned;
    std::array<juce::DrawableButton*, ToolbarComponent::NumSlots> ptrs{};
    for (int slot = 0; slot < ToolbarComponent::NumSlots; ++slot) {
        owned[(size_t)slot] = std::make_unique<ToolbarButton>("b" + juce::String(slot));
        parent.addAndMakeVisible(*owned[(size_t)slot]);
        ptrs[(size_t)slot] = owned[(size_t)slot].get();
    }
    toolbar.setButtons(ptrs);
    parent.setSize(2000, 44);
    toolbar.setBounds(0, 0, 2000, 44);
    toolbar.layoutButtons(toolbar.getLocalBounds());
    EXPECT_EQ(owned[ToolbarComponent::Feedback]->getWidth(), 84);
    // The chip and caption fit inside the button the bar actually lays out.
    const auto& b = *owned[ToolbarComponent::Save];
    EXPECT_EQ(b.getHeight(), 44);
}

TEST_F(ToolbarButtonTest, InlineChipSitsLeftOfItsCaptionInOneRow) {
    auto& b = make(Icon::ToolbarSnap, ToolbarGroup::Edit, "Snap");
    b.setLayout(ToolbarButtonLayout::Inline);
    b.setSize(b.getPreferredWidth(), 30);

    const auto chip = synth::theme::toolbarChipBounds(b);
    EXPECT_EQ(chip.getWidth(), 30.0f);
    EXPECT_EQ(chip.getHeight(), 24.0f);
    EXPECT_LE(chip.getBottom(), (float)b.getHeight()) << "the chip fits the bar";
    EXPECT_EQ(chip.getCentreY(), b.getHeight() * 0.5f) << "centred vertically, not above a caption";
    EXPECT_LT(chip.getRight(), (float)b.getWidth()) << "room is left for the caption beside it";
}

TEST_F(ToolbarButtonTest, CompactInlineChipFitsAMixerBar) {
    auto& b = make(Icon::MixerEq, ToolbarGroup::View, "EQ");
    b.setLayout(ToolbarButtonLayout::InlineCompact);
    b.setSize(b.getPreferredWidth(), 23);

    const auto chip = synth::theme::toolbarChipBounds(b);
    EXPECT_EQ(chip.getWidth(), 24.0f);
    EXPECT_EQ(chip.getHeight(), 20.0f);
    EXPECT_GE(chip.getY(), 0.0f);
    EXPECT_LE(chip.getBottom(), 23.0f);
    const auto glyph = juce::Rectangle<float>(0, 0, 24, 24).transformed(synth::theme::toolbarIconTransform(b));
    EXPECT_FLOAT_EQ(glyph.getWidth(), 15.0f);
}

TEST_F(ToolbarButtonTest, InlineWithoutACaptionIsJustACentredChip) {
    auto& b = make(Icon::MixerAddBus, ToolbarGroup::Edit, "Add Bus");
    b.setLayout(ToolbarButtonLayout::InlineCompact);
    b.setLabel("Add Bus", false);
    b.setSize(b.getPreferredWidth(), 23);

    EXPECT_TRUE(b.getButtonText().isEmpty());
    EXPECT_EQ(b.getTitle(), "Add Bus") << "the screen-reader name stays the full name";
    EXPECT_EQ(synth::theme::toolbarChipBounds(b).getCentreX(), b.getWidth() * 0.5f);
    EXPECT_LE(b.getPreferredWidth(), 30) << "icon-only is narrow";
}

TEST_F(ToolbarButtonTest, InlineWidthFollowsTheCaption) {
    auto& shortName = make(Icon::MixerEq, ToolbarGroup::View, "EQ");
    auto& longName = make(Icon::MixerResetMeters, ToolbarGroup::Housekeeping, "Reset Meters");
    for (auto* b : {&shortName, &longName})
        b->setLayout(ToolbarButtonLayout::InlineCompact);
    EXPECT_GT(longName.getPreferredWidth(), shortName.getPreferredWidth());

    longName.setLabel("Reset Meters", false);
    EXPECT_LT(longName.getPreferredWidth(), shortName.getPreferredWidth()) << "no caption, no text width";
}

TEST_F(ToolbarButtonTest, ACentredInlineButtonKeepsChipAndCaptionTogether) {
    auto& b = make(Icon::ToolbarAddTrack, ToolbarGroup::Edit, "Add Track");
    b.setLayout(ToolbarButtonLayout::Inline);
    b.setContentCentred(true);
    b.setSize(b.getPreferredWidth() + 100, 26);
    EXPECT_NEAR(b.getInlineContentOffset(), 50.0f, 1.0f);
    EXPECT_NEAR(synth::theme::toolbarChipBounds(b).getX(), 51.0f, 1.0f);

    b.setContentCentred(false);
    EXPECT_EQ(b.getInlineContentOffset(), 0.0f);
}

TEST_F(ToolbarButtonTest, ALitCaptionedInlineButtonSitsOnAFaintPill) {
    auto& b = make(Icon::ToolbarSnap, ToolbarGroup::Edit, "Snap");
    b.setLayout(ToolbarButtonLayout::Inline);
    b.setSize(b.getPreferredWidth(), 30);
    const juce::Point<int> pillOnly{b.getWidth() - 2, b.getHeight() / 2}; // right of the caption
    const auto bg = theme().colors.bg0;
    const auto hue = synth::ui::toolbarGroupHue(theme(), ToolbarGroup::Edit);

    EXPECT_TRUE(pixelNear(render(b), pillOnly, bg)) << "an unlit button has no pill";
    b.setToggleState(true, juce::dontSendNotification);
    EXPECT_TRUE(pixelNear(render(b), pillOnly, bg.overlaidWith(hue.withAlpha(kPillAlpha)))) << "a lit one does";

    b.setLabel("Snap", false);
    b.setSize(b.getPreferredWidth(), 30);
    EXPECT_TRUE(pixelNear(render(b), {1, 1}, bg)) << "icon-only has no pill, just the solid chip";
}

TEST_F(ToolbarButtonTest, TheInlineCaptionIsPaintedBesideTheChip) {
    auto& b = make(Icon::ToolbarAddTrack, ToolbarGroup::Edit, "Add Track");
    b.setLayout(ToolbarButtonLayout::Inline);
    b.setSize(b.getPreferredWidth(), 30);
    const auto img = render(b);
    const auto chip = synth::theme::toolbarChipBounds(b);

    int inked = 0;
    for (int y = 0; y < b.getHeight(); ++y)
        for (int x = (int)chip.getRight() + 2; x < b.getWidth(); ++x)
            inked += near(img.getPixelAt(x, y), theme().colors.bg0, 2) ? 0 : 1;
    EXPECT_GT(inked, 20) << "caption pixels to the right of the chip";

    int below = 0;
    for (int x = 0; x < b.getWidth(); ++x)
        below += near(img.getPixelAt(x, b.getHeight() - 1), theme().colors.bg0, 2) ? 0 : 1;
    EXPECT_EQ(below, 0) << "nothing is drawn under the chip: the caption is not stacked";
}
