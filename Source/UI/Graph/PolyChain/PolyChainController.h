#pragma once

#include "Mixer/ChannelFlows/PolyVoiceGraph.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

class GraphEditor;

namespace synth::ui {

/** What the user picked when a Poly click would also change modules on other tracks. */
enum class PolyChainChoice { cancel, thisTrackOnly, includeConnectedTracks };

/** The wording of that question, kept apart from the window that shows it. */
struct PolyChainPrompt {
    int otherModuleCount = 0;
    juce::String trackNames; ///< "Bass and Lead".
    juce::String message;    ///< "This also changes 2 modules on Bass and Lead."
};

/**
 * The Poly pill's behaviour (docs/modules/modulation.md#the-poly-toggle): a user click on the pill switches the whole
 * voice graph, not one module. One controller per GraphEditor; the card routes the click here and the rest of the
 * editor routes a new cable here. Programmatic sets (undo/redo restore, project load, the AI, MIDI Learn and
 * automation) change the parameter and never reach this class, so they never propagate.
 *
 *  * polyPillClicked plans the voice graph (planPolyVoiceGraph), asks first when other tracks' modules would change,
 *    then flips the chosen modules and inserts or removes the Poly MIDI node as ONE undo step.
 *  * joinConnectedModules lets a module that had no cables join the poly state of the module it is first cabled to.
 *
 * Message thread only.
 */
class PolyChainController {
public:
    using ShowToast = std::function<void(const juce::String& message, const juce::String& actionLabel,
                                         std::function<void()> action, const juce::String& actionTooltip)>;
    using Confirm = std::function<void(const PolyChainPrompt&, std::function<void(PolyChainChoice)> answer)>;

    explicit PolyChainController(GraphEditor& editor);
    ~PolyChainController();

    /** The timeline's tracks in order (source node uuid + name). Unset: no track owns anything. Set by MainComponent.
     */
    std::function<std::vector<synth::PolyTrackRef>()> trackProvider;
    /** Shows a transient message with an action. Unset (tests, plugin): no feedback. */
    ShowToast showToast;
    /** Test seam: answers the other-tracks question without a window. Unset: a juce::AlertWindow. */
    Confirm confirmForTest;

    /** The pill of `clicked` was clicked: `poly` is the value it asks for. The pill itself never changes state here;
     *  it follows the parameter, so a cancelled question leaves it as it was. */
    void polyPillClicked(juce::AudioProcessorGraph::NodeID clicked, bool poly);

    /** A cable is about to join `source` and `destination`: a poly-capable module with no cable yet joins the poly
     *  state of a poly-ON poly-capable (or Poly MIDI) module on the other end. Inside the cable's own undo step. */
    void joinConnectedModules(juce::AudioProcessorGraph::NodeID source, juce::AudioProcessorGraph::NodeID destination);

    /** Keeps joinConnectedModules quiet while alive: for cables that restore wiring rather than start new ones. */
    class SuppressJoin {
    public:
        explicit SuppressJoin(PolyChainController& controller)
            : controller_(controller) {
            ++controller_.suppressJoin_;
        }
        ~SuppressJoin() { --controller_.suppressJoin_; }
        SuppressJoin(const SuppressJoin&) = delete;
        SuppressJoin& operator=(const SuppressJoin&) = delete;

    private:
        PolyChainController& controller_;
    };

private:
    void ask(const synth::PolyVoiceGraphPlan& plan, std::function<void(PolyChainChoice)> answer);
    void commit(const std::vector<juce::AudioProcessorGraph::NodeID>& nodes, bool poly);
    void announce(int changed, bool poly, int editSerialAfter);
    bool hasCables(juce::AudioProcessorGraph::NodeID node) const;

    GraphEditor& editor_;
    int suppressJoin_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace synth::ui
