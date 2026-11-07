#pragma once

// ColourPickerFields.h -- the two painted colour controls of the colour picker, with the keyboard and screen-reader
// support juce::ColourSelector's own colour space and hue bar lack: a square for saturation and brightness and a bar
// for hue. Both hold the colour as hue, saturation and brightness, report a user edit through onChange and follow the
// owner through setHsb (no callback). docs/development/accessibility.md.
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

struct Hsb {
    float hue = 0.0f;        ///< 0..1 (0 and 1 are both red)
    float saturation = 0.0f; ///< 0..1
    float brightness = 1.0f; ///< 0..1

    juce::Colour toColour() const { return juce::Colour(hue, saturation, brightness, 1.0f); }
    static Hsb fromColour(juce::Colour colour);
};

class ColourPickerField
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    ColourPickerField();

    /** Runs when the user changes the colour here (key or mouse), with the new value. */
    std::function<void(const Hsb&)> onChange;

    /** Follows the owner: shows `hsb`, tells no one. */
    void setHsb(const Hsb& hsb);
    const Hsb& hsb() const noexcept { return hsb_; }

    /** What a screen reader reads as the value, e.g. "Hue 210 degrees". */
    virtual juce::String valueText() const = 0;

    void paint(juce::Graphics& g) final;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

protected:
    /** The control's own drawing, under the marker and the focus ring. */
    virtual void paintContent(juce::Graphics& g, juce::Rectangle<int> area) = 0;
    virtual void paintMarker(juce::Graphics& g, juce::Rectangle<int> area) = 0;
    /** The user moved it: applies `next`, repaints and reports. */
    void userChanged(const Hsb& next);
    void refreshDescription();

    static constexpr int kEdge = 3;
    Hsb hsb_;
};

class SaturationBrightnessField final : public ColourPickerField {
public:
    SaturationBrightnessField();
    juce::String valueText() const override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override { mouseDrag(e); }
    void mouseDrag(const juce::MouseEvent& e) override;
    void resized() override { image_ = {}; }

private:
    void paintContent(juce::Graphics& g, juce::Rectangle<int> area) override;
    void paintMarker(juce::Graphics& g, juce::Rectangle<int> area) override;

    juce::Image image_;
    float imageHue_ = -1.0f;
};

class HueBar final : public ColourPickerField {
public:
    HueBar();
    juce::String valueText() const override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& e) override { mouseDrag(e); }
    void mouseDrag(const juce::MouseEvent& e) override;

private:
    void paintContent(juce::Graphics& g, juce::Rectangle<int> area) override;
    void paintMarker(juce::Graphics& g, juce::Rectangle<int> area) override;
};

} // namespace synth::ui
