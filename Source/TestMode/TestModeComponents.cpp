// Concern: locating components by id and turning them into JSON for the test-mode client.

#include "TestMode/TestModeComponents.h"
#include "MainComponent/MainComponent.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <functional>
#if !JUCE_WINDOWS
#include <cxxabi.h>
#endif

namespace synth::testmode {

namespace {

juce::var rectToVar(juce::Rectangle<int> r) {
    auto* o = new juce::DynamicObject();
    o->setProperty("x", r.getX());
    o->setProperty("y", r.getY());
    o->setProperty("w", r.getWidth());
    o->setProperty("h", r.getHeight());
    return juce::var(o);
}

juce::Rectangle<int> boundsInWindow(const juce::Component& c) {
    auto bounds = c.getScreenBounds();
    if (const auto* window = findMainWindow())
        bounds = bounds.translated(-window->getScreenX(), -window->getScreenY());
    return bounds;
}

// Depth first; stops when `visit` returns true.
bool walk(juce::Component& c, const std::function<bool(juce::Component&)>& visit) {
    if (visit(c))
        return true;
    for (auto* child : c.getChildren())
        if (walk(*child, visit))
            return true;
    return false;
}

bool walkDesktop(const std::function<bool(juce::Component&)>& visit) {
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* top = desktop.getComponent(i); top != nullptr && walk(*top, visit))
            return true;
    return false;
}

using Match = std::function<bool(const juce::Component&)>;

// The first match, preferring one that is showing.
juce::Component* firstMatch(const std::function<void(const std::function<bool(juce::Component&)>&)>& scan,
                            const Match& match) {
    juce::Component* any = nullptr;
    juce::Component* showing = nullptr;
    scan([&](juce::Component& c) {
        if (!match(c))
            return false;
        if (any == nullptr)
            any = &c;
        if (c.isShowing()) {
            showing = &c;
            return true;
        }
        return false;
    });
    return showing != nullptr ? showing : any;
}

juce::Component* findOne(const juce::String& id, juce::Component* within) {
    const auto scan = [within](const std::function<bool(juce::Component&)>& visit) {
        if (within != nullptr)
            walk(*within, visit);
        else
            walkDesktop(visit);
    };
    const Match matchers[] = {
        [&](const juce::Component& c) {
            if (c.getComponentID() == id)
                return true;
            // A card's number can shift when another of its type is added or removed: its live title is the truth.
            const auto* card = dynamic_cast<const ModuleComponent*>(&c);
            return card != nullptr && id.startsWith("card:") && "card:" + card->cardTitle() == id;
        },
        [&](const juce::Component& c) { return c.getName() == id; },
        [&](const juce::Component& c) { return c.getTitle() == id; },
    };
    for (const auto& matcher : matchers)
        if (auto* found = firstMatch(scan, matcher))
            return found;
    return nullptr;
}

} // namespace

juce::Component* findMainWindow() {
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* top = desktop.getComponent(i))
            if (auto* window = dynamic_cast<juce::ResizableWindow*>(top))
                if (dynamic_cast<MainComponent*>(window->getContentComponent()) != nullptr)
                    return top;
    return desktop.getNumComponents() > 0 ? desktop.getComponent(0) : nullptr;
}

Lookup findComponent(const juce::String& id) {
    Lookup out;
    juce::StringArray parts;
    parts.addTokens(id, ">", "");
    parts.trim();
    parts.removeEmptyStrings();
    if (parts.isEmpty()) {
        out.error = "empty component id";
        return out;
    }
    juce::Component* current = nullptr;
    for (const auto& part : parts) {
        juce::Component* next = nullptr;
        if (part == "window" && current == nullptr)
            next = findMainWindow();
        else
            next = findOne(part, current);
        if (next == nullptr) {
            out.error = "no component \"" + part + "\"" +
                        (current != nullptr ? " inside \"" + current->getComponentID() + "\"" : juce::String());
            return out;
        }
        current = next;
    }
    out.component = current;
    return out;
}

juce::String typeNameOf(const juce::Component& component) {
    const char* raw = typeid(component).name();
#if !JUCE_WINDOWS
    int status = 0;
    char* demangled = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
    const juce::String name = (status == 0 && demangled != nullptr) ? juce::String(demangled) : juce::String(raw);
    std::free(demangled);
    return name;
#else
    return juce::String(raw).replace("class ", "").replace("struct ", "");
#endif
}

juce::var describeComponent(const juce::Component& c) {
    auto* o = new juce::DynamicObject();
    o->setProperty("id", c.getComponentID());
    o->setProperty("name", c.getName());
    o->setProperty("title", c.getTitle());
    o->setProperty("type", typeNameOf(c));
    o->setProperty("bounds", rectToVar(boundsInWindow(c)));
    o->setProperty("visible", c.isVisible());
    o->setProperty("showing", c.isShowing());
    return juce::var(o);
}

juce::var describeTree(const juce::Component& root, int depth, bool all) {
    juce::var out = describeComponent(root);
    juce::Array<juce::var> children;
    if (depth > 0)
        for (auto* child : root.getChildren())
            if (all || child->isShowing())
                children.add(describeTree(*child, depth - 1, all));
    out.getDynamicObject()->setProperty("children", children);
    return out;
}

juce::var describeState(const juce::Component& c) {
    juce::var out = describeComponent(c);
    auto* o = out.getDynamicObject();
    o->setProperty("enabled", c.isEnabled());
    o->setProperty("hasFocus", c.hasKeyboardFocus(false));
    if (const auto* button = dynamic_cast<const juce::Button*>(&c)) {
        o->setProperty("text", button->getButtonText());
        o->setProperty("toggleState", button->getToggleState());
    }
    if (const auto* label = dynamic_cast<const juce::Label*>(&c))
        o->setProperty("text", label->getText());
    if (const auto* slider = dynamic_cast<const juce::Slider*>(&c)) {
        o->setProperty("value", slider->getValue());
        o->setProperty("min", slider->getMinimum());
        o->setProperty("max", slider->getMaximum());
    }
    if (const auto* combo = dynamic_cast<const juce::ComboBox*>(&c)) {
        o->setProperty("text", combo->getText());
        o->setProperty("selectedId", combo->getSelectedId());
    }
    if (const auto* editor = dynamic_cast<const juce::TextEditor*>(&c))
        o->setProperty("text", editor->getText());
    if (const auto* source = dynamic_cast<const StateSource*>(&c))
        source->addTestModeState(*o);
    return out;
}

} // namespace synth::testmode
