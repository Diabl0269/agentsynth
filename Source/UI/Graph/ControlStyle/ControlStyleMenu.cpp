#include "ControlStyleMenu.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardLayoutQuickEdit.h"
#include "UI/Graph/ControlStyle/ControlStylePanel.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth {

namespace {
std::function<void(std::unique_ptr<juce::Component>)>& launcherForTest() {
    static std::function<void(std::unique_ptr<juce::Component>)> launcher;
    return launcher;
}

bool showsSlider(const juce::Component& parent) {
    for (auto* child : parent.getChildren())
        if (dynamic_cast<juce::Slider*>(child) != nullptr || showsSlider(*child))
            return true;
    return false;
}
} // namespace

void setControlStylePanelLauncherForTest(std::function<void(std::unique_ptr<juce::Component>)> launcher) {
    launcherForTest() = std::move(launcher);
}

bool appendControlStyleMenuItem(juce::PopupMenu& menu, ModuleComponent& card) {
    if (!showsSlider(card))
        return false;
    juce::Component::SafePointer<ModuleComponent> safeCard(&card);
    menu.addItem("Control Style...", [safeCard] {
        if (safeCard != nullptr)
            showControlStylePanel(*safeCard);
    });
    return true;
}

// "Edit Layout..." edits a built-in card on the card itself; for a hosted plugin it is the card's button's editor
// over the plugin's parameters (docs/layout/module-card-layout.md#editing-a-layout).
void appendLayoutAndStyleMenuItems(juce::PopupMenu& menu, ModuleComponent& card) {
    juce::Component::SafePointer<ModuleComponent> safeCard(&card);
    const auto* body = card.getCardBody();
    if (body != nullptr && body->drawsFromLayout()) {
        appendEditLayoutMenuItem(menu, [safeCard] {
            if (safeCard != nullptr)
                safeCard->showCardLayoutEditor();
        });
        appendControlStyleMenuItem(menu, card);
        menu.addSeparator();
    } else if (auto* hosted = dynamic_cast<HostedPluginModule*>(card.getModule())) {
        appendEditLayoutMenuItem(menu, [safeCard] {
            if (safeCard != nullptr && safeCard->onChooseKnobsRequested)
                safeCard->onChooseKnobsRequested();
        });
        appendControlStyleMenuItem(menu, card);
        // Starts "add by moving a control in the plugin" (no popover).
        menu.addItem("Add control from plugin window...", hosted->hasInstance(), false, [safeCard] {
            if (safeCard != nullptr)
                safeCard->setPluginTouchToAdd(true);
        });
        menu.addSeparator();
    } else if (appendControlStyleMenuItem(menu, card)) {
        menu.addSeparator();
    }
}

// Every control of the panel reaches the node by id through the editor, never through the card: a write can
// rebuild the card while the panel stays open.
void showControlStylePanel(ModuleComponent& card) {
    auto* editor = card.findParentComponentOfClass<GraphEditor>();
    if (editor == nullptr)
        return;
    auto panel = std::make_unique<synth::ui::ControlStylePanel>(*editor, card.getNodeId(),
                                                                AIStateMapper::getFactoryTypeName(card.getModule()));
    if (auto& launcher = launcherForTest())
        return launcher(std::move(panel));
    auto* raw = panel.get();
    juce::CallOutBox::launchAsynchronously(std::move(panel), card.getScreenBounds(), nullptr);
    juce::Component::SafePointer<synth::ui::ControlStylePanel> safePanel(raw);
    juce::MessageManager::callAsync([safePanel] {
        if (safePanel != nullptr)
            safePanel->focusFirstControl();
    });
}

} // namespace synth
