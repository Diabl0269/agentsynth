// Shared fixtures for the Filter shape editor / morph map tests: a Filter in a real graph (so undo works) with its
// shape adapter, and menu capture helpers. Header-only.
#pragma once

#include "../CurveEditor/CurveEditorTestHelpers.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/FilterModule.h"
#include "UI/ModuleViews/ShapeEditor/FilterShapesAdapter.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace synth::ui::test {

struct ShapeRig {
    AudioEngine engine;
    FilterModule* filter = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    AppUndoManager undo;
    std::shared_ptr<FilterShapesAdapter> adapter;

    ShapeRig() {
        filter = new FilterModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(filter));
        adapter = std::make_shared<FilterShapesAdapter>(*filter, &undo, engine.getGraph(), node->nodeID, "Filter");
    }
};

/** The items of a menu and a way to run one by name. */
struct CapturedMenu {
    std::vector<std::string> items;
    std::vector<bool> enabled;
    juce::PopupMenu menu;

    void capture(juce::PopupMenu& m) {
        items.clear();
        enabled.clear();
        menu = m;
        juce::PopupMenu::MenuItemIterator it(menu, false);
        while (it.next()) {
            if (it.getItem().isSeparator)
                continue;
            items.push_back(it.getItem().text.toStdString());
            enabled.push_back(it.getItem().isEnabled);
        }
    }

    bool run(const juce::String& text) {
        juce::PopupMenu::MenuItemIterator it(menu, false);
        while (it.next()) {
            auto& item = it.getItem();
            if (item.text == text && item.action) {
                item.action();
                return true;
            }
        }
        return false;
    }

    bool isEnabled(const juce::String& text) const {
        juce::PopupMenu copy = menu;
        juce::PopupMenu::MenuItemIterator it(copy, false);
        while (it.next())
            if (it.getItem().text == text)
                return it.getItem().isEnabled;
        return false;
    }
};

inline juce::KeyPress key(int code, juce::ModifierKeys mods = {}) { return juce::KeyPress(code, mods, 0); }

} // namespace synth::ui::test
