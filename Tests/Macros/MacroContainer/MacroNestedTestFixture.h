#pragma once

// Shared fixture for the nested-macro canvas tests (MacroNestedGeometryTests.cpp,
// MacroNestedCableTests.cpp, MacroNestedMembershipTests.cpp). The nesting is made the way a user makes
// it: group the child, then group the whole child plus the parent's own modules (Create Macro's path).
// Header-only; not registered in Tests/CMakeLists.txt.

#include "MacroContainerTestHelpers.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

// A child macro {c1, c2} nested in a parent macro whose own direct members are {p1, p2}, both
// expanded. The child row sits above the parent's own row so each has empty space of its own.
struct NestedMacroFixture {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID c1, c2, p1, p2;
    juce::String childId, parentId;

    explicit NestedMacroFixture(bool autoCreateChildPorts = false,
                                const std::function<void(NestedMacroFixture&)>& beforeGrouping = {}) {
        undo.setGraphEditor(&editor);
        editor.setSize(2000, 1600);
        c1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 400, 300);
        c2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 800, 300);
        p1 = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 300, 1000);
        p2 = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 1000);
        if (beforeGrouping)
            beforeGrouping(*this);

        editor.setSelectedNodes({c1, c2});
        childId = ctl().groupSelectionIntoMacro(autoCreateChildPorts);
        editor.setSelectedNodes({c1, c2, p1, p2}); // the whole child plus two loose modules
        parentId = ctl().groupSelectionIntoMacro();
        linked = parentId.isNotEmpty() && editor.getMacros().parentOf(childId) == parentId;
        editor.clearSelection();
        ctl().setMacroCollapsed(childId, false);
        ctl().setMacroCollapsed(parentId, false);
        editor.updateComponents();
        undo.clearUndoHistory();
    }

    bool linked = false;

    MacroGroupController& ctl() { return editor.getMacroController(); }
    MacroCardComponent* card(const juce::String& id) { return ctl().getMacroCardForTest(id); }
    bool cardVisible(const juce::String& id) {
        auto* c = card(id);
        return c != nullptr && c->isVisible();
    }
    bool moduleVisible(NodeID id) {
        auto* comp = findComponent(editor, id);
        return comp != nullptr && comp->isVisible();
    }
    juce::Point<int> pos(NodeID id) { return findComponent(editor, id)->getPosition(); }

    // True if `p` hit-tests to `macroId`'s hull and lies on no visible module, chip or collapse
    // button, so a press there is a press on that hull's own empty space.
    bool isEmptyHullSpace(juce::Point<int> p, const juce::String& macroId) {
        if (ctl().macroHullAt(p) != macroId)
            return false;
        for (auto* comp : editor.getModuleComponents())
            if (comp != nullptr && comp->isVisible() && comp->getBounds().contains(p))
                return false;
        return ctl().macroChipAt(p).isEmpty() && ctl().macroCollapseButtonAt(p).isEmpty();
    }
};

} // namespace
