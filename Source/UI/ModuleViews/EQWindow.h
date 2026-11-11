#pragma once
#include <algorithm>

#include "EQBandStrip.h"
#include "EQBandTypePill.h"
#include "EQCurveComponent.h"
#include "EQSoloButton.h"
#include "UI/Layout/DialogKeyboard.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

/** Dialog content for the pop-out Parametric EQ editor.
 *
 *  The module card's inline curve is necessarily small; this hosts a second EQCurveComponent on
 *  the same module so the same points can be edited at a comfortable size. Both views read the
 *  module's parameters and write through the same setters, so they stay in sync automatically —
 *  each one's 30 Hz timer picks the other's edits up on the next tick.
 *
 *  Follows the SettingsWindow pattern: this is just the content Component; the caller wraps it in
 *  a juce::DialogWindow via PopupMotion::launchDialog().
 *
 *  Layout: the curve, the band strip under it (EQBandStrip), then the toolbar: the spectrum toggle, a reserved
 *  empty slot for the Compare and Suggest buttons, the selected band's type pill, the solo (headphones) button and
 *  the hint.
 *
 *  Keyboard: Tab visits the curve (which edits with the keys listed on EQCurveComponent, S solos and Escape ends a
 *  solo first), the band chips, the spectrum toggle, the type pill and the solo button; Escape closes the window. A
 *  solo the window's curve started ends when the window closes.
 */
class EQWindow : public juce::Component {
    /** A label whose tooltip is built when the pointer rests on it, so it can name keys as they are bound then. */
    class LiveTipLabel : public juce::Label {
    public:
        std::function<juce::String()> liveTooltip;
        juce::String getTooltip() override { return liveTooltip ? liveTooltip() : juce::Label::getTooltip(); }
    };

public:
    explicit EQWindow(ParametricEQModule& eq) {
        curve = std::make_unique<EQCurveComponent>(eq);
        curve->setComponentID("eqWindowCurve");
        addAndMakeVisible(*curve);

        strip = std::make_unique<synth::ui::EQBandStrip>(*curve);
        strip->setComponentID("eqWindowStrip");
        strip->onPreferredHeightChanged = [this] { resized(); };
        addAndMakeVisible(*strip);

        typePill = std::make_unique<synth::ui::EQBandTypePill>(*curve);
        addAndMakeVisible(*typePill);

        soloButton = std::make_unique<synth::ui::EQSoloButton>(*curve);
        soloButton->setComponentID("eqWindowSolo");
        addAndMakeVisible(*soloButton);

        spectrumToggle.setButtonText("Show Spectrum");
        spectrumToggle.setTooltip("Draw the live spectrum of the signal behind the curve");
        spectrumToggle.setToggleState(curve->getShowSpectrum(), juce::dontSendNotification);
        spectrumToggle.onClick = [this] { curve->setShowSpectrum(spectrumToggle.getToggleState()); };
        addAndMakeVisible(spectrumToggle);

        hint.setText("Double-click adds or removes", juce::dontSendNotification);
        hint.liveTooltip = [this] { return hintTooltip(); };
        hint.setJustificationType(juce::Justification::centredLeft);
        hint.setInterceptsMouseClicks(false, true);
        addAndMakeVisible(hint);

        setSize(820, 470);
    }

    ~EQWindow() override = default;

    /** Keeps the hosting window no smaller than the footer needs: the toggle, type pill and hint side by side. The
     *  limits go on after the window has been sized to this content: set while the window is being built, they clamped
     *  it to the smallest size and the window opened at 480 x 320 instead of 820 x 470. */
    void parentHierarchyChanged() override {
        if (auto* window = findParentComponentOfClass<juce::ResizableWindow>())
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<juce::ResizableWindow>(window)] {
                if (safe != nullptr)
                    safe->setResizeLimits(kMinWidth, kMinHeight, 4000, 4000);
            });
    }

    void resized() override {
        auto area = getLocalBounds().reduced(10);
        auto footer = area.removeFromBottom(26);
        spectrumToggle.setBounds(footer.removeFromLeft(130));
        footer.removeFromLeft(kFooterGap);
        // The reserved slot gives way first when the window is narrow, so the pill and hint never overlap the toggle.
        const int slotWidth =
            std::clamp(footer.getWidth() - kTypePillWidth - kSoloButtonWidth - kHintMinWidth - 3 * kFooterGap, 0,
                       kReservedToolbarSlotWidth);
        reservedToolbarSlot = footer.removeFromLeft(slotWidth);
        if (slotWidth > 0)
            footer.removeFromLeft(kFooterGap);
        typePill->setBounds(footer.removeFromLeft(kTypePillWidth).withSizeKeepingCentre(kTypePillWidth, 22));
        footer.removeFromLeft(kFooterGap);
        soloButton->setBounds(footer.removeFromLeft(kSoloButtonWidth).withSizeKeepingCentre(kSoloButtonWidth, 22));
        footer.removeFromLeft(kFooterGap);
        hint.setBounds(footer);
        area.removeFromBottom(6);
        const int stripHeight = std::min(strip->heightForWidth(area.getWidth()), area.getHeight() / 3);
        strip->setBounds(area.removeFromBottom(stripHeight));
        area.removeFromBottom(6);
        curve->setBounds(area);
    }

    /** Fires when Escape is pressed. Left unset, Escape closes the dialog window hosting this
     *  content. */
    std::function<void()> onRequestClose;

    /** Hands a key this window does not use (undo, redo, play/stop) to the app; see makeAppShortcutRelay. */
    std::function<bool(const juce::KeyPress&)> onAppShortcut;

    bool keyPressed(const juce::KeyPress& key) override {
        if (key != juce::KeyPress::escapeKey)
            return onAppShortcut && onAppShortcut(key);
        if (onRequestClose)
            onRequestClose();
        else
            synth::ui::closeHostingWindow(*this);
        return true;
    }

    /** Lets an undo or redo glide the window's curve (EQCurveComponent::setUndoManager). */
    void setUndoManager(AppUndoManager* manager) { curve->setUndoManager(manager); }

    /** Resolves the curve's keys, and every tooltip that names one, through `manager`. Must outlive this window. */
    void setShortcutManager(const ShortcutManager* manager) { curve->setShortcutManager(manager); }

    /** Forwards the undo bracketing hooks to the hosted curve. */
    void setGestureCallbacks(std::function<void()> onStart, std::function<void()> onEnd) {
        curve->onGestureStart = std::move(onStart);
        curve->onGestureEnd = std::move(onEnd);
    }

    /** Width of the empty toolbar area right of the spectrum toggle, kept for the Compare and Suggest buttons. */
    static constexpr int kReservedToolbarSlotWidth = 220;
    static constexpr int kTypePillWidth = 110;
    static constexpr int kSoloButtonWidth = 28;
    static constexpr int kFooterGap = 6;
    static constexpr int kMinWidth = 480;
    static constexpr int kMinHeight = 320;
    static constexpr int kHintMinWidth = 170;
    juce::Rectangle<int> getReservedToolbarSlot() const { return reservedToolbarSlot; }

    // Testing hooks
    EQCurveComponent& getCurve() { return *curve; }
    synth::ui::EQBandStrip& getStrip() { return *strip; }
    synth::ui::EQBandTypePill& getTypePill() { return *typePill; }
    synth::ui::EQSoloButton& getSoloButton() { return *soloButton; }
    juce::Label& getHintForTest() { return hint; }
    juce::ToggleButton& getSpectrumToggle() { return spectrumToggle; }

private:
    juce::String hintTooltip() const {
        const auto* manager = curve->getShortcutManager();
        juce::StringArray keys;
        const auto add = [&](const char* id, const char* what) {
            const auto key = EQCurveComponent::keyHint(manager, id);
            if (key.isNotEmpty())
                keys.add(key + " " + what);
        };
        add("eqSolo", "solos");
        add("eqTypeMenu", "sets the type");
        add("eqAddBand", "adds");
        add("eqDuplicateBand", "duplicates");
        add("eqRemoveBand", "removes");
        return "Double-click the curve to add or remove a band; right-click a band to set its type; Option-click solos "
               "it. Keys on the selected band: " +
               keys.joinIntoString(", ");
    }

    std::unique_ptr<EQCurveComponent> curve;
    std::unique_ptr<synth::ui::EQBandStrip> strip;
    std::unique_ptr<synth::ui::EQBandTypePill> typePill;
    std::unique_ptr<synth::ui::EQSoloButton> soloButton;
    juce::Rectangle<int> reservedToolbarSlot;
    juce::ToggleButton spectrumToggle;
    LiveTipLabel hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQWindow)
};
