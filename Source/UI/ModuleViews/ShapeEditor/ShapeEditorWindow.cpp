// ShapeEditorWindow.cpp -- the shape editor's window and its grow-out-of-the-card motion.
#include "ShapeEditorWindow.h"
#include "UI/Layout/AppShortcutRelay.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Library/SoundBrowser/SoundDragState.h"

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

/** The shape editor's window. It is not modal, so a shape can be dragged onto it from the Sounds view; it owns
    itself and is deleted one turn after it closes (Esc, Cmd+W, Done, the title bar's close button), by the card that
    opened it (MorphShapesCard::detach), or by deleteOpenModalDialogs() on quit. */
class ShapeEditorDialog final
    : public juce::DialogWindow
    , private SoundDragState::Listener
    , private juce::ComponentListener {
public:
    ShapeEditorDialog(juce::DialogWindow::LaunchOptions& options)
        : juce::DialogWindow(
              options.dialogTitle, options.dialogBackgroundColour, false, true,
              options.componentToCentreAround != nullptr
                  ? juce::Component::getApproximateScaleFactorForComponent(options.componentToCentreAround)
                  : 1.0f) {
        setContentOwned(options.content.release(), true);
        centreAroundComponent(options.componentToCentreAround, getWidth(), getHeight());
        setResizable(options.resizable, options.useBottomRightCornerResizer);
        setUsingNativeTitleBar(options.useNativeTitleBar);
        SoundDragState::addListener(this);
    }

    ~ShapeEditorDialog() override {
        stopWatchingWindows();
        SoundDragState::removeListener(this);
    }

    /** The title bar's button fades the window out first; a close that already faded it (closeHostingWindow calls
        this from its fade's end) hides it at once. */
    void closeButtonPressed() override {
        if (PopupMotion::isDismissing(*this)) {
            finishClose();
            return;
        }
        PopupMotion::dismiss(*this, [safe = juce::Component::SafePointer<ShapeEditorDialog>(this)] {
            if (safe != nullptr)
                safe->finishClose();
        });
    }

private:
    // Deleting now would pull the window out from under the key or click handler that is closing it.
    void finishClose() {
        if (closing_)
            return;
        closing_ = true;
        setVisible(false);
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<ShapeEditorDialog>(this)] { delete safe.getComponent(); });
    }

    // A drag hit-tests the windows in the order they were last brought forward, and a press in the main window to
    // start the drag brings that one forward under this one: raised again (without the keyboard), a drop over this
    // window lands here. The container also hides its drag image just before it looks for the drop target, and the OS
    // then activates the main window again; every window brought forward from the drag's start until the release has
    // finished is put back under this one.
    void soundDragChanged() override {
        if (SoundDragState::isShapeActive() && isVisible() && !closing_) {
            watchOtherWindows();
            toFront(false);
        } else if (watching_ && !SoundDragState::isShapeActive()) {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ShapeEditorDialog>(this)] {
                if (safe != nullptr)
                    safe->stopWatchingWindows();
            });
        }
    }

    void componentBroughtToFront(juce::Component& other) override {
        if (watching_ && &other != this && isVisible() && !closing_)
            toFront(false);
    }

    void watchOtherWindows() {
        if (watching_)
            return;
        watching_ = true;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = desktop.getComponent(i); window != nullptr && window != this) {
                window->addComponentListener(this);
                watched_.add(window);
            }
    }

    void stopWatchingWindows() {
        watching_ = false;
        for (auto& window : watched_)
            if (window != nullptr)
                window->removeComponentListener(this);
        watched_.clear();
    }

    juce::Array<juce::Component::SafePointer<juce::Component>> watched_;
    bool watching_ = false;
    bool closing_ = false;
};
} // namespace

namespace test_hooks {
std::function<void(std::unique_ptr<ShapeListEditor>)>& shapeEditorWindowHookForTest() {
    static std::function<void(std::unique_ptr<ShapeListEditor>)> hook;
    return hook;
}
} // namespace test_hooks

void focusShapeEditorWindow(juce::DialogWindow& window) {
    window.toFront(true);
    if (auto* editor = dynamic_cast<ShapeListEditor*>(window.getContentComponent()))
        editor->focusFirstControl();
}

juce::DialogWindow* openShapeEditorWindow(std::shared_ptr<ShapeListAdapter> adapter, int selected,
                                          juce::Component* anchor,
                                          std::function<void(MorphMap::Axis, juce::Rectangle<int>)> onStripMenu) {
    auto content = std::make_unique<ShapeListEditor>(std::move(adapter), selected);
    content->onStripMenu = std::move(onStripMenu);
    content->onAppShortcut = makeAppShortcutRelay(anchor);
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
    popup_motion::applyThemedBackground(options);
    auto* dialog = PopupMotion::launchNonModalDialog(
        *new ShapeEditorDialog(options), anchor,
        [anchor](juce::DialogWindow& window) { return motionStyle(anchor, &window); });
    // A window that has only just been launched may not be showing yet, and focus can only land on a showing
    // component, so the focus is given one turn later (the window may also be gone by then).
    juce::MessageManager::callAsync([window = juce::Component::SafePointer<juce::DialogWindow>(dialog)] {
        if (window != nullptr)
            focusShapeEditorWindow(*window);
    });
    return dialog;
}

} // namespace synth::ui
