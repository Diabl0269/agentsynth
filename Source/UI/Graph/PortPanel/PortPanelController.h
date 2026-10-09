#pragma once

// The port connections panel's behaviour: a plain click on any jack opens a small panel beside it listing what the jack
// is wired to (PortConnectionsPanel), a second click on the same jack closes it. One controller per GraphEditor; cards
// route a jack's release and double-click here, and the panel's edits go through GraphEditor.
//
//  * A click is a left press and release under 3 px apart that is the first of its clicks (a drag from the jack still
//    makes a cable and opens nothing; the right-click menu is unchanged).
//  * A jack with exactly one cable and the "double-click a port to disconnect" preference on waits one double-click
//    interval before opening, so the double-click that disconnects it never flashes the panel; a jack with none or
//    several cables opens at once, and a double-click on one with several keeps the panel instead of disconnecting.
//  * Hovering or keyboard-focusing a row highlights that cable on the canvas and dims all others (the canvas paint
//    reads highlightedCable()).
//  * The panel's Add connection page: a hovered or focused target previews a dashed cable from the jack to it
//    (paintPreview(), drawn by the canvas), and a pick goes through connect() -> PortConnector.
//
// docs/layout/cables.md#port-connections-panel.

#include "PortConnectionList.h"
#include "PortTargetList.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <functional>
#include <memory>
#include <optional>

namespace synth::ui {

class ModDotPanelFrame;
class PortConnectionsPanel;

/** How far the cables other than the highlighted one are dimmed: their alpha is multiplied by this. */
inline constexpr float kPortPanelDimAlpha = 0.35f;

class PortPanelController {
public:
    static constexpr float kClickThresholdPx = 3.0f;

    explicit PortPanelController(GraphEditor& editor);
    ~PortPanelController();

    // ---- from the card ----
    /** A jack was released (`e` in the card's space): a plain click opens the panel, or closes it when it is this
     * jack's. */
    void jackReleased(ModuleComponent& card, const ModuleComponent::Port& port, const juce::MouseEvent& e);
    /** A double-click on a connected jack, before it would disconnect. True when the jack has several cables: the panel
     *  is kept (opened when it was not) and nothing is disconnected. A single cable: drops the pending open and the
     *  panel and returns false, for the caller to disconnect. */
    bool keepPanelForDoubleClick(ModuleComponent& card, const ModuleComponent::Port& port);
    /** The press of a double-click that something else took (the knob's mod dot): no panel from the first click. */
    void cancelPendingOpen();

    // ---- the panel ----
    /** Shows `content` in a window beside `screenAnchor` (the jack, in screen space); a headless test captures the
     *  content instead (a real window needs a display). */
    using PanelLauncher = std::function<void(std::unique_ptr<juce::Component> content, juce::Component& anchor,
                                             juce::Rectangle<int> screenAnchor)>;
    PanelLauncher panelLauncher;
    /** Opens the panel for `port` (replacing another). */
    void open(ModuleComponent& card, const ModuleComponent::Port& port);
    /** The open panel, or null. */
    PortConnectionsPanel* getPanel() const;
    bool isOpenFor(const PortRef& port) const;
    void close();
    /** The editor's 30 Hz tick: the open panel follows the graph. */
    void tick();
    /** Called by the panel as it goes away. */
    void panelClosed(PortConnectionsPanel* panel);
    /** Removes every cable on the jack as one undo step (GraphEditor::disconnectPort). */
    void disconnectAll(const PortRef& port);

    // ---- cable highlight ----
    void setHighlightedCable(const GraphEditor::CableId& id);
    void clearHighlightIf(const GraphEditor::CableId& id);
    void clearHighlight();
    const std::optional<GraphEditor::CableId>& highlightedCable() const noexcept { return highlighted_; }

    // ---- adding a connection ----
    /** Connects `from` to `target` (one undo step; the new cable grows in). False when nothing was made. */
    bool connect(const PortRef& from, const PortTarget& target);
    /** Previews the cable `from` -> `target` as a dashed wire on the canvas; null clears it. */
    void setPreview(const PortRef& from, const PortTarget* target);
    void clearPreview();
    bool hasPreview() const noexcept { return preview_.has_value(); }
    /** The preview's ends in canvas coordinates (source end first), for the canvas paint and a test. */
    struct Preview {
        juce::Point<float> p1, p2;
        juce::Colour colour;
    };
    const std::optional<Preview>& preview() const noexcept { return preview_; }
    /** Draws the preview in canvas coordinates; nothing when there is none. */
    void paintPreview(juce::Graphics& g) const;

    // Test seams.
    bool hasPendingOpenForTest() const noexcept { return pending_.isTimerRunning(); }
    /** Runs the deferred open now, as its timer would. */
    void firePendingOpenForTest() { pending_.fire(); }

private:
    struct PendingOpen final : juce::Timer {
        std::function<void()> action;
        void fire() {
            stopTimer();
            if (auto run = std::move(action))
                run();
            action = nullptr;
        }
        void timerCallback() override { fire(); }
    };

    ModuleComponent* cardFor(juce::AudioProcessorGraph::NodeID node) const;
    PortRef refFor(const ModuleComponent& card, const ModuleComponent::Port& port) const;
    void launch(ModuleComponent& card, const ModuleComponent::Port& port, std::unique_ptr<PortConnectionsPanel> panel);

    GraphEditor& editor_;
    juce::Component::SafePointer<juce::Component> panel_;
    juce::Component::SafePointer<juce::Component> frame_; // the window the panel sits in (null with a test launcher)
    bool dismissed_ = false;
    std::optional<GraphEditor::CableId> highlighted_;
    std::optional<Preview> preview_;
    PendingOpen pending_;
};

} // namespace synth::ui
