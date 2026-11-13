// PopupHost.cpp -- opens a popover (launchPopover, PopoverSlot) and relays the app's shortcuts out of it.
#include "PopupHost.h"
#include "TestMode/AutomationHiding.h"
#include "UI/Layout/AppShortcutRelay.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/DialogTooltip.h"
#include <map>

namespace synth::ui {

namespace {

class CallOutShortcutRelay;
std::map<const juce::Component*, CallOutShortcutRelay*>& relays() {
    static std::map<const juce::Component*, CallOutShortcutRelay*> map;
    return map;
}

/** Owned by nobody: deletes itself when the box it listens to is deleted. */
class CallOutShortcutRelay
    : public juce::KeyListener
    , private juce::ComponentListener {
public:
    CallOutShortcutRelay(juce::CallOutBox& box, juce::Component* anchor, bool closeOnUndo)
        : relay_(makeAppShortcutRelay(
              anchor, [safeBox = juce::Component::SafePointer<juce::CallOutBox>(&box), closeOnUndo](bool afterwards) {
                  // The content of the box tells itself what an undo or redo may change under it.
                  if (safeBox != nullptr && safeBox->getNumChildComponents() > 0)
                      if (auto* aware = dynamic_cast<AppUndoAware*>(safeBox->getChildComponent(0))) {
                          if (afterwards)
                              aware->appUndoneOrRedone();
                          else
                              aware->appAboutToUndoOrRedo();
                      } else if (afterwards && closeOnUndo)
                          closeHostingWindow(*safeBox->getChildComponent(0));
              })) {
        box.addKeyListener(this);
        box.addComponentListener(this);
        relays()[&box] = this;
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override { return relay_ && relay_(key); }

private:
    void componentBeingDeleted(juce::Component& component) override {
        component.removeKeyListener(this);
        component.removeComponentListener(this);
        relays().erase(&component);
        delete this;
    }

    std::function<bool(const juce::KeyPress&)> relay_;
};

juce::Point<int> pointerPosition() {
    const auto& forced = test_hooks::popoverMouseForTest();
    return forced.has_value() ? *forced : juce::Desktop::getMousePosition();
}

// The call-out every popover opens in. Escape and a press outside are decided here, once, for every content:
//   - Escape (from the box, or from a text field inside it) lets the content put things back, then fades the live
//     window out, and the keyboard goes back to the trigger. A second Escape while it fades does nothing.
//   - A press on the trigger or on the area the box points at closes it through the fade and is consumed: the box stays
//     modal while it fades, so the press never reaches the button and re-opens the popover.
//   - A press anywhere else closes it the way JUCE does, so that press still lands where it was aimed; the leaving
//     picture fades it. Click-away commits; only Escape cancels.
//   - It is its own native window, so the main window's tooltip window draws its tips under it: the box hosts a tooltip
//     window of its own (a child, deleted before the box), and every control in any popover gets its tip, a greyed
//     one's reason included.
class PopoverBox final
    : public juce::CallOutBox
    , private juce::FocusChangeListener {
public:
    PopoverBox(juce::Component& content, juce::Rectangle<int> area, PopoverOptions options)
        : juce::CallOutBox(content, area, nullptr)
        , content_(content)
        , lastContentBounds_(content.getBounds())
        , area_(area)
        , options_(std::move(options))
        , triggerToFocus_(options_.trigger) {
        // The look and feel has already attached the call-out with the shared style from its constructor.
        auto style = options_.style;
        if (growsFromOpener()) {
            style = popup_motion::growStyle();
            style.body = [this]() -> juce::Component* { return &content_; };
            style.bodyPivot = [this] { return growPivot(); };
        }
        if (!style.anchor)
            style.anchor = [area] { return area.getCentre(); };
        PopupMotion::setStyle(*this, std::move(style));
        tooltip_ = std::make_unique<AppTooltipWindow>(this, dialog_tooltip::appProperties());
        tooltip_->setComponentID("popoverTooltip"); // test mode reads and screenshots the tip by this id
        juce::Desktop::getInstance().addFocusChangeListener(this);
    }

    ~PopoverBox() override {
        tooltip_.reset(); // the tip and its fading ghost go while the box's own window is still whole
        juce::Desktop::getInstance().removeFocusChangeListener(this);
        for (auto& editor : labelEditors_)
            if (editor != nullptr)
                editor->removeKeyListener(&labelEscape_);
        // Escape puts the keyboard back where the user was: on the button that opened the popover.
        if (closedByEscape_ && triggerToFocus_ != nullptr && triggerToFocus_->getWantsKeyboardFocus() &&
            triggerToFocus_->isShowing())
            triggerToFocus_->grabKeyboardFocus();
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key.isKeyCode(juce::KeyPress::escapeKey)) {
            escape();
            return true;
        }
        return juce::CallOutBox::keyPressed(key);
    }

    // The background, arrow and shadow are painted here, not by the content the motion scales: they follow its scale.
    void paint(juce::Graphics& g) override {
        g.addTransform(content_.getTransform());
        juce::CallOutBox::paint(g);
    }

    // The motion's scale reaches the box as a child change. JUCE would re-place the window around the scaled content
    // on every frame (and keep the size of the first, 40% frame); only a real change of the content's size or place
    // does that, measured from where the call-out first put it. The scaled bubble also covers more
    // than the content, so the whole box repaints.
    void childBoundsChanged(juce::Component* child) override {
        if (child == &content_ && content_.getBounds() == lastContentBounds_) {
            repaint();
            return;
        }
        lastContentBounds_ = content_.getBounds();
        juce::CallOutBox::childBoundsChanged(child);
    }

    void inputAttemptWhenModal() override {
        if (options_.holdGestureDown && options_.holdGestureDown())
            return;
        if (PopupMotion::isDismissing(*this))
            return; // consumed: the box is already leaving
        const auto mouse = pointerPosition();
        const bool onTrigger = options_.trigger != nullptr && options_.trigger->getScreenBounds().contains(mouse);
        if (onTrigger || area_.contains(mouse)) {
            PopupMotion::dismissCallOut(*this);
            return;
        }
        exitModalState(0);
        setVisible(false);
    }

private:
    bool growsFromOpener() const {
        const auto* content = dynamic_cast<const PopoverContent*>(&content_);
        return content != nullptr && content->growsFromOpener();
    }

    // The middle of what the box points at, in the box's own space (the space the content's transform is in).
    juce::Point<float> growPivot() const {
        const auto local = getLocalPoint(nullptr, area_.getCentre());
        return getLocalBounds().getConstrainedPoint(local).toFloat();
    }

    void escape() {
        if (PopupMotion::isDismissing(*this))
            return;
        if (auto* cancellable = dynamic_cast<PopoverContent*>(&content_))
            cancellable->onPopoverCancel();
        closedByEscape_ = true;
        PopupMotion::dismissCallOut(*this);
    }

    // A juce::TextEditor swallows Escape; sent up instead, it reaches this box. An editor with its own Escape stays.
    // A label's editor (a slider's value box) cannot bubble: the label deletes the editor on Escape before the editor's
    // own hook runs. A key listener sees the key first, so the box closes at the same moment the label cancels its
    // edit.
    void letEscapeReachBoxFrom(juce::Component& root) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(&root); editor != nullptr && !editor->onEscapeKey) {
            if (dynamic_cast<juce::Label*>(editor->getParentComponent()) != nullptr) {
                editor->addKeyListener(&labelEscape_);
                labelEditors_.add(editor);
            } else {
                bubbleEscapeToParents(*editor);
            }
        }
        for (auto* child : root.getChildren())
            letEscapeReachBoxFrom(*child);
    }

    void visibilityChanged() override {
        if (isVisible())
            letEscapeReachBoxFrom(content_);
    }

    // Editors made later (a label's editor) take focus when they appear.
    void globalFocusChanged(juce::Component* focused) override {
        if (focused != nullptr && isParentOf(focused))
            letEscapeReachBoxFrom(*focused);
    }

    struct LabelEscape final : juce::KeyListener {
        explicit LabelEscape(PopoverBox& boxToClose)
            : box(boxToClose) {}
        bool keyPressed(const juce::KeyPress& key, juce::Component*) override {
            if (key.isKeyCode(juce::KeyPress::escapeKey))
                box.escape();
            return false; // the label still cancels its own edit
        }
        PopoverBox& box;
    };

    juce::Component& content_;
    std::unique_ptr<AppTooltipWindow> tooltip_;
    juce::Rectangle<int> lastContentBounds_;
    juce::Rectangle<int> area_;
    PopoverOptions options_;
    LabelEscape labelEscape_{*this};
    juce::Array<juce::Component::SafePointer<juce::TextEditor>> labelEditors_;
    juce::Component::SafePointer<juce::Component> triggerToFocus_;
    bool closedByEscape_ = false;
};

// Owns the content and the box; JUCE's modal manager deletes this callback (and with it both) once the box leaves
// modal state. Mirrors juce::CallOutBox::launchAsynchronously, which keeps its own copy private.
class PopoverLaunch final
    : public juce::ModalComponentManager::Callback
    , private juce::Timer {
public:
    PopoverLaunch(std::unique_ptr<juce::Component> content, juce::Rectangle<int> area, PopoverOptions options)
        : content_(std::move(content))
        , box_(*content_, area, std::move(options)) {
        box_.setVisible(true);
        box_.enterModalState(true, this);
        startTimer(200);
    }

    void modalStateFinished(int) override {}

    // A popover must not outlive the app being in front; it leaves through the same fade as every other close.
    void timerCallback() override {
        if (!synth::testmode::appIsInFront())
            PopupMotion::dismissCallOut(box_);
    }

    PopoverBox& box() { return box_; }

private:
    std::unique_ptr<juce::Component> content_;
    PopoverBox box_;
};

} // namespace

void relayAppShortcutsFromCallOut(juce::CallOutBox& box, juce::Component* anchor, bool closeOnUndo) {
    new CallOutShortcutRelay(box, anchor, closeOnUndo);
}

namespace test_hooks {
bool offerKeyToPopoverForTest(juce::CallOutBox& box, const juce::KeyPress& key) {
    const auto it = relays().find(&box);
    return it != relays().end() && it->second->keyPressed(key, &box);
}

PopoverLauncher& popoverLauncherForTest() {
    static PopoverLauncher launcher;
    return launcher;
}

std::optional<juce::Point<int>>& popoverMouseForTest() {
    static std::optional<juce::Point<int>> mouse;
    return mouse;
}
} // namespace test_hooks

juce::CallOutBox* launchPopover(std::unique_ptr<juce::Component> content, juce::Rectangle<int> area,
                                juce::Component* anchor, PopoverOptions options) {
    jassert(content != nullptr);
    juce::CallOutBox* box = nullptr;
    const bool closeOnUndo = options.closeOnUndo;
    if (auto& launcher = test_hooks::popoverLauncherForTest())
        box = launcher(std::move(content), area);
    else if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        return nullptr; // a CallOutBox places itself on a display and crashes without one (a headless test run)
    else
        box = &(new PopoverLaunch(std::move(content), area, std::move(options)))->box();
    if (box != nullptr && anchor != nullptr)
        relayAppShortcutsFromCallOut(*box, anchor, closeOnUndo);
    return box;
}

PopoverSlot::Result PopoverSlot::open(const std::function<std::unique_ptr<juce::Component>()>& makeContent,
                                      juce::Rectangle<int> area, juce::Component* anchor, PopoverOptions options) {
    if (isOpen()) {
        if (box_ != nullptr && PopupMotion::isDismissing(*box_))
            return Result::Ignored;
        close();
        return Result::ClosedIt;
    }
    auto content = makeContent();
    if (content == nullptr)
        return Result::Ignored;
    const bool contentKeptByTest = static_cast<bool>(test_hooks::popoverLauncherForTest());
    content_ = content.get();
    box_ = launchPopover(std::move(content), area, anchor, std::move(options));
    if (box_ == nullptr && !contentKeptByTest) {
        content_ = nullptr; // nothing opened, and the content is gone with the launch
        return Result::Ignored;
    }
    return Result::Opened;
}

bool PopoverSlot::isOpen() const {
    if (box_ != nullptr)
        return box_->isVisible();
    return content_ != nullptr; // a test launcher that kept the content and made no box
}

void PopoverSlot::close() {
    if (box_ != nullptr)
        PopupMotion::dismissCallOut(*box_);
    else if (content_ != nullptr)
        closeHostingWindow(*content_);
}

} // namespace synth::ui
