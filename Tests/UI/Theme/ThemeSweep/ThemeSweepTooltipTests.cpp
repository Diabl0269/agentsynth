// ThemeSweepTooltipTests.cpp -- bug kind long-tooltip: a hover tip is a name and a few keys, never a how-to. Walks
// every component of the theme sweep's surfaces (every module card, every dialog) and fails a tooltip over kMaxTipChars
// that is not in the named allow-list. The full key lists live in the module help (docs/modules) and the accessible
// description. An earlier fix shortened one tip and built no guard; the same kind came back on the curve editor and the
// EQ curve and built this one (docs/layout/animation.md#tooltips).
#include "../../Layout/NativeWindowTestGuard.h"
#include "ThemeSweepSupport.h"
#include "ThemeSweepSurfaces.h"
#include "UI/Layout/PopupMotion.h"
#include <gtest/gtest.h>
#include <set>
#include <string>

using namespace synth::test::theme_sweep;

namespace {

constexpr int kMaxTipChars = 90;

// A tooltip that is allowed to run longer, matched by its opening words. Each is an explanation of one control or a
// live readout, found when the guard was built; the key and gesture how-tos (the curve editor's, the EQ
// curve's) were shortened instead. This list only shrinks: never add to it, shorten the tip and move the words to the
// module help. A tracker ticket shortens these.
constexpr const char* kExplains = "an explanation of one control or a live readout that predates the guard";
struct AllowedTip {
    const char* tipStart;
    const char* why;
};

const std::vector<AllowedTip> kAllowed = {
    {"Band ", kExplains},
    {"Body. How much the instrument's bo", kExplains},
    {"Choose a saved or factory shape fr", kExplains},
    {"Controllers: click an arrow or pre", kExplains},
    {"Key strip: the keys each recording", kExplains},
    {"Kick plays when this note arrives:", kExplains},
    {"Lanes: choose which lanes show und", kExplains},
    {"Load an audio file as a wavetable,", kExplains},
    {"No track plays this Drum Kit. Add ", kExplains},
    {"Octave: click an arrow or press Up", kExplains},
    {"Phase. Where each new note starts ", kExplains},
    {"Rand Phase. How far each unison vo", kExplains},
    {"Rand. How far each unison voice's ", kExplains},
    {"Rate. Follow notes: plays the note", kExplains},
    {"Resonance Mode. Peak: Resonance li", kExplains},
    {"Resonator. How much the instrument", kExplains},
    {"Shape 1 - drag to move, double-cli", kExplains},
    {"Shape of the selected dot: drag it", kExplains},
    {"Show Kick on the card, in the clip", kExplains},
    {"Show Response: Draw the filter's c", kExplains},
    {"Show Spectrum: Draw the filter's o", kExplains},
    {"Step grid: click a pad to turn a s", kExplains},
    {"Steps: click an arrow or press Up ", kExplains},
    {"Tune snaps to this key's notes, 0 ", kExplains},
    {"Wavetable display: the frame the o", kExplains},
    {"Double-click the curve to add or r",
     "the EQ window hint: it names each key as the user has bound it (EQShortcuts test)"},
    {"Draw the filter's output behind th", kExplains},
    {"Extra render time after the range e", kExplains},
    {"Grid: what the curve snaps to acro", kExplains},
    {"Instrument map: the arrow keys mov", kExplains},
    {"Key map: click a key to pick the s", kExplains},
    {"Notes move the cursor: each note c", kExplains},
    {"One click places dots at the harmo", kExplains},
    {"Pick a pen (Free, Line, Sine, Tria", kExplains},
    {"Root: harmonics are counted from t", kExplains},
    {"Show the labelled reference lines ", kExplains},
    {"Snap mode: Time and level. Which w", kExplains},
    {"Snap to grid: points land on the g", kExplains},
    {"/", "a file path shown as the destination, not a sentence"},
    {"C:\\", "a file path shown as the destination, not a sentence"},
};

void collect(juce::Component& component, const std::string& path,
             std::vector<std::pair<std::string, juce::String>>& out) {
    for (auto* child : component.getChildren()) {
        const auto name = child->getTitle().isNotEmpty() ? child->getTitle() : child->getName();
        const auto childPath = path + " > " + (name.isNotEmpty() ? name : juce::String("<unnamed>")).toStdString();
        if (auto* client = dynamic_cast<juce::TooltipClient*>(child)) {
            const auto tip = client->getTooltip();
            if (tip.length() > kMaxTipChars)
                out.emplace_back(childPath, tip);
        }
        collect(*child, childPath, out);
    }
}

bool allowed(const juce::String& tip, std::set<std::string>& used) {
    for (const auto& entry : kAllowed)
        if (tip.startsWith(entry.tipStart)) {
            used.insert(entry.tipStart);
            return true;
        }
    return false;
}

class ThemeSweepTooltips : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        wasEnabled_ = synth::ui::PopupMotion::isEnabled();
        synth::ui::PopupMotion::setEnabled(false);
    }
    void TearDown() override {
        synth::ui::deleteOpenModalDialogs();
        synth::ui::PopupMotion::setEnabled(wasEnabled_);
    }
    bool wasEnabled_ = true;
};

} // namespace

// A tooltip does not depend on the theme, so one theme is enough.
TEST(ThemeSweepTooltipsCards, NoCardComponentHasATooltipOverTheCap) {
    juce::MessageManager::getInstance();
    std::vector<std::pair<std::string, juce::String>> found;
    std::set<std::string> used;
    const auto names = moduleTypeNames();
    ASSERT_FALSE(names.empty());
    for (const auto& type : names) {
        std::string whyNot;
        auto rig = buildCard(type, whyNot);
        if (rig == nullptr) {
            ADD_FAILURE() << type << ": cannot be built (" << whyNot << ")";
            continue;
        }
        std::vector<std::pair<std::string, juce::String>> over;
        collect(rig->card(), "Card: " + type, over);
        for (auto& [path, tip] : over)
            if (!allowed(tip, used))
                found.emplace_back(path, tip);
    }
    for (const auto& [path, tip] : found)
        ADD_FAILURE() << path << ": a " << tip.length() << " character tooltip (cap " << kMaxTipChars << "): " << tip
                      << "\nShorten it to a name and two or three keys; the rest goes in the module help.";
}

TEST_F(ThemeSweepTooltips, NoDialogComponentHasATooltipOverTheCap) {
    std::vector<std::pair<std::string, juce::String>> found;
    std::set<std::string> used;
    int opened = 0;
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open)
            continue;
        SweepEnvironment env(sweepThemes().front());
        auto* window = surface.open(env);
        if (window == nullptr)
            continue;
        ++opened;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        std::vector<std::pair<std::string, juce::String>> over;
        collect(*window, "Dialog: " + surface.name, over);
        for (auto& [path, tip] : over)
            if (!allowed(tip, used))
                found.emplace_back(path, tip);
    }
    EXPECT_GT(opened, 0);
    for (const auto& [path, tip] : found)
        ADD_FAILURE() << path << ": a " << tip.length() << " character tooltip (cap " << kMaxTipChars << "): " << tip
                      << "\nShorten it to a name and two or three keys; the rest goes in the module help.";
}

TEST(ThemeSweepTooltipsAllowList, EveryAllowedTipNamesItsReason) {
    for (const auto& entry : kAllowed)
        EXPECT_GT(std::string(entry.why).size(), 10u) << entry.tipStart;
}
