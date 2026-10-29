// ThemeSweepTests.cpp -- every dialog, window, alert, module card and the main shell renders an opaque, themed
// background and readable text in all four built-in themes. Dialog and window call sites are discovered by scanning
// Source (a new one that is not in ThemeSweepSurfaces.cpp fails), module cards by walking the module factory.
// Findings that are known and ticketed live in kKnownGaps; the list may only shrink (docs/layout/theming.md).
#include "../../../App/MainComponent/MainComponentTestFixture.h"
#include "../../Layout/NativeWindowTestGuard.h"
#include "ThemeSweepSupport.h"
#include "ThemeSweepSurfaces.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>

using namespace synth::test::theme_sweep;
using synth::theme::Theme;

namespace {

// A finding that is known and has a ticket. Key: "<surface> / <theme id> / <what>" as the failure message prints it.
struct KnownGap {
    const char* key;
    const char* ticket;
};

const std::vector<KnownGap> kKnownGaps = {
    // Shape editor buttons
    {"Shape editor / daylight / Button 'Delete'", "FRO1307"},
    {"Shape editor / daylight / Button 'Off'", "FRO1307"},
    {"Shape editor / obsidian / Button 'Delete'", "FRO1307"},
    {"Shape editor / warm / Button 'Delete'", "FRO1307"},
    // Neon alert box is translucent
    {"Alert box / neon / probe 0 not opaque", "FRO1164"},
    {"Alert box / neon / probe 1 not opaque", "FRO1164"},
    {"Alert box / neon / probe 2 not opaque", "FRO1164"},
    {"Alert box / neon / probe 3 not opaque", "FRO1164"},
    // Daylight on-state TextButton: page colour on accent
    {"Card: ADSR / daylight / Button 'Time'", "FRO1307"},
    {"Card: Amp Env / daylight / Button 'Time'", "FRO1307"},
    {"Card: Delay / daylight / Button 'Time'", "FRO1307"},
    {"Card: Distortion / daylight / Button 'Soft'", "FRO1307"},
    {"Card: Drum Kit / daylight / Button 'Kick'", "FRO1307"},
    {"Card: Envelope Follower / daylight / Button 'Peak'", "FRO1307"},
    {"Card: Filter Env / daylight / Button 'Time'", "FRO1307"},
    {"Card: LFO / daylight / Button 'Sync'", "FRO1307"},
    {"Card: Mallet / daylight / Button 'Bar'", "FRO1307"},
    {"Card: Math / daylight / Button 'Off'", "FRO1307"},
    {"Card: Noise / daylight / Button 'White'", "FRO1307"},
    {"Card: Oscillator / daylight / Button 'Sine'", "FRO1307"},
    {"Card: Pitch Shifter / daylight / Button 'Pitch'", "FRO1307"},
    {"Card: Pluck / daylight / Button 'String'", "FRO1307"},
    {"Card: Sample & Hold / daylight / Button 'Internal'", "FRO1307"},
    {"Card: Sample & Hold / daylight / Button 'Random'", "FRO1307"},
    {"Card: Sample & Hold / daylight / Button 'Sample'", "FRO1307"},
    {"Card: Sampler / daylight / Button 'Sample'", "FRO1307"},
    {"Card: Wavetable / daylight / Button 'Tune'", "FRO1307"},
};

// ---------------------------------------------------------------------------------------------------------------------
// Findings: every check that failed, keyed so it can be compared with kKnownGaps.
// ---------------------------------------------------------------------------------------------------------------------

class Findings {
public:
    void add(const std::string& surface, const juce::String& themeId, const std::string& what,
             const std::string& detail) {
        found_[surface + " / " + themeId.toStdString() + " / " + what] = detail;
    }

    // An unlisted finding fails; a listed one that no longer shows up fails too (so the list only shrinks).
    // `tested` names the surfaces this run covered: a known gap on any other surface is not judged here.
    void expectOnlyKnownGaps(const std::set<std::string>& tested) const {
        std::set<std::string> listed;
        for (const auto& gap : kKnownGaps) {
            listed.insert(gap.key);
            const std::string key = gap.key;
            if (!tested.count(key.substr(0, key.find(" / "))))
                continue;
            if (!found_.count(key))
                ADD_FAILURE() << "fixed - remove it from kKnownGaps: " << key << " (" << gap.ticket << ")";
        }
        for (const auto& [key, detail] : found_)
            if (!listed.count(key))
                ADD_FAILURE() << key << ": " << detail;
    }

private:
    std::map<std::string, std::string> found_;
};

// ---------------------------------------------------------------------------------------------------------------------
// The checks shared by every surface.
// ---------------------------------------------------------------------------------------------------------------------

using Probes = std::vector<juce::Point<int>>;

// A window's own backing, checked inside its four corners.
// `inset` clears a rounded corner (the alert box).
Probes cornerProbes(int w, int h, int inset = 2) {
    return {{inset, inset}, {w - 1 - inset, inset}, {inset, h - 1 - inset}, {w - 1 - inset, h - 1 - inset}};
}

bool isJuceDefaultWindowColour(juce::Colour c) {
    static const juce::LookAndFeel_V4 v4;
    static const juce::LookAndFeel_V2 v2;
    return c == juce::Colours::lightgrey || c == juce::Colours::darkgrey ||
           c == v4.findColour(juce::ResizableWindow::backgroundColourId) ||
           c == v2.findColour(juce::ResizableWindow::backgroundColourId);
}

// Probe pixels per surface per theme, kept to compare the themes with one another afterwards.
class ProbeLog {
public:
    void record(const std::string& surface, const juce::String& themeId, std::vector<juce::Colour> pixels) {
        pixels_[surface][themeId.toStdString()] = std::move(pixels);
    }

    // The same probe must differ between Daylight and every other theme, or the theme never reached it.
    void expectThemed(Findings& findings) const {
        for (const auto& [surface, byTheme] : pixels_) {
            const auto daylight = byTheme.find("daylight");
            if (daylight == byTheme.end())
                continue;
            for (const auto& [id, pixels] : byTheme)
                for (size_t i = 0; id != "daylight" && i < pixels.size(); ++i)
                    if (pixels[i] == daylight->second[i])
                        findings.add(surface, "daylight", "probe " + std::to_string(i) + " same as " + id,
                                     "hard-coded colour: " + pixels[i].toDisplayString(true).toStdString());
        }
    }

private:
    std::map<std::string, std::map<std::string, std::vector<juce::Colour>>> pixels_;
};

void checkSurface(Findings& findings, ProbeLog& log, const std::string& surface, const Theme& theme,
                  juce::Component& root, const Probes& probes, const std::vector<TextSample>& extraText = {},
                  bool requireOpaque = true) {
    const auto image = renderToImage(root);
    writeSweepPng(surface, theme.id, image);

    std::vector<juce::Colour> pixels;
    for (size_t i = 0; i < probes.size(); ++i) {
        auto pixel = image.getPixelAt(probes[i].x, probes[i].y);
        // A card is glass over the canvas by design: it is judged over the page colour it sits on.
        if (!requireOpaque)
            pixel = theme.colors.bg0.withAlpha(1.0f).overlaidWith(pixel);
        pixels.push_back(pixel);
        if (pixel.getAlpha() != 255)
            findings.add(surface, theme.id, "probe " + std::to_string(i) + " not opaque",
                         "alpha " + std::to_string(pixel.getAlpha()));
        else if (isJuceDefaultWindowColour(pixel))
            findings.add(surface, theme.id, "probe " + std::to_string(i) + " is JUCE's default window colour",
                         pixel.toDisplayString(false).toStdString());
    }
    log.record(surface, theme.id, std::move(pixels));

    auto samples = collectTextSamples(root, image, synth::theme::windowBackgroundFor(theme));
    samples.insert(samples.end(), extraText.begin(), extraText.end());
    for (const auto& sample : samples)
        if (sample.ratio < kMinTextContrast)
            findings.add(surface, theme.id, sample.description.toStdString(),
                         juce::String(sample.ratio, 2).toStdString() + ":1, text " +
                             sample.text.toDisplayString(true).toStdString() + " on " +
                             sample.background.toDisplayString(false).toStdString());
}

// ---------------------------------------------------------------------------------------------------------------------
// Scanning Source for call sites.
// ---------------------------------------------------------------------------------------------------------------------

juce::File sourceDir() { return juce::File(TESTS_ROOT_DIR).getParentDirectory().getChildFile("Source"); }

// `text` without its comments (string literals are left alone).
std::string stripComments(const std::string& text) {
    std::string out;
    bool inString = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            out += c;
            if (c == '\\' && i + 1 < text.size())
                out += text[++i];
            else if (c == '"')
                inString = false;
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n')
                ++i;
            out += '\n';
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'))
                ++i;
            ++i;
        } else {
            out += c;
            inString = c == '"';
        }
    }
    return out;
}

// Occurrences of `pattern` per Source file (repo-relative path), comments excluded.
std::map<std::string, int> countCallSites(const std::regex& pattern, const std::string& skipPrefix) {
    std::map<std::string, int> counts;
    juce::Array<juce::File> files;
    sourceDir().findChildFiles(files, juce::File::findFiles, true, "*.cpp;*.h;*.mm");
    for (const auto& file : files) {
        const auto relative =
            "Source/" + file.getRelativePathFrom(sourceDir()).replaceCharacter('\\', '/').toStdString();
        if (!skipPrefix.empty() && relative.rfind(skipPrefix, 0) == 0)
            continue;
        const auto text = stripComments(file.loadFileAsString().toStdString());
        const auto n =
            (int)std::distance(std::sregex_iterator(text.begin(), text.end(), pattern), std::sregex_iterator());
        if (n > 0)
            counts[relative] = n;
    }
    return counts;
}

void expectRegistered(const char* what, const std::map<std::string, int>& scanned,
                      const std::map<std::string, int>& registered) {
    std::set<std::string> files;
    for (const auto& [file, n] : scanned)
        files.insert(file);
    for (const auto& [file, n] : registered)
        files.insert(file);
    for (const auto& file : files) {
        const int found = scanned.count(file) ? scanned.at(file) : 0;
        const int listed = registered.count(file) ? registered.at(file) : 0;
        if (found > listed)
            ADD_FAILURE() << "not registered: " << file << " (" << found << " " << what << " call sites, " << listed
                          << " registered) - add it to ThemeSweepSurfaces.cpp";
        else if (found < listed)
            ADD_FAILURE() << "stale: " << file << " has " << found << " " << what << " call sites but " << listed
                          << " are registered - remove the extra from ThemeSweepSurfaces.cpp";
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------------------------------------------------

// Windows that open for real need a display (and not the ASAN job's Xvfb).
class ThemeSweepWindows : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        wasEnabled_ = synth::ui::PopupMotion::isEnabled();
        synth::ui::PopupMotion::setEnabled(false); // a dialog shows at once, fully opaque
    }
    void TearDown() override {
        synth::ui::deleteOpenModalDialogs();
        synth::ui::PopupMotion::setEnabled(wasEnabled_);
    }
    bool wasEnabled_ = true;
};

class ThemeSweepShell : public MainComponentTest {};

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------------------------------------------------

TEST(ThemeSweep, EveryDialogCallSiteIsRegistered) {
    std::map<std::string, int> dialogs, windows;
    for (const auto& s : dialogSurfaces()) {
        ++dialogs[s.sourceFile];
        EXPECT_NE(s.open != nullptr, !s.exemptReason.empty()) << s.name << ": either an opener or an exemption reason";
    }
    for (const auto& s : windowSurfaces()) {
        ++windows[s.sourceFile];
        EXPECT_NE(s.swept, !s.exemptReason.empty()) << s.name << ": either swept or an exemption reason";
    }
    expectRegistered("PopupMotion::launchDialog",
                     countCallSites(std::regex(R"(PopupMotion::launchDialog\s*\()"), "Source/UI/Layout/PopupMotion."),
                     dialogs);
    expectRegistered("DocumentWindow", countCallSites(std::regex(R"(\bDocumentWindow\s*\()"), ""), windows);
}

// ---------------------------------------------------------------------------------------------------------------------
// Dialogs, windows, alerts
// ---------------------------------------------------------------------------------------------------------------------

TEST_F(ThemeSweepWindows, DialogsAreOpaqueAndReadableInEveryTheme) {
    Findings findings;
    ProbeLog log;
    std::set<std::string> tested;
    for (const auto& surface : dialogSurfaces()) {
        if (!surface.open)
            continue;
        tested.insert(surface.name);
        for (const auto& theme : synth::theme::builtInThemes()) {
            SweepEnvironment env(theme);
            auto* window = surface.open(env);
            if (window == nullptr) {
                findings.add(surface.name, theme.id, "did not open", "the opener returned no window");
                continue;
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
            if (window->getBackgroundColour() != synth::theme::windowBackgroundFor(theme))
                findings.add(surface.name, theme.id, "dialog background colour",
                             window->getBackgroundColour().toDisplayString(true).toStdString() + ", not the theme's");
            checkSurface(findings, log, surface.name, theme, *window,
                         cornerProbes(window->getWidth(), window->getHeight()));
        }
    }
    log.expectThemed(findings);
    findings.expectOnlyKnownGaps(tested);
}

TEST(ThemeSweepWindowsHeadless, DetachedWindowsAreOpaqueAndReadableInEveryTheme) {
    Findings findings;
    ProbeLog log;
    for (const auto& theme : synth::theme::builtInThemes()) {
        SweepEnvironment env(theme);
        auto& window = buildDetachedPanelWindow(env);
        checkSurface(findings, log, "Detached panel", theme, window,
                     cornerProbes(window.getWidth(), window.getHeight()));
    }
    log.expectThemed(findings);
    findings.expectOnlyKnownGaps({"Detached panel"});
}

TEST(ThemeSweepWindowsHeadless, AlertWindowsAreOpaqueAndReadableInEveryTheme) {
    Findings findings;
    ProbeLog log;
    for (const auto& theme : synth::theme::builtInThemes()) {
        SweepEnvironment env(theme);
        auto alert = buildAlertWindow(env);
        ASSERT_NE(alert, nullptr);
        // The title and message are drawn by the look and feel, not by child controls: sample them by hand.
        const auto background = alert->getLocalBounds().getCentre();
        const auto image = renderToImage(*alert);
        TextSample message;
        message.description = "Alert message";
        message.text = alert->findColour(juce::AlertWindow::textColourId);
        message.background = image.getPixelAt(8, background.y);
        message.ratio = contrastRatio(message.text, message.background);
        checkSurface(findings, log, "Alert box", theme, *alert, cornerProbes(alert->getWidth(), alert->getHeight(), 12),
                     {message});
    }
    log.expectThemed(findings);
    findings.expectOnlyKnownGaps({"Alert box"});
}

// ---------------------------------------------------------------------------------------------------------------------
// Module cards and the main shell
// ---------------------------------------------------------------------------------------------------------------------

namespace {

// Inside the card body, clear of its rounded corners and of the controls.
Probes cardProbes(int w, int h) { return {{w / 2, 3}, {6, h / 2}, {w - 7, h / 2}, {w / 2, h - 4}}; }

} // namespace

TEST(ThemeSweepCards, EveryModuleCardIsOpaqueAndReadableInEveryTheme) {
    Findings findings;
    ProbeLog log;
    std::set<std::string> tested;
    const auto names = moduleTypeNames();
    ASSERT_FALSE(names.empty());
    juce::MessageManager::getInstance();
    for (const auto& type : names) {
        std::string whyNot;
        auto rig = buildCard(type, whyNot);
        if (rig == nullptr) {
            ADD_FAILURE() << type << ": cannot be built (" << whyNot << ") - give it an exemption in the registry";
            continue;
        }
        const auto surface = "Card: " + type;
        tested.insert(surface);
        for (const auto& theme : synth::theme::builtInThemes()) {
            SweepEnvironment env(theme);
            auto& card = rig->card();
            card.setLookAndFeel(&env.lookAndFeel());
            checkSurface(findings, log, surface, theme, card, cardProbes(card.getWidth(), card.getHeight()), {}, false);
            card.setLookAndFeel(nullptr);
        }
    }
    log.expectThemed(findings);
    findings.expectOnlyKnownGaps(tested);
}

TEST_F(ThemeSweepShell, TheMainShellIsOpaqueAndReadableInEveryTheme) {
    Findings findings;
    ProbeLog log;
    juce::MessageManager::getInstance();
    for (const auto& theme : synth::theme::builtInThemes()) {
        SweepEnvironment env(theme);
        synth::theme::ThemeManager themeManager;
        auto shell = std::make_unique<MainComponent>(themeManager, env.lookAndFeel(), std::make_unique<MockProvider>());
        env.lookAndFeel().applyTheme(theme); // the constructor applied the saved theme; this run wants its own
        shell->setSize(1400, 900);
        shell->sendLookAndFeelChange();
        shell->newPatchForTest();
        checkSurface(findings, log, "Main shell", theme, *shell, cornerProbes(1400, 900));
        shell.reset();
    }
    log.expectThemed(findings);
    findings.expectOnlyKnownGaps({"Main shell"});
}
