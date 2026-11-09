#pragma once

// One connection of the port connections panel: a swatch in the cable's colour, "<module> . <port>" at the far end
// and a remove button. A modulation cable (an attenuverter chain) is the same row the knob's sources panel draws
// (ModDotSourceRow: amount bar, typed percent, curve chip, show in timeline) on two lines, the name on top, so a
// connection offers the same controls everywhere; the panel wires them (ModSourceRowWiring). Hovering the row or
// focusing its remove button reports "highlight this cable" so the canvas can show which one it is.
// docs/layout/cables.md#port-connections-panel.

#include "PortConnectionList.h"
#include "UI/Graph/ModDot/ModDotGlyphButton.h"
#include "UI/Graph/ModDot/ModDotMotion.h"
#include "UI/Graph/ModDot/ModDotSourceRow.h"
#include <functional>
#include <memory>

namespace synth::ui {

class PortConnectionRow final : public juce::Component {
public:
    static constexpr int kHeight = 30;    // an audio, MIDI or plain CV row, and a mod row's name line
    static constexpr int kModHeight = 56; // a modulation row: the name line, then the controls line

    /** The row's full height: kModHeight for a modulation, else kHeight. */
    static int heightFor(const PortConnection& connection) noexcept {
        return connection.isModulation() ? kModHeight : kHeight;
    }

    explicit PortConnectionRow(const PortConnection& connection);

    const GraphEditor::CableId& cableId() const noexcept { return connection_.cable.id; }
    const PortConnection& connection() const noexcept { return connection_; }
    /** Re-reads the far end's name and the cable's colour. */
    void update(const PortConnection& connection);
    /** True while the pointer is over the row or its remove button has keyboard focus. */
    bool isHighlighted() const noexcept { return highlighted_; }

    // What the user did.
    std::function<void(PortConnectionRow&)> onRemove;
    std::function<void(PortConnectionRow&, bool highlighted)> onHighlight;
    /** Up/Down on the row's button: the panel moves focus to the neighbouring control. */
    std::function<void(PortConnectionRow&, int step)> onNavigate;

    /** The remove button of whichever row this is. */
    ModDotGlyphButton& removeButton() noexcept { return mod_ != nullptr ? mod_->removeGlyph() : removeButton_; }
    /** The mod controls of a modulation connection (null on an audio, MIDI or plain CV one). */
    ModDotSourceRow* modRow() noexcept { return mod_.get(); }
    juce::Colour swatchColour() const noexcept { return connection_.colour; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusOfChildComponentChanged(FocusChangeType cause) override;

private:
    void applyNames();
    void refreshHighlight();
    KnobModSource modSource() const;

    PortConnection connection_;
    bool hovered_ = false;
    bool focused_ = false;
    bool highlighted_ = false;
    ModDotHoverFade hover_;
    ModDotGlyphButton removeButton_;
    std::unique_ptr<ModDotSourceRow> mod_; // set for a modulation connection; replaces the swatch, name and trash
    juce::Rectangle<int> swatchArea_;
    juce::Rectangle<int> nameArea_;
};

} // namespace synth::ui
