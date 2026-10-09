#pragma once

// One connection of the port connections panel: a swatch in the cable's colour, "<module> . <port>" at the far end
// and a remove button. Pure view: it reports what the user did and the panel applies it to the graph. Hovering the
// row or focusing its remove button reports "highlight this cable" so the canvas can show which one it is.
// docs/layout/cables.md#port-connections-panel.

#include "PortConnectionList.h"
#include "UI/Graph/ModDot/ModDotGlyphButton.h"
#include "UI/Graph/ModDot/ModDotMotion.h"
#include <functional>

namespace synth::ui {

class PortConnectionRow final : public juce::Component {
public:
    static constexpr int kHeight = 30;

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

    ModDotGlyphButton& removeButton() noexcept { return removeButton_; }
    juce::Colour swatchColour() const noexcept { return connection_.colour; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void applyNames();
    void refreshHighlight();

    PortConnection connection_;
    bool hovered_ = false;
    bool focused_ = false;
    bool highlighted_ = false;
    ModDotHoverFade hover_;
    ModDotGlyphButton removeButton_;
    juce::Rectangle<int> swatchArea_;
    juce::Rectangle<int> nameArea_;
};

} // namespace synth::ui
