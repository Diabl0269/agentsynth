#pragma once

#include "MixerSectionLayout.h"
#include "UI/Layout/FadeVisibility.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// MixerSectionControls.h (docs/mixer/panel.md#shared-sections): the two small pieces every column
// repeats per section -- the divider under a section (resize drag, double-click
// reset) and the 14 px strip a hidden section leaves behind (click to show it again). Both act on the
// shared MixerSectionLayout, so a gesture in any one column changes every column.
namespace synth::ui {

class MixerSectionDivider
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    MixerSectionDivider();

    /** `layout` may be null (the divider then paints its idle line and ignores the mouse). */
    void setLayout(MixerSectionLayout* layout, MixerSection section);

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    bool isResizable() const noexcept;
    bool isHighlighted() const noexcept;

    MixerSectionLayout* layout_ = nullptr;
    MixerSection section_ = MixerSection::Inserts;
    int dragStartScreenY_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerSectionDivider)
};

class MixerCollapsedSection
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    MixerCollapsedSection();

    /** `layout` may be null (a click then does nothing). */
    void setLayout(MixerSectionLayout* layout, MixerSection section);
    /** What the hidden section holds, e.g. "2 sends"; empty paints a blank strip. */
    void setSummary(const juce::String& summary);
    const juce::String& getSummary() const noexcept { return summary_; }

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    MixerSectionLayout* layout_ = nullptr;
    MixerSection section_ = MixerSection::Inserts;
    juce::String summary_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerCollapsedSection)
};

/** The swap a section makes as it closes or opens: its content (the insert or send viewport; none for EQ, whose
 *  thumbnail is sized and dimmed by the column) fades out as its 14 px strip fades in, and back. The heights
 *  slide from the panel's shared MixerSectionLayout, so this only owns the two opacities. */
class MixerSectionSwap {
public:
    /** `content` may be null. */
    MixerSectionSwap(juce::Component* content, juce::Component& strip)
        : strip_({&strip}) {
        if (content != nullptr)
            content_ = std::make_unique<FadeVisibility>(std::initializer_list<juce::Component*>{content});
    }

    /** Idempotent, so every layout pass may call it. */
    void setHidden(bool hidden) {
        if (content_ != nullptr)
            content_->setShown(!hidden);
        strip_.setShown(hidden);
    }

private:
    std::unique_ptr<FadeVisibility> content_;
    FadeVisibility strip_;
};

/** "no inserts", "1 insert", "3 inserts" -- the collapsed strip's wording for `count` items. */
juce::String mixerSectionCountSummary(int count, const juce::String& singular, const juce::String& plural);

} // namespace synth::ui
