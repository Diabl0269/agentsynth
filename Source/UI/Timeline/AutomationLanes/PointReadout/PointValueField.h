#pragma once

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

// PointValueField -- the small inline text field an automation lane editor opens beside a point so its value can be
// typed. Return commits, Escape cancels, losing focus commits what parses and drops what does not. It fades in and
// out, and is a child of its owner.
//
// The owner must outlive it and set `parse` and `onCommit` before open(). Message thread only.
namespace synth::ui {

class PointValueField : public juce::TextEditor {
public:
    static constexpr double kFadeInMs = 140.0;
    static constexpr double kFadeOutMs = 100.0;

    explicit PointValueField(juce::Component& owner);
    ~PointValueField() override;

    // The value `text` stands for, or nullopt when it is not a value; Return keeps the field open on nullopt.
    std::function<std::optional<double>(const juce::String&)> parse;
    // Receives the parsed value, after the field has closed.
    std::function<void(double)> onCommit;

    // Opens beside `anchor` (the point's centre, owner coordinates) with `text` selected and the keyboard focus.
    // `name` is the accessible title. Opening an open field re-seeds it.
    void open(juce::Point<float> anchor, const juce::String& text, const juce::String& name);
    // Closes the field; `commit` also parses and commits the current text (dropped when it does not parse). The
    // keyboard focus returns to the owner unless the field is closing because it lost focus.
    void close(bool commit);

    bool isOpen() const noexcept { return open_; }
    // True while the typed text does not parse; the field is drawn in the error colour.
    bool isInvalid() const noexcept { return invalid_; }
    // 0 hidden .. 1 settled; also the field's alpha.
    float getOpacity() const noexcept { return opacity_; }

    // A plain number with an optional unit after it ("-12", "-12 dB", "3.5%"); nullopt for anything else.
    static std::optional<double> parseNumber(const juce::String& text);

    bool keyPressed(const juce::KeyPress& key) override;
    void lookAndFeelChanged() override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    bool animates() const;
    void fade(bool in);
    void setOpacity(float value);
    void applyColours();
    void setInvalid(bool invalid);
    void finish(bool commit, bool refocus);
    void submit();

    juce::Component& owner_;
    bool open_ = false;
    bool invalid_ = false;
    float opacity_ = 0.0f;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver fade_;
};

} // namespace synth::ui
