#pragma once

// KeyboardSweepShell.h -- what the sweeps that press real keys on a real MainComponent share
// (KeyStandardsSweepTests.cpp, ReachSweepTests.cpp): a main window on the desktop with the app's look and feel (so keys
// go through the window's peer and rings are painted as in the app), how a control is named in a finding, and the list
// of known gaps that may only shrink.
#include "../../App/MainComponent/MainComponentTestFixture.h"
#include "../../TestWait.h"
#include "KeyboardReachFixture.h"
#include "NativeWindowTestGuard.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/ThemeManager.h"
#if defined(__GNUG__)
#include <cxxabi.h>
#endif
#include <gtest/gtest.h>
#include <iostream>
#include <set>
#include <string>
#include <typeinfo>
#include <vector>

namespace synth::test::keyboard_sweep {

// A gap that is known and has a ticket. Key: "<surface> / <control>: <what>" as a finding prints it.
// `varies`: a gap seen on some machines only (screen size, timing); a listed one that is not found is not judged.
struct KnownGap {
    const char* key;
    const char* ticket;
    bool varies = false;
};

inline std::string className(const juce::Component& c) {
#if defined(__GNUG__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(typeid(c).name(), nullptr, nullptr, &status);
    std::string name = status == 0 && demangled != nullptr ? demangled : typeid(c).name();
    std::free(demangled);
#else
    std::string name = typeid(c).name();
    for (const char* prefix : {"class ", "struct "})
        if (name.rfind(prefix, 0) == 0)
            name = name.substr(std::char_traits<char>::length(prefix));
#endif
    const auto colons = name.rfind("::");
    return colons == std::string::npos ? name : name.substr(colons + 2);
}

// A control is named by its class and title, button text or component id, with digits written as '#'.
inline std::string describe(juce::Component& c) {
    juce::String name = c.getTitle();
    if (name.isEmpty())
        name = c.getComponentID();
    if (name.isEmpty())
        if (auto* button = dynamic_cast<juce::Button*>(&c))
            name = button->getButtonText();
    juce::String clean;
    for (auto ch : name.substring(0, 28))
        clean += juce::CharacterFunctions::isDigit(ch) ? juce::juce_wchar('#') : ch;
    return className(c) + (clean.isEmpty() ? "" : " '" + clean.toStdString() + "'");
}

inline void closeMenus() {
    juce::PopupMenu::dismissAllActiveMenus();
    synth::test::flushMessageQueue();
}

// Collects the findings of a run, then compares them with the known gaps.
class SweepFindings {
public:
    SweepFindings(const char* label, const std::vector<KnownGap>& gaps)
        : label_(label)
        , gaps_(gaps) {}

    // `detail` (who took the key, which key) is printed but is not part of the key, so a known gap keeps its text.
    void add(const std::string& surface, const std::string& control, const std::string& what,
             const std::string& detail = {}) {
        const auto key = surface + " / " + control + ": " + what;
        if (found_.insert(key).second && !detail.empty())
            std::cout << "[ " << label_ << " ] " << key << " (" << detail << ")\n";
    }
    void walked(const std::string& surface, size_t controls) {
        walked_.insert(surface);
        controls_ += controls;
    }
    void unfocusable(const std::string& name) { unfocusable_.insert(name); }

    // An unlisted finding fails; a listed gap that is no longer found fails too, so the list only shrinks. A gap on a
    // surface this run did not walk is not judged.
    void expectOnlyKnownGaps() const {
        std::cout << "[ " << label_ << " ] " << walked_.size() << " surfaces, " << controls_ << " controls, "
                  << unfocusable_.size() << " could not take focus here, " << found_.size() << " findings\n";
        for (const auto& name : unfocusable_)
            std::cout << "[ " << label_ << " ] could not take focus: " << name << "\n";
        std::set<std::string> listed;
        for (const auto& gap : gaps_) {
            const std::string key = gap.key;
            listed.insert(key);
            if (!gap.varies && walked_.count(key.substr(0, key.find(" / "))) != 0 && found_.count(key) == 0)
                ADD_FAILURE() << "fixed - remove it from kKnownGaps: " << key << " (" << gap.ticket << ")";
        }
        for (const auto& key : found_)
            if (listed.count(key) == 0)
                ADD_FAILURE() << key;
    }

private:
    std::string label_;
    const std::vector<KnownGap>& gaps_;
    std::set<std::string> found_;
    std::set<std::string> walked_;
    std::set<std::string> unfocusable_;
    size_t controls_ = 0;
};

// A real MainComponent on the desktop with a track on the timeline and the mixer built. Skips without a display.
class KeyboardDesktopShell : public KeyboardReachFixture {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        MainComponentTest::SetUp();
        resetGuard_.emplace();
        popupMotionWas_ = synth::ui::PopupMotion::isEnabled();
        synth::ui::PopupMotion::setEnabled(false);
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
        mc_->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        mc_->setVisible(true);
        mc_->resized();
    }
    void TearDown() override {
        if (mc_ == nullptr)
            return;
        closeMenus();
        mc_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(previousDefault_);
        synth::ui::PopupMotion::setEnabled(popupMotionWas_);
        KeyboardReachFixture::TearDown();
    }

    synth::theme::ThemeManager themeManager_;
    synth::theme::AppLookAndFeel lookAndFeel_;
    juce::LookAndFeel* previousDefault_ = nullptr;
    bool popupMotionWas_ = true;
};

} // namespace synth::test::keyboard_sweep
