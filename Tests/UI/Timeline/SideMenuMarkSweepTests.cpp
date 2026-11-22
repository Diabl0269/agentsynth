// SideMenuMarkSweepTests.cpp -- every control in Source/ that paints a menu mark (the shared side-menu notch, a combo
// chevron) is either an icon button with a side menu, which owns a SideMenuGesture and opens by right-click, corner
// press and held press, or is listed here with the reason it is not. A new control that paints a mark and is in
// neither list fails by name. docs/timeline/edit-tools.md#the-side-menu-notch
#include "../SourceScan.h"
#include "TimelinePanel/TimelinePanelTestEvents.h"
#include "UI/Chrome/ToolbarButton/SideMenuMark.h"
#include "UI/Chrome/ToolbarButton/SnapModeButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/DepthLevel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawPenButton.h"
#include "UI/Timeline/ClipSizingButton.h"
#include "UI/Timeline/TrackColour.h"
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>

namespace {

// The files that paint a menu mark or own the gesture, with why each is on the list. "adopted" files are also
// exercised below.
const std::map<std::string, std::string>& registry() {
    static const std::map<std::string, std::string> entries = {
        {"Source/UI/Timeline/ClipSizingButton.h", "adopted: the Clip sizing chip"},
        {"Source/UI/Timeline/ClipSizingButton.cpp", "adopted: the Clip sizing chip"},
        {"Source/UI/Chrome/ToolbarButton/SnapModeButton.h", "adopted: the Snap mode picker"},
        {"Source/UI/Chrome/ToolbarButton/SnapModeButton.cpp", "adopted: the Snap mode picker"},
        {"Source/UI/Chrome/ToolbarButton/ToolbarMenuNotch.h",
         "exempt: declares the notch painter ClipSizingButton and SnapModeButton share (the buttons are listed)"},
        {"Source/UI/Chrome/ToolbarButton/ToolbarMenuNotch.cpp",
         "exempt: the notch painter ClipSizingButton and SnapModeButton share (the buttons are listed)"},
        {"Source/UI/Timeline/AutomationLanes/LaneShapes/DrawPenButton.h", "adopted: the pen"},
        {"Source/UI/Timeline/AutomationLanes/LaneShapes/DrawPenButton.cpp", "adopted: the pen"},
        {"Source/UI/Timeline/TimelineRoutingPane/TimelineRoutingPaneControls.cpp",
         "exempt: a text dropdown (caption and combo chevron), not an icon chip with a side menu"},
        {"Source/UI/Theme/AppLookAndFeel/AppLookAndFeel.h", "exempt: declares paintComboChevron for text combos"},
        {"Source/UI/Theme/AppLookAndFeel/AppLookAndFeelComboAndMenus.cpp",
         "exempt: draws every juce::ComboBox, a text dropdown whose whole face opens the menu"},
    };
    return entries;
}

// The helper itself is the mechanism, not a control.
constexpr const char* kHelperPrefix = "Source/UI/Chrome/ToolbarButton/SideMenuMark.";

} // namespace

TEST(SideMenuMarkSweep, EveryControlThatPaintsAMenuMarkIsAdoptedOrExemptWithAReason) {
    namespace scan = synth::test::source_scan;
    std::map<std::string, int> found;
    for (const auto& [file, n] : scan::countCallSites(
             std::regex(R"(paintComboChevron\(|paintSideMenuNotch\(|paintToolbarMenuNotch\(|\bSideMenuGesture\b)"),
             kHelperPrefix))
        found[file] = n;
    for (const auto& [file, n] : found)
        EXPECT_EQ(registry().count(file), 1u) << "not on the side-menu sweep list: " << file
                                              << " paints a menu mark. An icon button with a side menu uses "
                                                 "SideMenuGesture and paintSideMenuNotch; anything else gets an "
                                                 "'exempt: <reason>' entry in SideMenuMarkSweepTests.cpp";
    for (const auto& [file, why] : registry()) {
        EXPECT_EQ(found.count(file), 1u) << "stale entry (no menu mark painted there any more): " << file;
        EXPECT_TRUE(why.rfind("adopted:", 0) == 0 || why.rfind("exempt: ", 0) == 0 && why.size() > 12) << file;
    }
}

namespace {

// One adopted control as the sweep drives it: the component, its gesture and how many menus it has opened.
struct Adopted {
    std::string name;
    std::unique_ptr<juce::Component> component;
    synth::ui::SideMenuGesture* gesture = nullptr;
    int opened = 0;
    int picked = 0;            // a tool or mode change a menu press must never cause
    juce::Point<float> corner; // a point inside the mark's press corner
};

std::unique_ptr<Adopted> makeSizing() {
    auto a = std::make_unique<Adopted>();
    a->name = "Clip sizing chip";
    auto button = std::make_unique<synth::ui::ClipSizingButton>();
    button->setSize(button->getFixedWidth(), 28);
    button->showMenuHook = [raw = a.get()](juce::PopupMenu&) { ++raw->opened; };
    button->onModeChanged = [raw = a.get()](synth::ClipSizingMode) { ++raw->picked; };
    a->gesture = &button->getMenuGesture();
    const auto chip = synth::theme::toolbarChipBounds(*button);
    a->corner = {chip.getRight() - 3.0f, chip.getBottom() - 3.0f};
    a->component = std::move(button);
    return a;
}

std::unique_ptr<Adopted> makeSnapMode() {
    auto a = std::make_unique<Adopted>();
    a->name = "Snap mode picker";
    auto button = std::make_unique<synth::ui::SnapModeButton>("snapModeTest", "snapModeTest");
    button->setSize(button->getFixedWidth(), 28);
    button->showMenuHook = [raw = a.get()](juce::PopupMenu&) { ++raw->opened; };
    button->onModeChanged = [raw = a.get()](synth::ui::SnapMode) { ++raw->picked; };
    a->gesture = &button->getMenuGesture();
    const auto chip = synth::theme::toolbarChipBounds(*button);
    a->corner = {chip.getRight() - 3.0f, chip.getBottom() - 3.0f};
    a->component = std::move(button);
    return a;
}

std::unique_ptr<Adopted> makePen() {
    auto a = std::make_unique<Adopted>();
    a->name = "Draw pen";
    auto button = std::make_unique<synth::ui::DrawPenButton>("pen");
    button->setSize(24, 24);
    button->onOpenFlyout = [raw = a.get()] { ++raw->opened; };
    button->onClick = [raw = a.get()] { ++raw->picked; };
    a->gesture = &button->getMenuGesture();
    a->corner = {21.0f, 21.0f};
    a->component = std::move(button);
    return a;
}

const juce::ModifierKeys kLeft(juce::ModifierKeys::leftButtonModifier);
const juce::ModifierKeys kRight(juce::ModifierKeys::rightButtonModifier);

} // namespace

TEST(SideMenuMarkSweep, EveryAdoptedControlOpensItsMenuByRightClickCornerPressAndHold) {
    std::unique_ptr<Adopted> (*const makers[])() = {makeSizing, makeSnapMode, makePen};
    for (const auto make : makers) {
        {
            auto a = make();
            auto& c = *a->component;
            const auto centre = c.getLocalBounds().getCentre().toFloat();
            c.mouseDown(makeClickEvent(c, centre, kRight));
            c.mouseUp(makeClickEvent(c, centre, kRight));
            EXPECT_EQ(a->opened, 1) << a->name << ": right-click";
            EXPECT_EQ(a->picked, 0) << a->name << ": right-click picks nothing";
        }
        {
            auto a = make();
            auto& c = *a->component;
            const auto corner = a->corner;
            c.mouseDown(makeClickEvent(c, corner, kLeft));
            c.mouseUp(makeClickEvent(c, corner, kLeft));
            EXPECT_EQ(a->opened, 1) << a->name << ": corner press";
            EXPECT_EQ(a->picked, 0) << a->name << ": corner press picks nothing";
        }
        {
            auto a = make();
            auto& c = *a->component;
            const auto centre = c.getLocalBounds().getCentre().toFloat();
            c.mouseDown(makeClickEvent(c, centre, kLeft));
            EXPECT_TRUE(a->gesture->isHoldPendingForTest()) << a->name;
            a->gesture->holdElapsedForTest();
            EXPECT_EQ(a->opened, 1) << a->name << ": held press";
            c.mouseUp(makeClickEvent(c, centre, kLeft));
            EXPECT_EQ(a->opened, 1) << a->name << ": the release opens nothing more";
            EXPECT_EQ(a->picked, 0) << a->name << ": the release after a hold is not a click";
        }
    }
}

namespace {

constexpr float kMinContrast = 3.5f;

enum class State { Rest, Hover, Pressed };
const char* name(State s) { return s == State::Rest ? "rest" : (s == State::Hover ? "hover" : "pressed"); }

void drive(juce::Button& b, State s) {
    b.setState(s == State::Rest ? juce::Button::buttonNormal
                                : (s == State::Hover ? juce::Button::buttonOver : juce::Button::buttonDown));
}

} // namespace

using synth::ui::colourContrast;

namespace {

// Where a notch sits and what is just left of it, for a chip painted `chip` (in the button's coordinates) with the
// press squash `squash`: one pixel inside the triangle and one on the chip beside it.
struct NotchProbe {
    juce::Point<int> notch;
    juce::Point<int> beside;
};

NotchProbe probeFor(juce::Rectangle<float> chip, float radius, juce::Point<float> squash) {
    const float leg = synth::ui::sideMenuNotchLegFor(radius);
    const auto squashed = juce::AffineTransform::scale(squash.x, squash.y, chip.getCentreX(), chip.getCentreY());
    const auto toPixel = [&](juce::Point<float> p) {
        const auto q = p.transformedBy(squashed);
        return juce::Point<int>((int)std::floor(q.x), (int)std::floor(q.y));
    };
    // On the corner's diagonal, inside the rounded arc and the triangle's hypotenuse, clear of any antialiased edge.
    const float arcDepth = radius * (1.0f - 1.0f / 1.41421356f);
    const float depth = arcDepth * 0.35f + leg * 0.325f + 0.5f;
    return {toPixel({chip.getRight() - depth, chip.getBottom() - depth}),
            toPixel({chip.getRight() - leg + 1.0f, chip.getBottom() - 3.5f})};
}

// The notch is the chip's own colour pushed away from it: darker on a light chip, lighter on a dark one, 3.5:1 against
// the chip pixels beside it, never pure black or white.
void expectNotchReads(const juce::Image& img, const NotchProbe& p, const juce::String& what,
                      const synth::theme::Theme&) {
    const auto notch = img.getPixelAt(p.notch.x, p.notch.y);
    const auto beside = img.getPixelAt(p.beside.x, p.beside.y);
    EXPECT_GE(colourContrast(notch, beside), kMinContrast)
        << what << ": notch " << notch.toDisplayString(false) << " beside " << beside.toDisplayString(false);
    const bool lightChip = colourContrast(beside, juce::Colours::black) > colourContrast(beside, juce::Colours::white);
    if (lightChip)
        EXPECT_LT(notch.getPerceivedBrightness(), beside.getPerceivedBrightness())
            << what << ": darker on a light chip";
    else
        EXPECT_GT(notch.getPerceivedBrightness(), beside.getPerceivedBrightness())
            << what << ": lighter on a dark chip";
    for (const auto pure : {juce::Colour(0xff000000), juce::Colour(0xffffffff)})
        EXPECT_NE(notch, pure) << what << ": notch is pure " << pure.toDisplayString(false);
}

synth::theme::Theme atDepth(synth::theme::Theme theme, synth::theme::DepthLevel depth) {
    theme.treatment.depth = depth;
    return theme;
}

} // namespace

TEST(SideMenuNotchInkSweep, TheDrawPensNotchReadsLitUnlitHoveredAndPressedInEveryTheme) {
    for (const auto& base : synth::theme::builtInThemes())
        for (const auto depth : {synth::theme::DepthLevel::Soft, synth::theme::DepthLevel::Rich}) {
            const auto theme = atDepth(base, depth);
            synth::theme::AppLookAndFeel lf;
            lf.applyTheme(theme);
            for (const bool lit : {false, true})
                for (const auto state : {State::Rest, State::Hover, State::Pressed}) {
                    synth::ui::DrawPenButton pen("pen");
                    pen.setLookAndFeel(&lf);
                    pen.setSize(24, 24);
                    pen.setVisible(true);
                    pen.setToggleState(lit, juce::dontSendNotification);
                    drive(pen, state);
                    juce::Image img(juce::Image::ARGB, 24, 24, true, juce::SoftwareImageType());
                    {
                        juce::Graphics g(img);
                        g.fillAll(theme.colors.bg0);
                        pen.paintEntireComponent(g, false);
                    }
                    const auto chip = pen.getLocalBounds().toFloat().reduced(0.5f);
                    expectNotchReads(img, probeFor(chip, theme.metrics.pillRadius, {1.0f, 1.0f}),
                                     theme.name + " pen " + (lit ? "lit " : "unlit ") + name(state), theme);
                    pen.setLookAndFeel(nullptr);
                }
        }
}

namespace {
template <class Button>
void sweepToolbarNotch(const char* label, const std::function<std::unique_ptr<Button>()>& make) {
    for (const auto& base : synth::theme::builtInThemes())
        for (const auto depth : {synth::theme::DepthLevel::Soft, synth::theme::DepthLevel::Rich}) {
            const auto theme = atDepth(base, depth);
            synth::theme::AppLookAndFeel lf;
            lf.applyTheme(theme);
            for (const bool lit : {false, true})
                for (const auto state : {State::Rest, State::Hover, State::Pressed}) {
                    auto b = make();
                    b->setLookAndFeel(&lf);
                    b->setSize(b->getFixedWidth(), 28);
                    b->setVisible(true);
                    b->setToggleState(lit, juce::dontSendNotification);
                    drive(*b, state);
                    juce::Image img(juce::Image::ARGB, b->getWidth(), b->getHeight(), true, juce::SoftwareImageType());
                    {
                        juce::Graphics g(img);
                        g.fillAll(theme.colors.bg0);
                        b->paintEntireComponent(g, false);
                    }
                    const auto spec = synth::ui::toolbarChipSpec(b->getLayout());
                    expectNotchReads(img,
                                     probeFor(synth::theme::toolbarChipBounds(*b), spec.chipRadius, b->getChipSquash()),
                                     theme.name + " " + label + (lit ? " lit " : " unlit ") + name(state), theme);
                    b->setLookAndFeel(nullptr);
                }
        }
}
} // namespace

TEST(SideMenuNotchInkSweep, TheSizingChipsNotchReadsInEveryStateAndTheme) {
    sweepToolbarNotch<synth::ui::ClipSizingButton>("sizing chip",
                                                   [] { return std::make_unique<synth::ui::ClipSizingButton>(); });
}

TEST(SideMenuNotchInkSweep, TheSnapPickersNotchReadsInEveryStateAndTheme) {
    sweepToolbarNotch<synth::ui::SnapModeButton>("snap picker", [] {
        return std::make_unique<synth::ui::SnapModeButton>("litInkSweepSnapMode", "litInkSweepSnapMode");
    });
}

// Regression test for FRO2203: on the Daylight toolbar the notch was near the chip's own colour. The Snap picker, the
// Draw pen and the Sizing chip, unlit, lit, hovered and pressed, paint a notch whose leg reaches at least 12 px
// (a probe 9.5 px along the bottom edge is notch ink) and that is clearly distinct from the chip (above 3:1).
TEST(SideMenuNotchInkSweep, DaylightsNotchIsVisibleAtTwelvePixelsOnAllThreeControls) {
    const auto theme = synth::theme::makeDaylight();
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(theme);
    const auto check = [&](juce::Component& b, juce::Rectangle<float> chip, float radius, juce::Point<float> squash,
                           const juce::String& what) {
        juce::Image img(juce::Image::ARGB, b.getWidth(), b.getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics g(img);
            g.fillAll(theme.colors.bg0);
            b.paintEntireComponent(g, false);
        }
        EXPECT_GE(synth::ui::sideMenuNotchLegFor(radius), 12.0f) << what;
        const auto at = [&](float dx, float dy) {
            const auto q = juce::Point<float>(chip.getRight() - dx, chip.getBottom() - dy)
                               .transformedBy(juce::AffineTransform::scale(squash.x, squash.y, chip.getCentreX(),
                                                                           chip.getCentreY()));
            return img.getPixelAt((int)std::floor(q.x), (int)std::floor(q.y));
        };
        const auto far = at(9.5f, 1.5f); // inside a 12 px leg, outside a 10 px one
        const auto beside = at(14.0f, 3.5f);
        EXPECT_GT(colourContrast(far, beside), 3.0f)
            << what << ": far " << far.toDisplayString(false) << " beside " << beside.toDisplayString(false);
    };
    for (const bool lit : {false, true})
        for (const auto state : {State::Rest, State::Hover, State::Pressed}) {
            const juce::String suffix = juce::String(lit ? " lit " : " unlit ") + name(state);
            {
                synth::ui::DrawPenButton pen("pen");
                pen.setLookAndFeel(&lf);
                pen.setSize(28, 28);
                pen.setVisible(true);
                pen.setToggleState(lit, juce::dontSendNotification);
                drive(pen, state);
                check(pen, pen.getLocalBounds().toFloat().reduced(0.5f), theme.metrics.pillRadius, {1.0f, 1.0f},
                      "pen" + suffix);
                pen.setLookAndFeel(nullptr);
            }
            {
                synth::ui::SnapModeButton snap("daylightNotchSnap", "daylightNotchSnap");
                snap.setLookAndFeel(&lf);
                snap.setSize(snap.getFixedWidth(), 28);
                snap.setVisible(true);
                snap.setToggleState(lit, juce::dontSendNotification);
                drive(snap, state);
                check(snap, synth::theme::toolbarChipBounds(snap),
                      synth::ui::toolbarChipSpec(snap.getLayout()).chipRadius, snap.getChipSquash(),
                      "snap picker" + suffix);
                snap.setLookAndFeel(nullptr);
            }
            {
                synth::ui::ClipSizingButton sizing;
                sizing.setLookAndFeel(&lf);
                sizing.setSize(sizing.getFixedWidth(), 28);
                sizing.setVisible(true);
                sizing.setToggleState(lit, juce::dontSendNotification);
                drive(sizing, state);
                check(sizing, synth::theme::toolbarChipBounds(sizing),
                      synth::ui::toolbarChipSpec(sizing.getLayout()).chipRadius, sizing.getChipSquash(),
                      "sizing chip" + suffix);
                sizing.setLookAndFeel(nullptr);
            }
        }
}
