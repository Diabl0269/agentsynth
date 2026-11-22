#pragma once

// Shared fixtures for Tests/UI/Graph/CardLayoutEditor/*Tests.cpp: a canvas whose cards resolve against
// a temp-dir ModuleCardLayoutStore (bound the way MainComponent binds the app's), and the editor a real
// menu click opens ("Edit Layout..." the on-card editor), taken through ModuleComponent's launcher seam
// instead of a window, and the list editor built over a built-in source.
// Header-only; not registered in Tests/CMakeLists.txt.

#include "../CardBody/CardBodyTestHelpers.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsFamilies.h"
#include "UI/Graph/CardBody/ModuleCardLayoutBinding.h"
#include "UI/Graph/CardLayoutEditor/BuiltInCardLayoutSource.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorComponent.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorRow.h"

namespace cardlayouteditor_test {

using cardbody_test::NodeID;
using synth::ui::CardLayoutEditorComponent;

/** A canvas bound to a store on a fresh temp directory. The store is declared before the canvas, so it
 *  outlives every card that resolved against it. */
struct EditorCanvas {
    juce::File storeDir{juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("card-layout-editor-store-" + juce::Uuid().toString())};
    synth::ModuleCardLayoutStore store{storeDir};
    cardbody_test::CardCanvas canvas;
    synth::ModuleCardLayoutBinding binding{canvas.editor, store};
    std::unique_ptr<juce::Component> launched;

    EditorCanvas() {
        ModuleComponent::setCardLayoutEditorLauncherForTest(
            [this](std::unique_ptr<juce::Component> editor) { launched = std::move(editor); });
    }

    ~EditorCanvas() {
        launched.reset(); // ends any open session while the canvas is alive
        ModuleComponent::setCardLayoutEditorLauncherForTest(nullptr);
        canvas.editor.detachAllModuleComponents();
        storeDir.deleteRecursively();
    }

    /** A Filter drawn as stacked rows (type, tone, modulation, output, the footer), the way the editor's row tests
     *  were written: the editor edits row layouts, and the Filter's built-in default is a lane grid now. A stored
     *  layout based on the current revision, so nothing about it reads as stale. */
    static synth::CardLayout rowFilterLayout() {
        using namespace synth::cardlayout;
        synth::CardLayout layout;
        layout.basedOn = juce::String("Filter@7");
        auto customShape =
            section("customShape", std::nullopt, {param("shift"), param("depth"), param("resonanceMode")});
        customShape.visibleWhen = synth::CardCondition{"filterType", {"Custom"}, synth::CardConditionEffect::Show};
        auto custom = section("custom", "Custom", {param("morphX"), param("morphY")});
        custom.visibleWhen = synth::CardCondition{"filterType", {"Custom"}, synth::CardConditionEffect::Show};
        layout.sections = {
            section("type", std::nullopt, {param("filterType")}),
            section("tone", std::nullopt,
                    {param("cutoff", synth::CardWidget::KnobLarge), param("resonance"), param("drive")}),
            customShape,
            section("modulation", "Modulation", {param("keyTrack"), param("outputLevel")}),
            section("output", std::nullopt, {param("mix"), param("pan")}),
            custom,
            footer({}),
        };
        return layout;
    }

    NodeID add(std::unique_ptr<juce::AudioProcessor> processor, int x = 0, int y = 0) {
        const bool isFilter = dynamic_cast<FilterModule*>(processor.get()) != nullptr;
        const auto id = isFilter ? canvas.add(std::move(processor), x, y, rowFilterLayout())
                                 : canvas.add(std::move(processor), x, y);
        canvas.editor.updateComponents();
        return id;
    }

    ModuleComponent* card(NodeID id) { return canvas.card(id); }

    /** The list editor over a built-in module's source, built directly: no menu opens it any more (the
     *  on-card editor replaced it for built-in cards), but the hosted plugin's editor is this same component. */
    CardLayoutEditorComponent* openList(NodeID id) {
        auto source = std::make_unique<synth::ui::BuiltInCardLayoutSource>(canvas.editor, &canvas.undo, id);
        launched = std::make_unique<CardLayoutEditorComponent>(std::move(source),
                                                               canvas.editor.getCardKeyboard().getShortcutManager());
        return static_cast<CardLayoutEditorComponent*>(launched.get());
    }

    /** Closes the editor, ending its session. */
    void close() { launched.reset(); }

    /** The node's stored override, read back as sections; nullopt without one. A Filter's untouched row layout (the
     *  one `add` gives it) is the layout it started with, not an override of its own, so it reads as none. */
    std::optional<synth::CardLayout> storedLayout(NodeID id) {
        const auto json = synth::getCardLayoutOverride(canvas.engine.getGraph(), id);
        if (json.isVoid())
            return std::nullopt;
        if (juce::JSON::toString(json) == juce::JSON::toString(rowFilterLayout().toVar()))
            return std::nullopt;
        return synth::upgradeV1(synth::CardLayout::fromVar(json).layout, {});
    }
};

/** The visible row index of `key` in `editor`, failing the test when it is absent. */
inline int rowOf(CardLayoutEditorComponent& editor, const juce::String& key) {
    const int row = editor.findRowForTest(key);
    EXPECT_GE(row, 0) << key << " has a row";
    return row;
}

/** The caption (or, for a toggle, the button text) the card shows for `paramId`. */
inline juce::String captionOf(ModuleComponent& card, const juce::String& paramId) {
    const auto& plan = card.getCardBody()->getPlan();
    const int index = plan.findParam(paramId);
    if (index < 0)
        return {};
    const auto& item = plan.items[(size_t)index];
    if (auto* label = dynamic_cast<juce::Label*>(item.label))
        return label->getText();
    if (auto* button = dynamic_cast<juce::Button*>(item.widget))
        return button->getButtonText();
    return {};
}

/** The section header labels the card shows, top to bottom. */
inline juce::StringArray sectionHeadersOf(ModuleComponent& card) {
    juce::StringArray titles;
    for (const auto& section : card.getCardBody()->getPlan().sections)
        if (auto* label = dynamic_cast<juce::Label*>(section.header))
            titles.add(label->getText());
    return titles;
}

} // namespace cardlayouteditor_test
