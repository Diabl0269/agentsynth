#include "UI/Layout/DialogTooltip.h"

#include "UI/Layout/AppTooltipWindow.h"
#include <memory>

namespace synth::ui::dialog_tooltip {

namespace {

juce::ApplicationProperties*& propertiesSlot() {
    static juce::ApplicationProperties* properties = nullptr;
    return properties;
}

// Owns the tooltip window of one dialog and deletes it first when the dialog starts to go, so the tip (and its fading
// ghost) never outlives the window it is a child of. Deletes itself with the window.
class DialogTooltipOwner final : private juce::ComponentListener {
public:
    explicit DialogTooltipOwner(juce::DialogWindow& window)
        : window_(&window)
        , tooltip_(std::make_unique<AppTooltipWindow>(&window, appProperties())) {
        tooltip_->setComponentID("dialogTooltip"); // test mode reads and screenshots the tip by this id
        window.addComponentListener(this);
    }

private:
    void componentBeingDeleted(juce::Component&) override {
        tooltip_.reset();
        window_->removeComponentListener(this);
        delete this;
    }

    juce::DialogWindow* window_;
    std::unique_ptr<AppTooltipWindow> tooltip_;
};

} // namespace

void setAppProperties(juce::ApplicationProperties* properties) noexcept { propertiesSlot() = properties; }

juce::ApplicationProperties* appProperties() noexcept { return propertiesSlot(); }

void attach(juce::DialogWindow& dialog) { new DialogTooltipOwner(dialog); }

AppTooltipWindow* tooltipOf(const juce::Component& dialog) {
    for (auto* child : dialog.getChildren())
        if (auto* tip = dynamic_cast<AppTooltipWindow*>(child))
            return tip;
    return nullptr;
}

int countIn(const juce::Component& dialog) {
    int count = 0;
    for (auto* child : dialog.getChildren())
        if (dynamic_cast<AppTooltipWindow*>(child) != nullptr)
            ++count;
    return count;
}

} // namespace synth::ui::dialog_tooltip
