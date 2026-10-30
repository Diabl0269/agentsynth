// DrumKitPads.cpp -- the default pad table, the count rule and the saved form of the pads.
#include "DrumKitPads.h"
#include "Modules/ModuleFileStateKeys.h"

namespace synth::drums {

namespace {

struct DefaultPad {
    int note;
    int sound;
    int revealSound; // for a free pad: the built-in sound it gets when the count first shows it
    const char* name;
};

// 1-8 are the kit a new Drum Kit shows, 9-16 and 17-20 the rest of the General MIDI notes it always played, and
// 21-32 free notes with no sound until the count shows them (so a note an older project never heard stays silent).
constexpr std::array<DefaultPad, kMaxPads> kDefaults = {{
    {36, 36, kNoSound, "Kick"},          {38, 38, kNoSound, "Snare"},         {39, 39, kNoSound, "Clap"},
    {42, 42, kNoSound, "Closed hat"},    {46, 46, kNoSound, "Open hat"},      {45, 45, kNoSound, "Tom"},
    {49, 49, kNoSound, "Cymbal"},        {56, 56, kNoSound, "Cowbell"},       {37, 37, kNoSound, "Rim"},
    {44, 44, kNoSound, "Pedal hat"},     {41, 41, kNoSound, "Low floor tom"}, {43, 43, kNoSound, "High floor tom"},
    {47, 47, kNoSound, "Low-mid tom"},   {48, 48, kNoSound, "Hi-mid tom"},    {50, 50, kNoSound, "High tom"},
    {51, 51, kNoSound, "Ride"},          {35, 35, kNoSound, "Kick 2"},        {40, 40, kNoSound, "Snare 2"},
    {57, 57, kNoSound, "Crash 2"},       {59, 59, kNoSound, "Ride 2"},        {52, kNoSound, 49, "China"},
    {53, kNoSound, 51, "Ride bell"},     {54, kNoSound, 57, "Tambourine"},    {55, kNoSound, 57, "Splash"},
    {58, kNoSound, 56, "Vibraslap"},     {60, kNoSound, 50, "Hi bongo"},      {61, kNoSound, 48, "Low bongo"},
    {62, kNoSound, 47, "Mute hi conga"}, {63, kNoSound, 45, "Open hi conga"}, {64, kNoSound, 43, "Low conga"},
    {65, kNoSound, 48, "High timbale"},  {66, kNoSound, 41, "Low timbale"},
}};

const char* kindName(PadKind kind) {
    switch (kind) {
    case PadKind::Kit:
        return "kit";
    case PadKind::BuiltIn:
        return "builtIn";
    case PadKind::File:
        return "file";
    case PadKind::Instrument:
        return "instrument";
    }
    return "builtIn";
}

PadKind kindFromName(const juce::String& name) {
    if (name == "kit")
        return PadKind::Kit;
    if (name == "file")
        return PadKind::File;
    if (name == "instrument")
        return PadKind::Instrument;
    return PadKind::BuiltIn;
}

int clampNote(const juce::var& v, int fallback) { return v.isVoid() ? fallback : juce::jlimit(0, 127, (int)v); }

} // namespace

Pad defaultPad(int index) {
    const auto& d = kDefaults[(size_t)juce::jlimit(0, kMaxPads - 1, index)];
    Pad pad;
    pad.kind = d.sound == kNoSound ? PadKind::BuiltIn : PadKind::Kit;
    pad.sound = d.sound;
    pad.note = d.note;
    pad.name = d.name;
    return pad;
}

PadTable defaultPadTable() {
    PadTable table;
    for (int i = 0; i < kMaxPads; ++i)
        table.pads[(size_t)i] = defaultPad(i);
    table.count = kDefaultPadCount;
    return table;
}

int clampPadCount(int count) noexcept {
    const int rounded = ((juce::jmax(0, count) + kPadStep - 1) / kPadStep) * kPadStep;
    return juce::jlimit(kMinPads, kMaxPads, rounded);
}

int revealSoundFor(int index) noexcept {
    return index >= 0 && index < kMaxPads ? kDefaults[(size_t)index].revealSound : kNoSound;
}

juce::String noteName(int note) {
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    note = juce::jlimit(0, 127, note);
    return juce::String(names[note % 12]) + juce::String(note / 12 - 2);
}

void writePadState(const PadTable& table, juce::DynamicObject& state) {
    juce::Array<juce::var> pads, files;
    for (const auto& pad : table.pads) {
        juce::DynamicObject::Ptr p = new juce::DynamicObject();
        p->setProperty("kind", kindName(pad.kind));
        p->setProperty("sound", pad.sound);
        p->setProperty("note", pad.note);
        p->setProperty("name", pad.name);
        p->setProperty("shown", pad.shown);
        if (pad.instrumentUuid.isNotEmpty()) {
            p->setProperty("instrument", pad.instrumentUuid);
            p->setProperty("pitch", pad.instrumentPitch);
        }
        pads.add(juce::var(p.get()));
        files.add(pad.kind == PadKind::File ? pad.file : juce::String());
    }
    state.setProperty("padCount", table.count);
    state.setProperty("pads", pads);
    state.setProperty(module_file_keys::kPadFiles, files);
}

bool readPadState(const juce::DynamicObject& state, PadTable& table) {
    const auto* pads = state.getProperty("pads").getArray();
    if (pads == nullptr)
        return false;
    const auto* files = state.getProperty(module_file_keys::kPadFiles).getArray();
    PadTable read = defaultPadTable();
    read.count = clampPadCount(state.hasProperty("padCount") ? (int)state.getProperty("padCount") : kDefaultPadCount);
    for (int i = 0; i < kMaxPads && i < pads->size(); ++i) {
        const auto* obj = (*pads)[i].getDynamicObject();
        if (obj == nullptr)
            continue;
        auto& pad = read.pads[(size_t)i];
        pad.kind = kindFromName(obj->getProperty("kind").toString());
        const auto sound = obj->getProperty("sound");
        pad.sound = sound.isVoid() ? pad.sound : juce::jlimit(kNoSound, 127, (int)sound);
        pad.note = clampNote(obj->getProperty("note"), pad.note);
        if (obj->hasProperty("name"))
            pad.name = obj->getProperty("name").toString().substring(0, 64);
        pad.shown = obj->hasProperty("shown") ? (bool)obj->getProperty("shown") : true;
        pad.instrumentUuid = obj->getProperty("instrument").toString().substring(0, 64);
        pad.instrumentPitch = clampNote(obj->getProperty("pitch"), 60);
        if (files != nullptr && i < files->size())
            pad.file = (*files)[i].toString();
        if (pad.kind == PadKind::File && pad.file.isEmpty())
            pad.kind = PadKind::BuiltIn; // a file the bundle no longer holds: the pad falls back to its drum
    }
    table = read;
    return true;
}

} // namespace synth::drums
