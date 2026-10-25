#pragma once

#include "Music/SongKey.h"
#include "UI/Layout/KeyPicker/KeyPickerPalette.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** One line of the key list: a group header, or a scale shown at the picker's current root. */
class KeyPickerRow
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kRowHeight = 24;
    static constexpr int kHeaderHeight = 20;

    /** A header row. */
    explicit KeyPickerRow(juce::String header);
    /** A scale row; `mask` is the scale's root-relative notes. */
    KeyPickerRow(juce::String scaleName, std::uint16_t mask, int root);

    std::function<void(KeyPickerRow&)> onClick;
    std::function<void(KeyPickerRow&)> onHover;

    bool isHeader() const noexcept { return header_; }
    const juce::String& scaleName() const noexcept { return name_; }
    std::uint16_t mask() const noexcept { return mask_; }
    MusicalScale key() const { return makeScaleFromMask(root_, mask_, name_); }
    int preferredHeight() const noexcept { return header_ ? kHeaderHeight : kRowHeight; }

    void setRoot(int root);
    void setTicked(bool ticked);
    bool isTicked() const noexcept { return ticked_; }
    void setHighlighted(bool on);
    void setPalette(const keypicker::Palette& palette);
    void setQuery(const juce::String& query);

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void refreshTexts();

    bool header_ = false;
    juce::String name_;
    std::uint16_t mask_ = 0;
    int root_ = 0;
    juce::String text_;
    juce::String detail_;
    juce::String query_;
    bool ticked_ = false;
    bool highlighted_ = false;
    keypicker::Palette palette_;
};

/** A flat action button painted from the picker's palette (a parentless call-out has no themed LookAndFeel). */
class KeyPickerButton : public juce::Button {
public:
    KeyPickerButton(const juce::String& title, const juce::String& tooltip);
    void setPalette(const keypicker::Palette& palette);
    void paintButton(juce::Graphics& g, bool over, bool down) override;

private:
    keypicker::Palette palette_;
};

/** The "Follow song key" row at the top: a dot (filled while on), the label and a muted detail at the right. */
class KeyPickerFollowRow : public juce::Button {
public:
    explicit KeyPickerFollowRow(const juce::String& tooltip);
    void setState(bool on, const juce::String& detail);
    void setPalette(const keypicker::Palette& palette);
    void paintButton(juce::Graphics& g, bool over, bool down) override;

private:
    bool on_ = false;
    juce::String detail_;
    keypicker::Palette palette_;
};

} // namespace synth::ui
