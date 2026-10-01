#pragma once

// AccessibilityAudit.h -- walks a component tree and reports every interactive control a
// screen-reader user or a mouse-hover user would meet with no name or no tooltip.
//
// Test-only (kept out of Source/ because nothing in the app calls it).
//
// Rules, kept deliberately simple:
//  - Only components whose own isVisible() flag, and every ancestor's up to the root, is true are
//    visited. isShowing() is not used: a headless component has no native peer.
//  - A component with isAccessible() == false, and everything under it, is skipped.
//  - A leaf is a juce::Button, juce::Slider, juce::ComboBox, juce::TextEditor, or any other
//    component with getWantsKeyboardFocus() == true. The audit does not descend into those four
//    stock control types (their internal parts are not separate controls); it does descend into
//    every other component.
//  - missingName: the leaf has an empty getTitle() and, for a Button, empty button text (the two
//    things JUCE's handlers speak; the handler itself is unreachable headlessly, see hasName). A
//    TextEditor with an empty title still counts as named when its description is not empty.
//  - missingTooltip: the leaf is a juce::TooltipClient whose getTooltip() is empty and whose direct
//    parent does not supply a non-empty tooltip either (a wrapper can tip for a slider inside it).
//    A TextEditor never counts.

#include <cstdlib>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <typeinfo>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif

namespace synth::test {

struct Gap {
    enum class Kind { MissingName, MissingTooltip };
    Kind kind;
    juce::String path; // "Root/Parent[name]/Leaf[name]\"text\""
};

namespace audit_detail {

inline juce::String className(const juce::Component& c) {
    std::string name = typeid(c).name();
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    char* demangled = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
    if (status == 0 && demangled != nullptr)
        name = demangled;
    std::free(demangled);
#endif
    const auto sep = name.rfind("::");
    return sep == std::string::npos ? juce::String(name) : juce::String(name.substr(sep + 2));
}

inline juce::String label(const juce::Component& c) {
    juce::String s = className(c);
    if (c.getName().isNotEmpty())
        s << "[" << c.getName() << "]";
    else if (c.getComponentID().isNotEmpty())
        s << "[" << c.getComponentID() << "]";
    if (auto* b = dynamic_cast<const juce::Button*>(&c))
        if (b->getButtonText().isNotEmpty())
            s << "\"" << b->getButtonText() << "\"";
    return s;
}

inline bool isStockControl(const juce::Component& c) {
    return dynamic_cast<const juce::Button*>(&c) != nullptr || dynamic_cast<const juce::Slider*>(&c) != nullptr ||
           dynamic_cast<const juce::ComboBox*>(&c) != nullptr || dynamic_cast<const juce::TextEditor*>(&c) != nullptr;
}

inline bool isLeaf(const juce::Component& c) { return isStockControl(c) || c.getWantsKeyboardFocus(); }

// What JUCE's own handlers speak: the component's title, falling back to the button text for a
// Button. Component::getAccessibilityHandler() returns null without a native peer, which a headless
// component never has, so the handler is not consulted and a handler overriding getTitle() is not
// seen.
inline bool hasName(const juce::Component& c) {
    if (c.getTitle().isNotEmpty())
        return true;
    if (auto* b = dynamic_cast<const juce::Button*>(&c))
        return b->getButtonText().isNotEmpty();
    return dynamic_cast<const juce::TextEditor*>(&c) != nullptr && c.getDescription().isNotEmpty();
}

inline bool missingTooltip(juce::Component& c) {
    if (dynamic_cast<juce::TextEditor*>(&c) != nullptr)
        return false;
    auto* own = dynamic_cast<juce::TooltipClient*>(&c);
    if (own == nullptr || own->getTooltip().isNotEmpty())
        return false;
    auto* parent = dynamic_cast<juce::TooltipClient*>(c.getParentComponent());
    return parent == nullptr || parent->getTooltip().isEmpty();
}

inline void walk(juce::Component& parent, const juce::String& parentPath, std::vector<Gap>& out) {
    for (auto* child : parent.getChildren()) {
        if (!child->isVisible() || !child->isAccessible())
            continue;
        const auto path = parentPath + "/" + label(*child);
        if (isLeaf(*child)) {
            if (!hasName(*child))
                out.push_back({Gap::Kind::MissingName, path});
            if (missingTooltip(*child))
                out.push_back({Gap::Kind::MissingTooltip, path});
        }
        if (!isStockControl(*child))
            walk(*child, path, out);
    }
}

} // namespace audit_detail

/** Every name/tooltip gap among the visible, accessible interactive descendants of `root`. */
inline std::vector<Gap> auditAccessibility(juce::Component& root) {
    std::vector<Gap> gaps;
    audit_detail::walk(root, audit_detail::label(root), gaps);
    return gaps;
}

inline int countGaps(const std::vector<Gap>& gaps, Gap::Kind kind) {
    int n = 0;
    for (const auto& g : gaps)
        n += g.kind == kind ? 1 : 0;
    return n;
}

} // namespace synth::test
