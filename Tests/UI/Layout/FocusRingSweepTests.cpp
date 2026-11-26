// FocusRingSweepTests.cpp -- the focus ring is one ring on every focusable control, in every built-in theme. Every open
// focus region of a real MainComponent (each main view in turn, a piano roll and a drum machine open on a clip) and
// every dialog of the theme sweep registry is walked with focusableDescendants (FocusStepWithin.h, the helper the reach
// sweeps use), so a new control or dialog is measured without being listed. Each stop is painted twice on the theme's
// background, once as it rests and once with every ring shown (setFocusRingsShownForTest: a headless window cannot hold
// focus); the pixels that differ are the ring, and the control's text is the pixels that differ when its text is left
// out (the way LayoutGapSweepTests measures a button). Images use SoftwareImageType(): the default native image type
// reads back zeros on a Windows runner.
//
// Asserted for each stop that paints a ring itself:
// 1. Thickness. Across the middle of each of the ring's four sides the accent's coverage adds up to
//    synth::ui::focusRingThickness(theme), within kThicknessTolerance.
// 2. Gap. The ring's outer edge is flush with the control's bounds (kFlushInset), or stands off by exactly the shared
//    kFocusRingGap for a control that draws a stand-off ring. Anything else is its own gap.
// 3. Centred. The ring's box is centred on the control's bounds, within one pixel on each axis.
// 4. Clear of text. No two pixels of the ring lie on pixels of the control's text (a Button's text, a Label's, a
//    ComboBox's shown text), so a ring never covers a letter (a fixed-width text link once ringed its whole box over
//    its first letter).
//
// Each toggle-able focusable control (a ToggleButton, a button that toggles on click, not a radio) is then measured a
// second time turned ON, named "<control> [on]": its ring must still show at the shared thickness, flush or stood off,
// with the surface (not the accent fill) in the band just inside the ring on every side.
//
// Skipped, and counted in the census this test prints (never silently): a stop that paints no ring itself (its ring is
// drawn by its parent or its region: KeyRing, ListBoxFocusRing, the zones cursor row; ReachSweepTests asserts that
// every stop lands with a ring showing), a stop that contains other stops (its parts are measured on their own), and,
// for rule 4 only, a control with no text to take out (an icon, a knob, a custom painter).
//
// A finding prints "<region id or dialog name> / <component description>: <what>" (digits in a description are written
// as '#'; the theme and numbers are printed beside it, not in it). A finding that is known and ticketed lives in
// kKnownGaps; the list may only shrink (docs/development/accessibility.md, "Focus ring").
// AGENTSYNTH_FOCUS_RING_VERBOSE=1 prints every measured stop.
#include "../../TestWait.h"
#include "../Theme/ThemeSweep/ThemeSweepSurfaces.h"
#include "KeyboardReachFixture.h"
#include "KeyboardSweepShell.h"
#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ThemeManager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>

using namespace synth::test::keyboard_sweep;
using synth::test::theme_sweep::dialogSurfaces;
using synth::test::theme_sweep::SweepEnvironment;
using Tab = synth::ui::BottomDockComponent::Tab;

namespace {

// A finding that is known and has a ticket. Key: "<surface> / <control>: <what>" as a finding prints it. The list may
// only shrink: a listed finding that is no longer found fails the run.
const std::vector<KnownGap> kKnownGaps = {};

constexpr float kThicknessTolerance = 0.45f; // px: one painted edge is anti-aliased, the sum is not exact
constexpr int kFlushInset = 1;               // px between the control's edge and the ring's outer edge
constexpr float kInkStep = 0.12f;            // per channel: a text pixel differs from the text-less one by more
constexpr float kStandOffTolerance = 1.5f;   // px around kFocusRingGap for a stand-off ring

struct RingsShown {
    RingsShown() { synth::ui::setFocusRingsShownForTest(true); }
    ~RingsShown() { synth::ui::setFocusRingsShownForTest(false); }
};

bool verbose() {
    const char* v = std::getenv("AGENTSYNTH_FOCUS_RING_VERBOSE");
    return v != nullptr && v[0] == '1';
}

// Room around the control in every render: a ring that stands off outside the control's bounds (the search pill's)
// paints unclipped, so the image is larger than the control and the control sits at (kMargin, kMargin) in it.
constexpr int kMargin = 8;

// `c` painted (itself and its children) on `background`, with its fill stepped in as if its ring showed when
// `fillStep` is set (the at-rest image of a control whose fill steps in under the ring).
juce::Image paintOn(juce::Component& c, juce::Colour background, bool fillStep = false) {
    juce::Image image(juce::Image::ARGB, juce::jmax(1, c.getWidth()) + 2 * kMargin,
                      juce::jmax(1, c.getHeight()) + 2 * kMargin, true, juce::SoftwareImageType());
    juce::Graphics g(image);
    g.fillAll(background);
    g.setOrigin(kMargin, kMargin);
    synth::ui::setFocusFillStepShownForTest(fillStep);
    c.paintEntireComponent(g, false);
    synth::ui::setFocusFillStepShownForTest(false);
    return image;
}

// The pixels where two renders differ.
struct Mask {
    int w = 0, h = 0;
    std::vector<char> on;
    bool at(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && on[(size_t)(y * w + x)] != 0; }
};

// Pixels where `a` and `b` differ by more than `minStep` in some channel (0 is any difference at all).
Mask difference(const juce::Image& a, const juce::Image& b, float minStep = 0.0f) {
    Mask m;
    m.w = a.getWidth();
    m.h = a.getHeight();
    m.on.assign((size_t)(m.w * m.h), 0);
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            const auto pa = a.getPixelAt(x, y), pb = b.getPixelAt(x, y);
            const float step = std::max({std::abs(pa.getFloatRed() - pb.getFloatRed()),
                                         std::abs(pa.getFloatGreen() - pb.getFloatGreen()),
                                         std::abs(pa.getFloatBlue() - pb.getFloatBlue())});
            m.on[(size_t)(y * m.w + x)] = pa != pb && step > minStep ? 1 : 0;
        }
    return m;
}

// How much of `accent` the ring put on top of `base` at one pixel: 0 none, 1 all of it.
float coverage(juce::Colour base, juce::Colour ringed, juce::Colour accent) {
    const float v[3] = {accent.getFloatRed() - base.getFloatRed(), accent.getFloatGreen() - base.getFloatGreen(),
                        accent.getFloatBlue() - base.getFloatBlue()};
    const float d[3] = {ringed.getFloatRed() - base.getFloatRed(), ringed.getFloatGreen() - base.getFloatGreen(),
                        ringed.getFloatBlue() - base.getFloatBlue()};
    float vv = 0.0f, dv = 0.0f;
    for (int i = 0; i < 3; ++i) {
        vv += v[i] * v[i];
        dv += d[i] * v[i];
    }
    return vv <= 1.0e-6f ? 0.0f : juce::jlimit(0.0f, 1.0f, dv / vv);
}

struct RingMeasure {
    juce::Rectangle<int> box; // the ring's pixels
    float thickness[4] = {};  // left, top, right, bottom: the accent coverage summed across the middle of the side
};

// The ring's box and each side's thickness; nullopt when the control paints no ring itself.
std::optional<RingMeasure> measure(const juce::Image& base, const juce::Image& ringed, const Mask& ring,
                                   juce::Colour accent) {
    int left = ring.w, top = ring.h, right = -1, bottom = -1;
    for (int y = 0; y < ring.h; ++y)
        for (int x = 0; x < ring.w; ++x)
            if (ring.at(x, y)) {
                left = juce::jmin(left, x);
                right = juce::jmax(right, x);
                top = juce::jmin(top, y);
                bottom = juce::jmax(bottom, y);
            }
    if (right < 0)
        return std::nullopt;
    RingMeasure m;
    m.box = juce::Rectangle<int>(left, top, right - left + 1, bottom - top + 1);
    const int midX = m.box.getCentreX(), midY = m.box.getCentreY();
    auto cover = [&](int x, int y) { return coverage(base.getPixelAt(x, y), ringed.getPixelAt(x, y), accent); };
    for (int x = left; x <= right && ring.at(x, midY); ++x)
        m.thickness[0] += cover(x, midY);
    for (int y = top; y <= bottom && ring.at(midX, y); ++y)
        m.thickness[1] += cover(midX, y);
    for (int x = right; x >= left && ring.at(x, midY); --x)
        m.thickness[2] += cover(x, midY);
    for (int y = bottom; y >= top && ring.at(midX, y); --y)
        m.thickness[3] += cover(midX, y);
    return m;
}

// Replaces `c`'s text with spaces of the same length (so its layout stays) and returns the way back; empty when `c` has
// no text to take out.
std::function<void()> takeTextOut(juce::Component& c) {
    if (auto* button = dynamic_cast<juce::Button*>(&c)) {
        const auto text = button->getButtonText();
        if (text.isEmpty())
            return nullptr;
        auto blank = juce::String::repeatedString(" ", text.length());
        // A toolbar button centres its content on its caption's width, so the blank keeps that width: blanking it to
        // a narrower string would move the chip and the rest of what the button paints, which is not text.
        if (const auto* toolbarButton = dynamic_cast<synth::ui::ToolbarButton*>(button)) {
            const int want = toolbarButton->getPreferredWidthFor(text);
            int best = 1000000;
            for (int n = 1; n <= 80; ++n) {
                const int off =
                    std::abs(toolbarButton->getPreferredWidthFor(juce::String::repeatedString(" ", n)) - want);
                if (off < best) {
                    best = off;
                    blank = juce::String::repeatedString(" ", n);
                }
            }
        }
        button->setButtonText(blank);
        return [button, text] { button->setButtonText(text); };
    }
    if (auto* label = dynamic_cast<juce::Label*>(&c)) {
        const auto text = label->getText();
        if (text.isEmpty())
            return nullptr;
        label->setText(juce::String::repeatedString(" ", text.length()), juce::dontSendNotification);
        return [label, text] { label->setText(text, juce::dontSendNotification); };
    }
    if (auto* combo = dynamic_cast<juce::ComboBox*>(&c)) {
        for (auto* child : combo->getChildren())
            if (auto* label = dynamic_cast<juce::Label*>(child); label != nullptr && label->getText().isNotEmpty()) {
                const auto text = label->getText();
                label->setText(juce::String::repeatedString(" ", text.length()), juce::dontSendNotification);
                return [label, text] { label->setText(text, juce::dontSendNotification); };
            }
    }
    return nullptr;
}

bool onScreenInside(const juce::Component& c, const juce::Component& root) {
    for (const auto* p = &c; p != nullptr; p = p->getParentComponent()) {
        if (p == &root)
            return true;
        if (!p->isVisible() || p->getAlpha() <= 0.0f || p->getWidth() <= 0 || p->getHeight() <= 0)
            return false;
    }
    return false;
}

// What the sweep did, printed at the end: which stops were measured and which were skipped for which reason.
struct Census {
    size_t measured = 0, textChecked = 0, litMeasured = 0;
    std::map<std::string, std::set<std::string>> skipped; // reason -> classes
    std::map<std::string, size_t> skippedCount;
    void skip(const std::string& reason, const std::string& cls) {
        skipped[reason].insert(cls);
        ++skippedCount[reason];
    }
    void print() const {
        std::cout << "[ focus ring ] " << measured << " stops measured, " << textChecked << " with text checked, "
                  << litMeasured << " measured again lit\n";
        for (const auto& [reason, classes] : skipped) {
            std::cout << "[ focus ring ] skipped (" << skippedCount.at(reason) << "): " << reason << ":";
            for (const auto& cls : classes)
                std::cout << " " << cls;
            std::cout << "\n";
        }
    }
};

const char* const kNoOwnRing =
    "paints no ring itself (its parent or region draws it; ReachSweepTests asserts it shows)";
const char* const kHoldsStops = "contains other stops (its parts are measured on their own)";
const char* const kTooThin = "is a thin handle that lights its whole bar (under 8 px across), not a ring";
const char* const kNoText = "rule 4 only: no text to take out (an icon, a knob, a custom painter)";

// `lit`: the stop is a toggle-able control the sweep turned ON; its ring must still show, with a band of the surface
// between it and the accent fill.
bool checkStop(SweepFindings& f, Census& census, const std::string& surface, juce::Component& stop,
               const synth::theme::Theme& theme, bool lit = false) {
    const auto name = describe(stop) + (lit ? " [on]" : "");
    if (!synth::ui::focusableDescendants(stop).empty()) {
        census.skip(kHoldsStops, className(stop));
        return false;
    }
    if (std::min(stop.getWidth(), stop.getHeight()) < 8) {
        census.skip(kTooThin, className(stop));
        return false;
    }
    const auto base = paintOn(stop, theme.colors.bg0, true);
    juce::Image noText;
    const auto restore = takeTextOut(stop);
    if (restore) {
        noText = paintOn(stop, theme.colors.bg0, true);
        restore();
    }
    juce::Image ringed;
    {
        RingsShown shown;
        ringed = paintOn(stop, theme.colors.bg0);
    }
    const auto ring = difference(base, ringed);
    const auto m = measure(base, ringed, ring, theme.colors.accent);
    if (!m.has_value()) {
        if (lit)
            f.add(surface, name, "its ring is not visible while it is on (the lit fill swallows it)",
                  theme.id.toStdString());
        else
            census.skip(kNoOwnRing, className(stop));
        return false;
    }
    ++census.measured;
    if (lit)
        ++census.litMeasured;

    const float want = synth::ui::focusRingThickness(theme);
    const int w = stop.getWidth(), h = stop.getHeight();
    const auto& box = m->box;
    const std::string numbers = theme.id.toStdString() + ": thickness " + std::to_string(m->thickness[0]) + "/" +
                                std::to_string(m->thickness[1]) + "/" + std::to_string(m->thickness[2]) + "/" +
                                std::to_string(m->thickness[3]) + " want " + std::to_string(want) + ", box " +
                                box.translated(-kMargin, -kMargin).toString().toStdString() + " in " +
                                std::to_string(w) + "x" + std::to_string(h);
    if (verbose())
        std::cout << "[ focus ring ] " << surface << " / " << name << " " << numbers << "\n";

    for (const float t : m->thickness)
        if (std::abs(t - want) > kThicknessTolerance) {
            f.add(surface, name, "its ring is not the shared thickness", numbers);
            break;
        }

    // The control sits at (kMargin, kMargin) in the images: an inset is the ring's outer edge's distance inside the
    // control's bounds, negative where the ring stands off outside them.
    const int insets[4] = {box.getX() - kMargin, box.getY() - kMargin, w + kMargin - box.getRight(),
                           h + kMargin - box.getBottom()};
    const bool flush = *std::max_element(insets, insets + 4) <= kFlushInset;
    bool standOff = true, standOffOutside = true;
    for (const int inset : insets) {
        standOff = standOff && std::abs((float)inset - synth::ui::kFocusRingGap) <= kStandOffTolerance;
        // Outside: the ring's inner edge is the shared fill gap clear of the control's edge.
        standOffOutside =
            standOffOutside && std::abs((float)inset + want + synth::ui::kFocusFillGap) <= kStandOffTolerance;
    }
    if (!flush && !standOff && !standOffOutside)
        f.add(surface, name, "its ring is not flush with the control or at the shared gap", numbers);

    if (std::abs(insets[0] - insets[2]) > 1 || std::abs(insets[1] - insets[3]) > 1)
        f.add(surface, name, "its ring is not centred on the control", numbers);

    if (lit) {
        // In the middle of the band just inside the ring's stroke, at a quarter, the middle and three quarters of each
        // side, the pixel is the surface and not the accent fill. A glyph in the way hides a sample or two, never most.
        const int depth = (int)std::floor(want + synth::ui::kFocusFillGap * 0.5f);
        int hidden = 0, samples = 0;
        for (const float along : {0.25f, 0.5f, 0.75f}) {
            const int x = box.getX() + (int)((float)(box.getWidth() - 1) * along);
            const int y = box.getY() + (int)((float)(box.getHeight() - 1) * along);
            for (const auto& p :
                 {juce::Point<int>(box.getX() + depth, y), juce::Point<int>(x, box.getY() + depth),
                  juce::Point<int>(box.getRight() - 1 - depth, y), juce::Point<int>(x, box.getBottom() - 1 - depth)}) {
                ++samples;
                if (synth::ui::fillHidesFocusRing(ringed.getPixelAt(p.x, p.y), theme))
                    ++hidden;
            }
        }
        if (hidden * 4 >= samples * 3)
            f.add(surface, name, "its lit fill touches its ring (no band of the surface between them)",
                  numbers + ", " + std::to_string(hidden) + " of " + std::to_string(samples) + " samples");
    }

    if (restore) {
        ++census.textChecked;
        // Text is a strong change; blanking a toolbar button's label also re-centres its content, which moves a faint
        // sheen under the ring, so only a clear change counts as ink.
        const auto ink = difference(base, noText, kInkStep);
        int covered = 0;
        for (int y = 0; y < ring.h; ++y)
            for (int x = 0; x < ring.w; ++x)
                if (ring.at(x, y) && ink.at(x, y))
                    ++covered;
        if (covered >= 2)
            f.add(surface, name, "its ring covers its text", numbers + ", " + std::to_string(covered) + " px");
    } else {
        census.skip(kNoText, className(stop));
    }
    return true;
}

// Whether the control draws anything more with every ring shown: a tick box that paints its own ring from real focus
// only does not, and has nothing to lose to a lit fill.
bool paintsOwnRing(juce::Component& stop, const synth::theme::Theme& theme) {
    const auto plain = paintOn(stop, theme.colors.bg0, true);
    RingsShown shown;
    const auto ringed = paintOn(stop, theme.colors.bg0, true);
    for (int y = 0; y < plain.getHeight(); ++y)
        for (int x = 0; x < plain.getWidth(); ++x)
            if (plain.getPixelAt(x, y) != ringed.getPixelAt(x, y))
                return true;
    return false;
}

// Turns a toggle-able button ON (its own state is put back after) and measures its ring again: a control filled with
// the accent when lit must not lose its ring to the fill.
void checkLitStop(SweepFindings& f, Census& census, const std::string& surface, juce::Component& stop,
                  const synth::theme::Theme& theme) {
    auto* button = dynamic_cast<juce::Button*>(&stop);
    if (button == nullptr || !synth::ui::focusableDescendants(stop).empty() || button->getToggleState() ||
        button->getRadioGroupId() != 0)
        return;
    if (!button->getClickingTogglesState() && dynamic_cast<juce::ToggleButton*>(button) == nullptr)
        return;
    // A control that paints no ring of its own (a tick box has its own guard) has nothing to lose to a lit fill.
    if (!paintsOwnRing(stop, theme))
        return;
    button->setToggleState(true, juce::dontSendNotification);
    checkStop(f, census, surface, stop, theme, true);
    button->setToggleState(false, juce::dontSendNotification);
}

void checkSurface(SweepFindings& f, Census& census, const std::string& surface, juce::Component& root,
                  const std::function<bool(juce::Component&)>& owns, const synth::theme::Theme& theme) {
    size_t stops = 0;
    for (auto* stop : synth::ui::focusableDescendants(root)) {
        if (!owns(*stop) || !onScreenInside(*stop, root) || stop->getWidth() < 4 || stop->getHeight() < 4)
            continue;
        ++stops;
        checkStop(f, census, surface, *stop, theme);
        checkLitStop(f, census, surface, *stop, theme);
    }
    f.walked(surface, stops);
}

class FocusRingSweepShell : public KeyboardReachFixture {
protected:
    void SetUp() override {
        MainComponentTest::SetUp(); // the shell is built below with a look and feel this test owns
        resetGuard_.emplace();
        previousDefault_ = &juce::LookAndFeel::getDefaultLookAndFeel();
        lookAndFeel_.applyTheme(synth::theme::builtInThemes().front());
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);
        mc_ = std::make_unique<MainComponent>(themeManager_, lookAndFeel_, std::make_unique<MockProvider>());
        mc_->setSize(1400, 900);
        mc_->newPatchForTest();
        synth::ui::FadeVisibility::setAnimateOffScreenForTest(false);
        mc_->simulateAddAudioTrackClick();
        mc_->getTimelineDoc().addTrack(synth::TrackKind::Midi, "Track 1");
        rebuildMixer();
        mc_->resized();
    }
    void TearDown() override {
        mc_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(previousDefault_);
        KeyboardReachFixture::TearDown();
    }

    void applyTheme(const synth::theme::Theme& theme) {
        lookAndFeel_.applyTheme(theme);
        mc_->sendLookAndFeelChange();
        mc_->resized();
    }

    void checkRegions(SweepFindings& f, Census& census, const std::string& suffix, const synth::theme::Theme& theme) {
        const auto& registry = mc_->getFocusRegionsForTest();
        for (const auto& region : openRegions()) {
            const auto id = region.id;
            checkSurface(
                f, census, (region.id + suffix).toStdString(), *region.root,
                [&registry, id](juce::Component& c) {
                    const auto* owner = registry.regionContaining(&c);
                    return owner != nullptr && owner->id == id;
                },
                theme);
        }
    }

    // Each main view in turn, then the timeline with a piano roll open on a clip, then with a drum machine.
    void checkScenes(SweepFindings& f, Census& census, const synth::theme::Theme& theme) {
        const auto firstTrack = mc_->getTimelineDoc().getTracks().front().id;
        mc_->getTimelinePanel().selectTracks({firstTrack}, firstTrack);
        mc_->getTimelinePanel().toggleSidePane(true);
        mc_->simulateToggleModMatrixClick();
        for (const Tab view : {Tab::Canvas, Tab::Timeline, Tab::Mixer, Tab::MidiRemote}) {
            mc_->setMainViewForTest(view);
            checkRegions(f, census, "", theme);
        }
        auto& panel = mc_->getTimelinePanel();
        mc_->setMainViewForTest(Tab::Timeline);
        const auto clip = mc_->getTimelineDoc().addClip(firstTrack, 0.0, 8.0, "Clip");
        panel.openPianoRoll(clip);
        panel.getPianoRoll().toggleScalePanel();
        mc_->resized();
        checkRegions(f, census, " [piano roll open]", theme);
        panel.closePianoRoll();
        panel.openDrumMachine(clip);
        mc_->resized();
        checkRegions(f, census, " [drum machine open]", theme);
        panel.closeDrumMachine();
        mc_->resized();
    }

    synth::theme::ThemeManager themeManager_;
    synth::theme::AppLookAndFeel lookAndFeel_;
    juce::LookAndFeel* previousDefault_ = nullptr;
};

} // namespace

TEST_F(FocusRingSweepShell, EveryFocusableControlOfEveryRegionHasTheSharedRingInEveryTheme) {
    SweepFindings findings("focus ring", kKnownGaps);
    Census census;
    for (const auto& theme : synth::theme::builtInThemes()) {
        applyTheme(theme);
        checkScenes(findings, census, theme);
    }
    census.print();
    EXPECT_GT(census.measured, 20u) << "the sweep is not seeing the app's controls";
    EXPECT_GT(census.textChecked, 5u) << "the text check is not seeing any text";
    EXPECT_GT(census.litMeasured, 3u) << "the lit check is not seeing any toggle-able control";
    findings.expectOnlyKnownGaps();
}

class FocusRingSweepDialogs : public KeyboardReachDialogFixture {};

TEST_F(FocusRingSweepDialogs, EveryFocusableControlOfEveryDialogHasTheSharedRingInEveryTheme) {
    SweepFindings findings("focus ring", kKnownGaps);
    Census census;
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open) {
            std::cout << "[ focus ring ] skipped dialog " << surface.name << ": " << surface.exemptReason << "\n";
            continue;
        }
        for (const auto& theme : synth::theme::builtInThemes()) {
            SweepEnvironment env(theme);
            auto* window = surface.open(env);
            ASSERT_NE(window, nullptr) << surface.name;
            synth::test::flushMessageQueue();
            checkSurface(findings, census, surface.name, *window, [](juce::Component&) { return true; }, theme);
        }
    }
    census.print();
    findings.expectOnlyKnownGaps();
}
