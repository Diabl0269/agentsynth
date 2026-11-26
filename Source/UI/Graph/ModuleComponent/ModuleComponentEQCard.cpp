// ModuleComponentEQCard.cpp -- the Parametric EQ card: its layout geometry (namespace eqCard): the curve with its two
// icon toggles (Show Spectrum, Open EQ Window) in its top right corner, the band strip, then the Main lane (the
// selected band's Freq, Gain and Q) and the Out lane (Mix, Pan, Output), both in ModuleComponentEQBandRow.cpp; the
// pop-out EQ window, and the undo-gesture wiring for its curve editor.
// ModuleComponent is declared in ModuleComponent.h; the rest of its implementation lives in the
// sibling ModuleComponent*.cpp units next to this one.
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "ModuleComponentEQBandRow.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/CardBody/CardHeightGlide.h"
#include "UI/Graph/CardWidgets/CardIconToggle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/AppShortcutRelay.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/EQWindow.h"

namespace eqCard {
constexpr int kMargin = 12;
constexpr int kCurveHeight = 150;
constexpr int kZoneGap = 8;   // between the picture zone (curve, strip) and the lanes
constexpr int kIconInset = 3; // the icon toggles' distance from the curve's top and right edges
constexpr int kIconGap = 4;
constexpr int kNumVisibleInputs = 6;

// getPortCenter puts input i at y = 38 (header) + 20 (MIDI-out offset) + i*20 + 20, and each
// label is drawn 10px above its jack with height 20 — so the last one ends 10px below its centre.
constexpr int kPortFirstY = 78;
constexpr int kPortStep = 20;
constexpr int kPortLabelBottom = kPortFirstY + (kNumVisibleInputs - 1) * kPortStep + 16;

// The port labels only occupy a narrow gutter down each edge (inputs are drawn at x+10 with
// width 60 from a jack at x=10; outputs mirror that), so the curve sits BESIDE them starting
// just under the header rather than below the whole stack. On a six-input module that reclaims
// ~125px of otherwise dead space at the top of the card.
constexpr int kCurveTop = 60;
constexpr int kPortGutter = 88;
constexpr int kCurveBottom = kCurveTop + kCurveHeight;
constexpr int kContentTop = (kCurveBottom > kPortLabelBottom ? kCurveBottom : kPortLabelBottom) + 8;

// A fresh EQ (every band off) shows only the + chip, one row.
constexpr int kRestingStripHeight = 22;
} // namespace eqCard

int ModuleComponent::parametricEQHeight() const {
    using namespace eqCard;
    // kContentTop already accounts for the curve, which is laid out beside the port labels. Below the strip come
    // the two lanes, a fixed height whichever band is selected, so a selection never resizes the card.
    int h = kContentTop;
    h += (eqBand_ != nullptr ? eqBand_->stripHeight(getWidth() - kMargin * 2) : kRestingStripHeight) + kZoneGap;
    h += EqBandRow::kLaneZoneHeight;
    return h + kMargin; // bottom margin
}

void ModuleComponent::layoutParametricEQ() {
    using namespace eqCard;

    const int contentW = getWidth() - kMargin * 2;
    if (contentW <= 0)
        return;

    // The curve occupies the space between the input and output port-label gutters, level with
    // the jacks rather than below them.
    if (eqCurveComponent) {
        const int curveW = getWidth() - kPortGutter * 2;
        if (curveW > 0)
            eqCurveComponent->setBounds(kPortGutter, kCurveTop, curveW, kCurveHeight);
        // Show Spectrum and Open EQ Window: small icon toggles in the curve's top right corner.
        constexpr int kSize = synth::ui::CardIconToggle::kSize;
        const int right = kPortGutter + curveW - kIconInset;
        if (eqPopOutButton)
            eqPopOutButton->setBounds(right - kSize, kCurveTop + kIconInset, kSize, kSize);
        if (spectrumToggle)
            spectrumToggle->setBounds(right - 2 * kSize - kIconGap, kCurveTop + kIconInset, kSize, kSize);
    }

    int y = kContentTop;
    if (eqBand_ != nullptr) {
        y += eqBand_->layoutStrip(kMargin, y, contentW) + kZoneGap;
        eqBand_->layoutLanes(kMargin, y, contentW);
    }
}

// A Parametric EQ lane stop's reading-order place: the lanes' top-left and its rank (Main's controls column first,
// then Out's Mix, Pan, Output). Nullopt for any other component.
std::optional<std::pair<juce::Point<int>, int>> ModuleComponent::eqLaneStop(const juce::Component* stop) const {
    if (eqBand_ == nullptr)
        return std::nullopt;
    const int rank = eqBand_->laneRankOf(stop);
    if (rank < 0)
        return std::nullopt;
    return std::make_pair(eqBand_->lanesOrigin(), rank);
}

void ModuleComponent::wireEqGestureCallbacks(EQCurveComponent& curve) {
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    auto capture = [safeThis](bool isStart) {
        if (safeThis == nullptr || safeThis->undoManager == nullptr || safeThis->module == nullptr)
            return;
        auto& graph = safeThis->owner.getAudioEngine().getGraph();
        if (isStart)
            safeThis->undoManager->captureBeforeState(graph);
        else
            safeThis->undoManager->pushSnapshotFromCapture(graph);
    };
    curve.setUndoManager(undoManager); // an undo or redo glides the dots and the curve
    curve.onGestureStart = [capture] { capture(true); };
    curve.onGestureEnd = [capture] { capture(false); };
}

void ModuleComponent::openEqWindow() {
    if (eqWindow != nullptr) {
        eqWindow->toFront(true);
        return;
    }

    auto* eqMod = dynamic_cast<ParametricEQModule*>(module);
    if (eqMod == nullptr)
        return;

    // A host with no display (a headless runner) cannot open a native window: JUCE's X11 code ends the process there.
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        return;

    auto content = std::make_unique<EQWindow>(*eqMod);
    juce::Component::SafePointer<ModuleComponent> safeThis(this);
    content->setGestureCallbacks(
        [safeThis] {
            if (safeThis != nullptr && safeThis->undoManager != nullptr && safeThis->module != nullptr)
                safeThis->undoManager->captureBeforeState(safeThis->owner.getAudioEngine().getGraph());
        },
        [safeThis] {
            if (safeThis != nullptr && safeThis->undoManager != nullptr && safeThis->module != nullptr)
                safeThis->undoManager->pushSnapshotFromCapture(safeThis->owner.getAudioEngine().getGraph());
        });

    content->onAppShortcut = synth::ui::makeAppShortcutRelay(this);
    content->setUndoManager(undoManager);
    content->setShortcutManager(owner.getCardKeyboard().getShortcutManager());

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content.release());
    options.dialogTitle = module->getName();
    options.componentToCentreAround = getTopLevelComponent();
    options.useNativeTitleBar = true;
    options.resizable = true;
    eqWindow = synth::ui::PopupMotion::launchDialog(options);
}

void ModuleComponent::driveEqKnobGlideForTest(bool force, double stepMs) {
    if (eqBand_ == nullptr)
        return;
    eqBand_->glideForTest().forceAnimateForTest(force);
    if (stepMs > 0.0) {
        eqBand_->glideForTest().stepForTest(stepMs);
        eqBand_->stepLaneSlideForTest(stepMs);
        heightGlideForTest().stepForTest(stepMs);
    }
}
