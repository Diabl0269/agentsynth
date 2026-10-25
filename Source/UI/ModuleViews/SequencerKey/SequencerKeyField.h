#pragma once

#include "Modules/Sequencer/SequencerModuleBase.h"
#include "UI/Layout/FollowKeyField.h"
#include <functional>

namespace synth {
class TransportService; // Source/Transport/TransportService.h
} // namespace synth

namespace synth::ui {

/** The Sequencer and Poly Sequencer card's Key: follows the song key or holds the module's own Key and Scale. A pick
 *  writes Key, Scale, Follow Song Key (and the custom mask for a scale that is no preset) as one undo step; nothing
 *  rewrites a note. docs/modules/sequencer-design.md#key. */
class SequencerKeyField : public FollowKeyField {
public:
    explicit SequencerKeyField(SequencerModuleBase& module);

    /** Non-owning; null reads as No key. */
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    /** Runs `edit` as one undo step; unset = the edit runs unrecorded. */
    void setRecordEdit(std::function<void(const std::function<void()>& edit)> record) {
        recordEdit_ = std::move(record);
    }
    bool hasRecordEdit() const noexcept { return static_cast<bool>(recordEdit_); }

    /** Message thread: re-reads the module and the song key (the card's 15 Hz poll). */
    void refresh();
    /** Gives the module `key` as its own key (nullopt = No scale). */
    void pickOwn(const std::optional<MusicalScale>& key);
    /** Turns following back on; the own key is kept for later. */
    void follow();

private:
    void runEdit(const std::function<void()>& edit);

    SequencerModuleBase& module_;
    synth::TransportService* transport_ = nullptr;
    std::function<void(const std::function<void()>&)> recordEdit_;

    JUCE_DECLARE_NON_COPYABLE(SequencerKeyField)
};

} // namespace synth::ui
