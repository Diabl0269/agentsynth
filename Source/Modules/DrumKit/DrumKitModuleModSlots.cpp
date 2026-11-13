// DrumKitModuleModSlots.cpp -- the Drum Kit's pad cables. A pad knob has no CV input of its own, so the module keeps
// kMaxPadModSlots reserved input channels above its list channels; a cable dropped on a pad knob claims a slot for
// that pad's parameter (bindModSlot) and is wired to the slot's channel like any CV jack.
//
// The message thread owns modSlotKeys_ (pad * 4 + field, -1 = free) and mirrors every change into modSlotKeysAudio_,
// which is all the audio thread reads: padValuesAt adds the slot's CV on top of the pad's own value for one strike and
// writes nothing back. The kit saves its slots only while at least one is bound, so an untouched kit's state is
// unchanged.
#include "DrumKitModule.h"
#include <algorithm>

namespace {
constexpr std::array<const char*, 4> kFieldSuffixes = {"Level", "Tune", "Decay", "Start"};

// pad * 4 + field for "pad<N><Level|Tune|Decay|Start>", else -1.
int keyOfParamId(const juce::String& id) {
    if (!DrumKitModule::isPadParamId(id))
        return -1;
    const auto rest = id.substring(3);
    const auto digits = rest.initialSectionContainingOnly("0123456789");
    const auto suffix = rest.substring(digits.length());
    for (size_t f = 0; f < kFieldSuffixes.size(); ++f)
        if (suffix == kFieldSuffixes[f])
            return (digits.getIntValue() - 1) * 4 + (int)f;
    return -1;
}

juce::String paramIdOfKey(int key) { return "pad" + juce::String((key >> 2) + 1) + kFieldSuffixes[(size_t)(key & 3)]; }
} // namespace

int DrumKitModule::firstFreeModSlot() const {
    for (int k = 0; k < kMaxPadModSlots; ++k)
        if (modSlotKeys_[(size_t)k] < 0)
            return k;
    return -1;
}

int DrumKitModule::bindModSlot(const juce::String& paramId) {
    const int key = keyOfParamId(paramId);
    if (key < 0)
        return -1;
    if (const int existing = modChannelForParam(paramId); existing >= 0)
        return existing;
    const int slot = firstFreeModSlot();
    if (slot < 0)
        return -1;
    modSlotKeys_[(size_t)slot] = key;
    modSlotKeysAudio_[(size_t)slot].store(key, std::memory_order_release);
    return kPadModSlotBase + slot;
}

void DrumKitModule::releaseUnconnectedModSlots(const std::function<bool(int rawChannel)>& isConnected) {
    for (int k = 0; k < kMaxPadModSlots; ++k)
        if (modSlotKeys_[(size_t)k] >= 0 && !isConnected(kPadModSlotBase + k)) {
            modSlotKeysAudio_[(size_t)k].store(-1, std::memory_order_release);
            modSlotKeys_[(size_t)k] = -1;
        }
}

int DrumKitModule::modChannelForParam(const juce::String& paramId) const {
    const int key = keyOfParamId(paramId);
    if (key < 0)
        return -1;
    for (int k = 0; k < kMaxPadModSlots; ++k)
        if (modSlotKeys_[(size_t)k] == key)
            return kPadModSlotBase + k;
    return -1;
}

bool DrumKitModule::canModulateParam(const juce::String& paramId) const {
    return keyOfParamId(paramId) >= 0 && (modChannelForParam(paramId) >= 0 || firstFreeModSlot() >= 0);
}

juce::String DrumKitModule::modSlotParamId(int slot) const {
    return slot >= 0 && slot < kMaxPadModSlots && modSlotKeys_[(size_t)slot] >= 0
               ? paramIdOfKey(modSlotKeys_[(size_t)slot])
               : juce::String();
}

std::vector<ModulationTarget> DrumKitModule::getModulationTargets() const {
    auto targets = mods_.targets();
    for (int k = 0; k < kMaxPadModSlots; ++k) {
        const auto id = modSlotParamId(k);
        if (id.isEmpty())
            continue;
        const int key = modSlotKeys_[(size_t)k];
        const auto* param = padParameter(key >> 2, (PadParam)(key & 3));
        targets.push_back({param != nullptr ? param->getName(64) : id, kPadModSlotBase + k, id, /*knobOnly*/ true});
    }
    return targets;
}

juce::String DrumKitModule::getInputPortLabel(int i) const {
    if (i < kPadModSlotBase)
        return mods_.label(i);
    const auto id = modSlotParamId(i - kPadModSlotBase);
    const int key = keyOfParamId(id);
    return key >= 0 ? padParameter(key >> 2, (PadParam)(key & 3))->getName(64) : juce::String();
}

// A slot is reached only through its knob; its "jack" index is the raw channel itself, so it never shares a jack.
LogicalPort DrumKitModule::mapInputChannel(int raw) const {
    auto port = ModuleBase::mapInputChannel(raw);
    port.role = PortRole::ModCV;
    if (raw >= kPadModSlotBase) {
        port.visibleJackIndex = raw;
        port.isPolyGroupHead = true;
    }
    return port;
}

// A pad cable is a UI-only binding: pad parameters stay out of the AI's view.
bool DrumKitModule::isAutoPromotableModTarget(int dstChannel) const {
    return dstChannel < kPadModSlotBase && ModuleBase::isAutoPromotableModTarget(dstChannel);
}

bool DrumKitModule::isReservedInputChannel(int rawChannel) const {
    return rawChannel >= kPadModSlotBase &&
           modSlotKeys_[(size_t)juce::jlimit(0, kMaxPadModSlots - 1, rawChannel - kPadModSlotBase)] < 0;
}

juce::var DrumKitModule::modSlotsToVar() const {
    juce::Array<juce::var> slots;
    for (int k = 0; k < kMaxPadModSlots; ++k) {
        if (modSlotKeys_[(size_t)k] < 0)
            continue;
        auto* entry = new juce::DynamicObject();
        entry->setProperty("slot", k);
        entry->setProperty("paramId", modSlotParamId(k));
        slots.add(juce::var(entry));
    }
    return slots.isEmpty() ? juce::var() : juce::var(slots);
}

// Replaces the whole table. A slot number out of range, an id that is no pad knob and a repeat are dropped: the file
// is ours, but not trusted to be well formed.
void DrumKitModule::modSlotsFromVar(const juce::var& slots) {
    std::array<int, kMaxPadModSlots> keys;
    keys.fill(-1);
    if (auto* array = slots.getArray())
        for (const auto& entry : *array) {
            const int slot = entry.getProperty("slot", -1);
            const int key = keyOfParamId(entry.getProperty("paramId", {}).toString());
            if (slot >= 0 && slot < kMaxPadModSlots && key >= 0 && keys[(size_t)slot] < 0 &&
                std::find(keys.begin(), keys.end(), key) == keys.end())
                keys[(size_t)slot] = key;
        }
    for (int k = 0; k < kMaxPadModSlots; ++k) {
        modSlotKeys_[(size_t)k] = keys[(size_t)k];
        modSlotKeysAudio_[(size_t)k].store(keys[(size_t)k], std::memory_order_release);
    }
}
