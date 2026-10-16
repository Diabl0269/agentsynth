// ShapeEditorWindow.cpp -- the shape editor's window and its grow-out-of-the-card motion.
#include "ShapeEditorWindow.h"
#include "UI/Layout/PopupMotion.h"

namespace synth::ui {

namespace {
constexpr float kInStartScale = 0.82f;
constexpr float kOutEndScale = 0.9f;
constexpr double kInMs = 220.0;
constexpr double kOutMs = 150.0;

/** Grows out of the card's centre: the content scales from 82% about that point with a soft overshoot while the
    window fades in, and shrinks back toward it on the way out. The window itself does not slide (the scale already
    moves it). */
popup_motion::Style motionStyle(juce::Component::SafePointer<juce::Component> anchor,
                                juce::Component::SafePointer<juce::DialogWindow> window) {
    popup_motion::Style style;
    style.inSlidePx = 0.0f;
    style.outSlidePx = 0.0f;
    style.overshoot = true;
    style.inMs = kInMs;
    style.outMs = kOutMs;
    style.alphaInFraction = 0.6f;
    style.inStartScale = kInStartScale;
    style.outEndScale = kOutEndScale;
    style.anchor = [anchor, window]() -> juce::Point<int> {
        if (anchor != nullptr)
            return anchor->localAreaToGlobal(anchor->getLocalBounds()).getCentre();
        return window != nullptr ? window->getScreenBounds().getCentre() : juce::Point<int>();
    };
    style.body = [window]() -> juce::Component* { return window != nullptr ? window->getContentComponent() : nullptr; };
    style.bodyPivot = [anchor, window]() -> juce::Point<float> {
        if (window == nullptr)
            return {};
        const auto centre = anchor != nullptr ? anchor->localAreaToGlobal(anchor->getLocalBounds()).getCentre()
                                              : window->getScreenBounds().getCentre();
        return (centre - window->getContentComponent()->getScreenPosition()).toFloat();
    };
    return style;
}
} // namespace

namespace test_hooks {
std::function<void(std::unique_ptr<ShapeListEditor>)>& shapeEditorWindowHookForTest() {
    static std::function<void(std::unique_ptr<ShapeListEditor>)> hook;
    return hook;
}
} // namespace test_hooks

juce::DialogWindow* openShapeEditorWindow(std::shared_ptr<ShapeListAdapter> adapter, int selected,
                                          juce::Component* anchor) {
    auto content = std::make_unique<ShapeListEditor>(std::move(adapter), selected);
    if (auto& hook = test_hooks::shapeEditorWindowHookForTest()) {
        hook(std::move(content));
        return nullptr;
    }
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = content->getWindowTitle();
    options.content.setOwned(content.release());
    options.componentToCentreAround = anchor != nullptr ? anchor->getTopLevelComponent() : nullptr;
    options.useNativeTitleBar = true;
    options.resizable = true;
    auto* dialog = options.launchAsync();
    if (dialog != nullptr)
        PopupMotion::attach(*dialog, motionStyle(anchor, dialog));
    return dialog;
}

} // namespace synth::ui
