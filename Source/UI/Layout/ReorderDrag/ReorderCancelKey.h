#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// ReorderCancelKey.h: Esc during a reorder drag. A mouse press does not move keyboard focus, so
// the dragged component never sees the key; this listens on the top-level window for exactly the
// length of the gesture instead.
namespace synth::ui {

class ReorderCancelKey : public juce::KeyListener {
public:
    ~ReorderCancelKey() override { disarm(); }

    /** Starts listening on `anywhere`'s top-level component; `onCancel` runs on Esc. */
    void arm(juce::Component& anywhere, std::function<void()> onCancel) {
        disarm();
        host_ = anywhere.getTopLevelComponent();
        onCancel_ = std::move(onCancel);
        if (host_ != nullptr)
            host_->addKeyListener(this);
    }

    void disarm() {
        if (host_ != nullptr)
            host_->removeKeyListener(this);
        host_ = nullptr;
        onCancel_ = nullptr;
    }

    bool isArmed() const noexcept { return onCancel_ != nullptr; }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override {
        if (!isArmed() || key != juce::KeyPress::escapeKey)
            return false;
        const auto cancel = onCancel_;
        cancel();
        return true;
    }

private:
    juce::Component::SafePointer<juce::Component> host_;
    std::function<void()> onCancel_;
};

} // namespace synth::ui
