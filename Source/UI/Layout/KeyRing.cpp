#include "UI/Layout/KeyRing.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Layout/ReadOnlyTextValue.h"
#include <algorithm>

namespace synth::ui {

namespace {
constexpr int kRingOutset = 1;

bool isReachable(const juce::Component& c, const juce::Component& owner) {
    if (!c.isEnabled())
        return false;
    for (const auto* at = &c; at != nullptr && at != &owner; at = at->getParentComponent())
        if (!at->isVisible())
            return false;
    return owner.isParentOf(&c);
}

juce::Rectangle<int> boundsInOwner(const juce::Component& c, const juce::Component& owner) {
    return owner.getLocalArea(c.getParentComponent(), c.getBounds());
}

// A control's name for a screen reader: its title, else the text it shows.
juce::String nameOf(const juce::Component& c) {
    if (c.getTitle().isNotEmpty())
        return c.getTitle();
    if (const auto* button = dynamic_cast<const juce::Button*>(&c);
        button != nullptr && button->getButtonText().isNotEmpty())
        return button->getButtonText();
    if (const auto* label = dynamic_cast<const juce::Label*>(&c))
        return label->getText();
    if (const auto* menu = dynamic_cast<const juce::ComboBox*>(&c))
        return menu->getText();
    return c.getName();
}
} // namespace

KeyRing::KeyRing(juce::Component& owner, StopsFn candidates, Order order)
    : owner_(owner)
    , candidates_(std::move(candidates))
    , order_(order) {}

std::vector<juce::Component*> KeyRing::getStops() const {
    auto stops = candidates_();
    stops.erase(std::remove_if(stops.begin(), stops.end(),
                               [this](juce::Component* c) { return c == nullptr || !isReachable(*c, owner_); }),
                stops.end());
    if (order_ == Order::byX)
        std::stable_sort(stops.begin(), stops.end(), [this](juce::Component* a, juce::Component* b) {
            return boundsInOwner(*a, owner_).getX() < boundsInOwner(*b, owner_).getX();
        });
    return stops;
}

// A ringed control that has since gone (hidden, disabled) hands the ring to the first one that is left.
juce::Component* KeyRing::getRingedStop() const {
    const auto stops = getStops();
    if (stops.empty())
        return nullptr;
    return std::find(stops.begin(), stops.end(), ringed_.getComponent()) != stops.end() ? ringed_.getComponent()
                                                                                        : stops.front();
}

bool KeyRing::enter() {
    auto* stop = getRingedStop();
    if (stop == nullptr)
        return false;
    active_ = true;
    moveRingTo(stop);
    return true;
}

void KeyRing::leave() {
    if (!active_)
        return;
    active_ = false;
    owner_.repaint();
    announce();
}

void KeyRing::moveRingTo(juce::Component* stop) {
    ringed_ = stop;
    owner_.repaint();
    announce();
}

// The panel root keeps real focus, so the screen reader hears the ring only through the panel's value.
void KeyRing::announce() const {
    if (auto* handler = owner_.getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

juce::String KeyRing::getValueText() const {
    if (!active_)
        return {};
    const auto* stop = getRingedStop();
    return stop != nullptr ? nameOf(*stop) : juce::String();
}

std::unique_ptr<juce::AccessibilityHandler> KeyRing::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        owner_, juce::AccessibilityRole::group, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<ReadOnlyTextValue>([this] { return getValueText(); })});
}

bool KeyRing::handleKey(const juce::KeyPress& key) {
    if (!active_ || key.getModifiers().isAnyModifierKeyDown())
        return false;
    const auto stops = getStops();
    auto* current = getRingedStop();
    if (stops.empty() || current == nullptr) {
        leave();
        return false;
    }
    const auto at = std::find(stops.begin(), stops.end(), current) - stops.begin();
    if (key.isKeyCode(juce::KeyPress::leftKey)) {
        moveRingTo(stops[(size_t)std::max<long>(0, at - 1)]);
    } else if (key.isKeyCode(juce::KeyPress::rightKey)) {
        moveRingTo(stops[(size_t)std::min<long>((long)stops.size() - 1, at + 1)]);
    } else if (key.isKeyCode(juce::KeyPress::homeKey)) {
        moveRingTo(stops.front());
    } else if (key.isKeyCode(juce::KeyPress::endKey)) {
        moveRingTo(stops.back());
    } else if (key.isKeyCode(juce::KeyPress::returnKey) || key.isKeyCode(juce::KeyPress::spaceKey)) {
        activate(*current);
    } else if (key.isKeyCode(juce::KeyPress::downKey) || key.isKeyCode(juce::KeyPress::escapeKey)) {
        leave();
    } else if (!key.isKeyCode(juce::KeyPress::upKey)) {
        return false; // a tool digit, a letter: the panel's own shortcuts still work with the ring up
    }
    return true;
}

// A menu opens, a text readout starts editing, a text field takes the keyboard (the ring is done: the panel loses
// focus), a button is pressed: what a click on it would do.
void KeyRing::activate(juce::Component& stop) {
    if (auto* menu = dynamic_cast<juce::ComboBox*>(&stop)) {
        menu->showPopup();
    } else if (auto* label = dynamic_cast<juce::Label*>(&stop)) {
        label->showEditor();
    } else if (auto* editor = dynamic_cast<juce::TextEditor*>(&stop)) {
        leave();
        editor->grabKeyboardFocus();
    } else if (auto* button = dynamic_cast<juce::Button*>(&stop)) {
        button->triggerClick();
    }
}

void KeyRing::paintRing(juce::Graphics& g) const {
    if (!active_)
        return;
    if (const auto* stop = getRingedStop())
        paintFocusRingAlways(g, boundsInOwner(*stop, owner_).expanded(kRingOutset).toFloat(), owner_, 4.0f);
}

} // namespace synth::ui
