// TooltipRulesWalk.h -- the walk behind the button tooltip rules (docs/development/accessibility.md#the-rule): collect
// every button under a component with what its hover shows, then list the labelled ones whose tooltip starts with their
// own words and the names one surface carries with different tooltips. Used by TooltipRulesTests.cpp (the cards and
// the EQ and Sampler windows) and ShapeWindowThemeTests.cpp (the shape window). Header-only.
#pragma once

#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/AppTooltipWindow.h"
#include "UI/Layout/ButtonTooltip.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <set>
#include <vector>

namespace synth::ui::test::tooltip_rules {

using synth::ui::buttonShowsText;
using synth::ui::tooltipEchoesLabel;

struct ButtonTip {
    juce::String path;
    juce::String name; // the screen-reader name: the title, else the text
    juce::String text;
    juce::String tip; // what the hover shows (a name drawn beside the control is left out, AppTooltipWindow)
    bool showsText = false;
    bool segment = false; // one value of a segmented switch: its tooltip names the switch ("Grid: Off")
};

inline void collectButtons(juce::Component& component, const juce::String& path, std::vector<ButtonTip>& out) {
    for (auto* child : component.getChildren()) {
        const auto label = child->getTitle().isNotEmpty() ? child->getTitle() : child->getName();
        const auto childPath = path + " > " + (label.isNotEmpty() ? label : juce::String("<unnamed>"));
        if (auto* button = dynamic_cast<juce::Button*>(child)) {
            ButtonTip entry;
            entry.path = childPath;
            entry.text = button->getButtonText();
            entry.name = button->getTitle().isNotEmpty() ? button->getTitle() : entry.text;
            entry.tip = synth::ui::AppTooltipWindow::withoutDrawnName(*button, button->getTooltip());
            entry.showsText = buttonShowsText(*button);
            entry.segment = dynamic_cast<synth::ui::CardSegmentedSwitch*>(&component) != nullptr;
            out.push_back(entry);
        }
        collectButtons(*child, childPath, out);
    }
}

// The labelled buttons in `buttons` whose hover tip opens with their own words (or is only those words).
inline std::vector<juce::String> echoes(const std::vector<ButtonTip>& buttons) {
    std::vector<juce::String> found;
    for (const auto& b : buttons)
        if (b.showsText && !b.segment && tooltipEchoesLabel(b.text, b.tip))
            found.push_back(b.path + ": \"" + b.tip + "\"");
    return found;
}

// Names that more than one button in `buttons` carries with different tooltips.
inline std::vector<juce::String> twinsThatDiffer(const std::vector<ButtonTip>& buttons) {
    std::map<juce::String, std::set<juce::String>> tipsByName;
    std::map<juce::String, juce::StringArray> pathsByName;
    // Delete is two actions that share a word: the card's header removes the module, the shape window's the shape.
    static const juce::StringArray differentActions{"Delete"};
    for (const auto& b : buttons) {
        if (b.name.isEmpty() || b.segment || differentActions.contains(b.name))
            continue;
        tipsByName[b.name].insert(b.tip);
        pathsByName[b.name].add(b.path + " \"" + b.tip + "\"");
    }
    std::vector<juce::String> found;
    for (const auto& [name, tips] : tipsByName)
        if (tips.size() > 1)
            found.push_back(name + ":\n      " + pathsByName[name].joinIntoString("\n      "));
    return found;
}

inline juce::String listing(const std::vector<juce::String>& lines) {
    juce::String out;
    for (const auto& line : lines)
        out << "\n  " << line;
    return out;
}

inline void append(std::vector<ButtonTip>& to, const std::vector<ButtonTip>& from) {
    to.insert(to.end(), from.begin(), from.end());
}

} // namespace synth::ui::test::tooltip_rules
