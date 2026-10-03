#pragma once

// Shared fixtures for Tests/UI/Graph/CardLayoutEditor/OnCard/*Tests.cpp: the editor canvas with the
// on-card editor a real "Edit Layout..." menu click opens, and a pointer that drives an outline through
// the real mouse handlers the way a window would (every event relative to the outline's current bounds,
// which move with the drag). Header-only; not registered in Tests/CMakeLists.txt.

#include "../CardLayoutEditorTestHelpers.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/CardLayoutEditor/OnCard/CardLayoutAddPanel.h"
#include "UI/Graph/CardLayoutEditor/OnCard/CardLayoutControlPanel.h"
#include "UI/Graph/CardLayoutEditor/OnCard/CardLayoutOnCardEditor.h"
#include "UI/Layout/ReducedMotion.h"

namespace oncard_test {

using cardlayouteditor_test::EditorCanvas;
using cardlayouteditor_test::NodeID;
using synth::ui::CardLayoutAddPanel;
using synth::ui::CardLayoutAddRow;
using synth::ui::CardLayoutControlPanel;
using synth::ui::CardLayoutOnCardEditor;
using synth::ui::CardLayoutOutline;

/** The canvas with motion off (nothing glides, so every drop has landed when the handler returns). */
struct OnCardRig : EditorCanvas {
    OnCardRig() {
        synth::ui::setReducedMotionForTest(true);
        CardLayoutOnCardEditor::setControlPanelLauncherForTest(
            [this](std::unique_ptr<juce::Component> panel) { panelLaunched = std::move(panel); });
    }
    ~OnCardRig() {
        panelLaunched.reset();
        CardLayoutOnCardEditor::setControlPanelLauncherForTest(nullptr);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }

    /** The control panel the editor last launched (the rig keeps it; the editor points at it while open). */
    std::unique_ptr<juce::Component> panelLaunched;

    /** Picks "Edit Layout..." from the card's module menu; the editor it opened, or null. */
    CardLayoutOnCardEditor* openOnCard(NodeID id) {
        auto* c = card(id);
        const auto menu = c != nullptr ? c->buildModuleContextMenu() : juce::PopupMenu();
        const auto* item = cardbody_test::menuItem(menu, "Edit Layout...");
        if (item == nullptr || !item->isEnabled || !item->action)
            return nullptr;
        item->action();
        return dynamic_cast<CardLayoutOnCardEditor*>(launched.get());
    }
};

/** One press-drag-release on a control's outline, in the overlay's coordinates. */
class Pointer {
public:
    Pointer(CardLayoutOnCardEditor& editor, const juce::String& key)
        : editor_(editor)
        , outline_(*editor.getOutlineForTest(key))
        , at_(outline_.getBounds().getCentre()) {
        outline_.mouseDown(event(juce::ModifierKeys::leftButtonModifier));
    }

    /** Moves the pointer by `by` in steps, with the left button down (and `extra` modifiers held). */
    void moveBy(juce::Point<int> by, int extra = 0) {
        const auto from = at_;
        for (int step = 1; step <= 4; ++step) {
            at_ = from + juce::Point<int>(by.x * step / 4, by.y * step / 4);
            outline_.mouseDrag(event(juce::ModifierKeys::leftButtonModifier | extra));
        }
    }

    void release() { outline_.mouseUp(event(0)); }

private:
    juce::MouseEvent event(int mods) {
        return cardbody_test::mouseAt(outline_, (at_ - outline_.getPosition()).toFloat(), juce::ModifierKeys(mods));
    }

    CardLayoutOnCardEditor& editor_;
    CardLayoutOutline& outline_;
    juce::Point<int> at_;
};

/** The item for `paramId` in the stored layout, or null. */
inline const synth::CardParamItem* storedItem(const synth::CardLayout& layout, const juce::String& paramId) {
    for (const auto& section : layout.sections)
        for (const auto& item : section.items)
            if (const auto* param = std::get_if<synth::CardParamItem>(&item);
                param != nullptr && param->paramId == paramId)
                return param;
    return nullptr;
}

/** The widget `paramId`'s card draws now. */
inline juce::Component* widgetOf(ModuleComponent& card, const juce::String& paramId) {
    return card.getCardBody()->findWidget(paramId);
}

/** Opens `key`'s control panel with Return on its outline; the rig keeps the panel. */
inline CardLayoutControlPanel* openControlPanel(OnCardRig& rig, CardLayoutOnCardEditor& editor,
                                                const juce::String& key) {
    rig.panelLaunched.reset();
    editor.getOutlineForTest(key)->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    return dynamic_cast<CardLayoutControlPanel*>(rig.panelLaunched.get());
}

/** Hides `key` through the Hide from card button of its panel. */
inline void hideThroughPanel(OnCardRig& rig, CardLayoutOnCardEditor& editor, const juce::String& key) {
    auto* panel = openControlPanel(rig, editor, key);
    ASSERT_NE(panel, nullptr) << key;
    panel->getHideButtonForTest().onClick();
}

/** Opens the Add control panel with a click on the strip's button; the rig keeps the panel. */
inline CardLayoutAddPanel* openAddPanel(OnCardRig& rig, CardLayoutOnCardEditor& editor) {
    rig.panelLaunched.reset();
    editor.getAddButtonForTest().onClick();
    return dynamic_cast<CardLayoutAddPanel*>(rig.panelLaunched.get());
}

/** A mouse event on `comp` whose screen position is `screen`. */
inline juce::MouseEvent eventAtScreen(juce::Component& comp, juce::Point<int> screen, int mods) {
    return cardbody_test::mouseAt(comp, comp.getLocalPoint(nullptr, screen).toFloat(), juce::ModifierKeys(mods));
}

/** A click on a row of the Add control panel. */
inline void clickRow(CardLayoutAddRow& row) {
    const auto at = row.localPointToGlobal(row.getLocalBounds().getCentre());
    row.mouseDown(eventAtScreen(row, at, juce::ModifierKeys::leftButtonModifier));
    row.mouseUp(eventAtScreen(row, at, 0));
}

/** Drags a row out of the panel and releases it over `to` (overlay pixels). */
inline void dragRowTo(CardLayoutOnCardEditor& editor, CardLayoutAddRow& row, juce::Point<int> to) {
    const auto from = row.localPointToGlobal(row.getLocalBounds().getCentre());
    const auto target = editor.localPointToGlobal(to);
    constexpr int left = juce::ModifierKeys::leftButtonModifier;
    row.mouseDown(eventAtScreen(row, from, left));
    row.mouseDrag(eventAtScreen(row, (from + target) / 2, left));
    row.mouseDrag(eventAtScreen(row, target, left));
    row.mouseUp(eventAtScreen(row, target, 0));
}

} // namespace oncard_test
