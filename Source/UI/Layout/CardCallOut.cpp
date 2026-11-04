// CardCallOut.cpp -- opens a card's popover and relays the app's shortcuts out of it.
#include "CardCallOut.h"
#include "UI/Layout/CalloutReveal.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorAppShortcuts.h"
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

} // namespace

void relayAppShortcutsFromCallOut(juce::CallOutBox& box, juce::Component* anchor, bool closeOnUndo) {
    new CallOutShortcutRelay(box, anchor, closeOnUndo);
}

namespace test_hooks {
bool offerKeyToCardCallOutForTest(juce::CallOutBox& box, const juce::KeyPress& key) {
    const auto it = relays().find(&box);
    return it != relays().end() && it->second->keyPressed(key, &box);
}

CardCallOutLauncher& cardCallOutLauncherForTest() {
    static CardCallOutLauncher launcher;
    return launcher;
}
} // namespace test_hooks

juce::CallOutBox* launchCardCallOut(std::unique_ptr<juce::Component> content, juce::Rectangle<int> area,
                                    juce::Component* anchor, bool closeOnUndo) {
    juce::CallOutBox* box = nullptr;
    if (auto& launcher = test_hooks::cardCallOutLauncherForTest())
        box = launcher(std::move(content), area);
    else
        box = &juce::CallOutBox::launchAsynchronously(std::move(content), area, nullptr);
    if (box != nullptr)
        relayAppShortcutsFromCallOut(*box, anchor, closeOnUndo);
    return box;
}

} // namespace synth::ui
