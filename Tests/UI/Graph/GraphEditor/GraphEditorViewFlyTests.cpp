// GraphEditor::flyViewTo: every way of showing something on the canvas flies the view there instead of jumping
// (docs/layout/animation.md#motion-rules). A registry of the entry points, each driven through its real call on
// a real off-screen MainComponent, read at the first frame, a middle one and the last; then the same under Reduce
// Motion and Animations Off, and cancelled by the user's own pan and zoom.
#include "../../../TestSettingsHelpers.h"
#include "../../SourceScan.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/BottomDockComponent/BottomDockComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <functional>
#include <gtest/gtest.h>

namespace {

class MockProviderVFT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockVFT"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

struct ModeGuardVFT {
    ~ModeGuardVFT() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

struct FlyRig {
    synth::test::PersistedKeysGuard guard{{"bottomDockVisible", "bottomDockActiveTab", "bottomDockTabOrder"}};
    ModeGuardVFT modes;
    MainComponent mc{std::make_unique<MockProviderVFT>()};

    FlyRig() {
        mc.setSize(1400, 900);
        mc.getBottomDock().getMixerPanel().setVisible(true);
        mc.getAudioEngine().suspendDeviceCallback();
        mc.newPatchForTest();
        mc.simulateAddAudioTrackClick();
        mc.simulateAddAudioTrackClick();
        panel().rebuild();
        panel().setSize(1400, 700);
        panel().resized();
        editor().setSize(1000, 700);
        editor().viewFlyForTest().setAnimateOffScreenForTest(true);
    }

    synth::ui::MixerPanelComponent& panel() { return mc.getBottomDock().getMixerPanel(); }
    GraphEditor& editor() { return mc.getGraphEditor(); }
    juce::String firstInsertUuid() { return panel().getStripColumnForTest(0)->getInsertList().getRowUuid(0); }
    juce::Point<float> viewCentre() { return editor().getVisibleCanvasRect().getCentre(); }
    void sendViewFar() { editor().centreViewOn({9000.0f, 9000.0f}); }
};

struct Entry {
    const char* name;
    std::function<void(FlyRig&)> arm;     // before the view is sent away: selection and folds
    std::function<void(FlyRig&)> trigger; // the real call
};

std::vector<Entry> entries() {
    return {
        {"mixer column Edit on canvas", nullptr,
         [](FlyRig& r) {
             // The header button's own callback, as the panel wires it.
             r.panel().getStripColumnForTest(0)->onEditOnCanvas(r.firstInsertUuid());
         }},
        {"mixer insert row (module)", nullptr, [](FlyRig& r) { r.panel().revealModuleOnCanvas(r.firstInsertUuid()); }},
        {"mixer insert row inside a collapsed macro",
         [](FlyRig& r) {
             const auto* macro = r.editor().getMacros().findByMember(r.firstInsertUuid());
             ASSERT_NE(macro, nullptr);
             r.editor().getMacroController().setMacroCollapsed(macro->id, true);
         },
         [](FlyRig& r) { r.panel().revealModuleOnCanvas(r.firstInsertUuid()); }},
        {"Go to Output", nullptr, [](FlyRig& r) { r.editor().locateMasterOrOutput(); }},
        {"keyboard step to the next module",
         [](FlyRig& r) {
             const auto* macro = r.editor().getMacros().findByMember(r.firstInsertUuid());
             ASSERT_NE(macro, nullptr);
             r.editor().getMacroController().setMacroCollapsed(macro->id, false);
             ASSERT_GT(r.editor().getModuleComponents().size(), 0);
             r.editor().selectModule(r.editor().getModuleComponents()[0]->getNodeId(), false);
         },
         [](FlyRig& r) { r.editor().selectAdjacentModule(1); }},
    };
}

float dist(juce::Point<float> a, juce::Point<float> b) { return a.getDistanceFrom(b); }

juce::MouseEvent eventOn(juce::Component& comp) {
    const juce::Point<float> p(10.0f, 10.0f);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(), 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), p,
                            juce::Time::getCurrentTime(), 1, false);
}

// Runs one entry from a far view; returns where the view ends up with the animation played out.
juce::Point<float> landing(FlyRig& r, const Entry& e) {
    r.sendViewFar();
    e.trigger(r);
    r.editor().viewFlyForTest().stepForTest(2000.0);
    return r.viewCentre();
}

} // namespace

TEST(ViewFlyRegistry, EveryEntryPointStartsAtTheOldViewPassesBetweenAndLandsOnTheTarget) {
    for (const auto& e : entries()) {
        SCOPED_TRACE(e.name);
        FlyRig r;
        synth::ui::setReducedMotionForTest(false);
        if (e.arm)
            e.arm(r);
        const auto target = landing(r, e);
        ASSERT_GT(dist(target, {9000.0f, 9000.0f}), 500.0f) << "the view travelled";

        if (e.arm)
            e.arm(r);
        r.sendViewFar();
        const auto start = r.viewCentre();
        e.trigger(r);
        EXPECT_LT(dist(r.viewCentre(), start), 1.0f) << "first frame: still the old view";
        EXPECT_TRUE(r.editor().viewFlyForTest().isActive());
        r.editor().viewFlyForTest().stepForTest(100.0);
        const auto mid = r.viewCentre();
        EXPECT_GT(dist(mid, start), 5.0f) << "a middle frame has moved";
        EXPECT_GT(dist(mid, target), 5.0f) << "and has not arrived";
        r.editor().viewFlyForTest().stepForTest(500.0);
        EXPECT_LT(dist(r.viewCentre(), target), 1.5f) << "last frame: on the target";
    }
}

// Regression: the view is stored in floats, so a fly by a fractional distance moves by a rounded delta. The tween read
// that as a clamp, ended the axis on its first frame and left the view a quarter of the way there in the real app.
TEST(ViewFlyRegistry, AFlyByAFractionalDistanceLandsExactlyOnTheTarget) {
    FlyRig r;
    synth::ui::setReducedMotionForTest(false);
    r.editor().centreViewOn({9000.37f, 8123.61f});
    const juce::Point<float> target{647.3f, 343.7f};
    r.editor().flyViewTo(target);
    r.editor().viewFlyForTest().stepForTest(100.0);
    EXPECT_GT(dist(r.viewCentre(), target), 50.0f) << "mid flight";
    r.editor().viewFlyForTest().stepForTest(600.0);
    EXPECT_LT(dist(r.viewCentre(), target), 0.05f) << "the whole way, not a clamp's first frame";
}

TEST(ViewFlyRegistry, UnderReduceMotionAndAnimationsOffEveryEntryPointLandsAtOnce) {
    for (const auto mode : {synth::ui::AnimationMode::reduced, synth::ui::AnimationMode::off}) {
        for (const auto& e : entries()) {
            SCOPED_TRACE(e.name);
            FlyRig r;
            synth::ui::setReducedMotionForTest(false);
            if (e.arm)
                e.arm(r);
            const auto target = landing(r, e);
            synth::ui::setAnimationMode(mode);
            if (e.arm)
                e.arm(r);
            r.sendViewFar();
            e.trigger(r);
            EXPECT_FALSE(r.editor().viewFlyForTest().isActive());
            EXPECT_LT(dist(r.viewCentre(), target), 1.5f) << "on the target in the same call";
        }
    }
}

TEST(ViewFlyRegistry, AUserPanOrZoomMidFlightCancelsTheFly) {
    FlyRig r;
    synth::ui::setReducedMotionForTest(false);
    const auto e = entries()[1];
    const auto target = landing(r, e);

    // A pan: a trackpad swipe applies at once and the fly lets go.
    r.sendViewFar();
    e.trigger(r);
    r.editor().viewFlyForTest().stepForTest(50.0);
    juce::MouseWheelDetails swipe{};
    swipe.deltaX = 0.5f;
    swipe.isSmooth = true;
    r.editor().mouseWheelMove(eventOn(r.editor()), swipe);
    const auto afterPan = r.viewCentre();
    r.editor().viewFlyForTest().stepForTest(600.0);
    EXPECT_LT(dist(r.viewCentre(), afterPan), 1.0f) << "the fly does not pull the view back";
    EXPECT_GT(dist(r.viewCentre(), target), 200.0f);

    // A zoom.
    r.sendViewFar();
    e.trigger(r);
    r.editor().viewFlyForTest().stepForTest(50.0);
    r.editor().zoomAroundCentre(1.0f);
    const auto afterZoom = r.viewCentre();
    r.editor().viewFlyForTest().stepForTest(600.0);
    EXPECT_LT(dist(r.viewCentre(), afterZoom), 1.0f);
}

TEST(ViewFlyRegistry, TheMinimapStaysInstantWhileDragging) {
    FlyRig r;
    synth::ui::setReducedMotionForTest(false);
    r.editor().getMinimap().onNavigate({1234.0f, 567.0f});
    EXPECT_FALSE(r.editor().viewFlyForTest().isActive());
    EXPECT_LT(dist(r.viewCentre(), {1234.0f, 567.0f}), 1.0f);
}

// Every `centreViewOn` call is the minimap's drag or the fly's own landing; anything else showing something on the
// canvas goes through flyViewTo. A new caller picks one, and this names it.
TEST(ViewFlyRegistry, NoNavigationCallerJumpsTheViewWithCentreViewOn) {
    const auto scanned = synth::test::source_scan::countCallSites(std::regex(R"(\bcentreViewOn\s*\()"), "");
    const std::map<std::string, int> registered = {
        {"Source/UI/Graph/GraphEditor/GraphEditor.h", 1},         // the declaration
        {"Source/UI/Graph/GraphEditor/GraphEditor.cpp", 1},       // the minimap drag: instant
        {"Source/UI/Graph/GraphEditor/GraphEditorCanvas.cpp", 1}, // the definition
        {"Source/UI/Graph/GraphEditor/GraphEditorViewFly.cpp", 2} // the landing when nothing animates
    };
    synth::test::source_scan::expectCountsMatch("centreViewOn call sites", scanned, registered,
                                                "GraphEditorViewFlyTests.cpp");
}
