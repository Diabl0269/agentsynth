// SamplerMapWindow.cpp -- the instrument map window and its grow-out-of-the-card motion, the same as the shape
// editor's.
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapWindow.h"
#include "UI/Layout/PopupMotion.h"

namespace synth::ui {

namespace {

constexpr float kInStartScale = 0.82f;
constexpr float kOutEndScale = 0.9f;
constexpr double kInMs = 220.0;
constexpr double kOutMs = 150.0;
constexpr float kAlphaInFraction = 0.6f;
constexpr int kMinWidth = 620;
constexpr int kMinHeight = 380;

juce::Point<int> centreOf(const juce::Component::SafePointer<juce::Component>& anchor,
                          const juce::Component::SafePointer<juce::DialogWindow>& window) {
    if (anchor != nullptr)
        return anchor->localAreaToGlobal(anchor->getLocalBounds()).getCentre();
    return window != nullptr ? window->getScreenBounds().getCentre() : juce::Point<int>();
}

// Grows out of the card's centre: the content scales from 82% about that point with a soft overshoot while the window
// fades in, and shrinks back toward it on the way out. The window itself does not slide (the scale already moves it).
popup_motion::Style motionStyle(juce::Component::SafePointer<juce::Component> anchor,
                                juce::Component::SafePointer<juce::DialogWindow> window) {
    popup_motion::Style style;
    style.inSlidePx = 0.0f;
    style.outSlidePx = 0.0f;
    style.overshoot = true;
    style.inMs = kInMs;
    style.outMs = kOutMs;
    style.alphaInFraction = kAlphaInFraction;
    style.inStartScale = kInStartScale;
    style.outEndScale = kOutEndScale;
    style.anchor = [anchor, window] { return centreOf(anchor, window); };
    style.body = [window]() -> juce::Component* { return window != nullptr ? window->getContentComponent() : nullptr; };
    style.bodyPivot = [anchor, window]() -> juce::Point<float> {
        if (window == nullptr || window->getContentComponent() == nullptr)
            return {};
        return (centreOf(anchor, window) - window->getContentComponent()->getScreenPosition()).toFloat();
    };
    return style;
}

} // namespace

namespace test_hooks {
std::function<void(std::unique_ptr<SamplerMapView>)>& samplerMapWindowHookForTest() {
    static std::function<void(std::unique_ptr<SamplerMapView>)> hook;
    return hook;
}
} // namespace test_hooks

juce::DialogWindow* openSamplerMapWindow(std::unique_ptr<SamplerMapView> view, juce::Component* anchor) {
    view->setSize(SamplerMapView::kWidth, SamplerMapView::kHeight);
    if (auto& hook = test_hooks::samplerMapWindowHookForTest()) {
        hook(std::move(view));
        return nullptr;
    }
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = view->getWindowTitle();
    options.content.setOwned(view.release());
    options.componentToCentreAround = anchor != nullptr ? anchor->getTopLevelComponent() : nullptr;
    options.useNativeTitleBar = true;
    options.resizable = true;
    // The limits are set before the motion starts, so its first frame already has the final size.
    return PopupMotion::launchDialog(options, [anchor](juce::DialogWindow& window) {
        window.setResizeLimits(kMinWidth, kMinHeight, 4096, 4096);
        return motionStyle(anchor, &window);
    });
}

} // namespace synth::ui
