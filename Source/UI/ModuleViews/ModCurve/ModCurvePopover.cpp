#include "ModCurvePopover.h"

#include "ModCurveWindow.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModDot/ModDotPanelLaunch.h"

namespace synth::ui {

namespace {
struct OpenBox {
    juce::Component::SafePointer<ModDotPanelFrame> frame;
    juce::Component::SafePointer<ModCurveBox> box;
    juce::uint32 attenuverter = 0;
};

OpenBox& openBox() {
    static OpenBox open;
    return open;
}

void forgetIfGone() {
    auto& open = openBox();
    if (open.frame == nullptr || open.box == nullptr)
        open = {};
}
} // namespace

namespace test_hooks {
std::function<void(std::unique_ptr<ModCurveBox>, juce::Component&)>& modCurvePopoverLauncherForTest() {
    static std::function<void(std::unique_ptr<ModCurveBox>, juce::Component&)> hook;
    return hook;
}
} // namespace test_hooks

bool isModCurvePopoverOpen() {
    forgetIfGone();
    return openBox().frame != nullptr && !openBox().frame->isClosing();
}

juce::AudioProcessorGraph::NodeID openModCurvePopoverFor() {
    return juce::AudioProcessorGraph::NodeID(isModCurvePopoverOpen() ? openBox().attenuverter : 0u);
}

ModCurveBox* openModCurveBox() { return isModCurvePopoverOpen() ? openBox().box.getComponent() : nullptr; }

void closeModCurvePopover() {
    forgetIfGone();
    if (auto* frame = openBox().frame.getComponent())
        frame->close();
}

bool modCurvePopoverContains(juce::Point<int> screenPoint) {
    forgetIfGone();
    auto* frame = openBox().frame.getComponent();
    return frame != nullptr && frame->getScreenBounds().contains(screenPoint);
}

void tickModCurvePopover() {
    if (auto* box = openModCurveBox())
        box->tick();
}

ModDotPanelFrame* openModCurvePopover(GraphEditor& editor, juce::AudioProcessorGraph::NodeID attenuverter,
                                      juce::Component& anchor, juce::Rectangle<int> anchorScreenBounds,
                                      bool anchorIsRegion) {
    // Opening again while the first fades out cuts that fade short; opening while it is open replaces it.
    if (auto* old = openBox().frame.getComponent(); old != nullptr) {
        if (!old->isClosing())
            old->close();
        old->finishClosingNow();
    }
    openBox() = {};

    auto target = std::make_shared<ModCurveEditTarget>(editor, attenuverter);
    if (!target->exists())
        return nullptr;
    auto box = std::make_unique<ModCurveBox>(target, ModCurvePresetStore::app());
    const juce::Component::SafePointer<juce::Component> anchorSafe(&anchor);
    const juce::Component::SafePointer<GraphEditor> editorSafe(&editor);
    // Opened from the canvas, not the chip: a chip in a panel or the matrix has no route to the app's shortcuts.
    box->onEnlarge = [target, editorSafe] {
        closeModCurvePopover();
        openModCurveWindow(target, editorSafe.getComponent());
    };
    box->onClose = [] { closeModCurvePopover(); };

    if (auto& launcher = test_hooks::modCurvePopoverLauncherForTest()) {
        launcher(std::move(box), anchor);
        return nullptr;
    }

    auto* boxPtr = box.get();
    ModDotPanelLaunch options;
    options.appProperties = editor.getModDot().host.appProperties;
    options.anchorIsRegion = anchorIsRegion;
    // A menu or the name prompt opened from the box is modal: a press in it is not "outside" the box.
    options.keepOpenOnOutsideClick = [] {
        return juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0;
    };
    auto* frame = launchInFrame(std::move(box), anchor, anchorScreenBounds, options);
    openBox() = {frame, boxPtr, attenuverter.uid};

    auto closed = frame->onClosed;
    frame->onClosed = [closed, anchorSafe] {
        if (closed)
            closed();
        juce::MessageManager::callAsync([anchorSafe] {
            if (anchorSafe != nullptr && anchorSafe->isShowing())
                anchorSafe->grabKeyboardFocus();
        });
    };
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ModCurveBox>(boxPtr)] {
        if (safe != nullptr)
            safe->focusFirstControl();
    });
    return frame;
}

void wireModCurveChip(ModCurveChip& chip, GraphEditor& editor, juce::AudioProcessorGraph::NodeID attenuverter,
                      const juce::String& sourceName) {
    auto target = std::make_shared<ModCurveEditTarget>(editor, attenuverter);
    chip.setTarget(target, sourceName);
    chip.onClick = [&chip, &editor, attenuverter] {
        if (isModCurvePopoverOpen() && openModCurvePopoverFor() == attenuverter) {
            closeModCurvePopover();
            return;
        }
        openModCurvePopover(editor, attenuverter, chip, chip.getScreenBounds(), false);
    };
}

} // namespace synth::ui
