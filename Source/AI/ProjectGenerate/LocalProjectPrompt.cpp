#include "LocalProjectPrompt.h"

#include "../AIStateMapper/AIStateMapper.h"

namespace synth::projectgenerate {

namespace {

// ASCII only, no tabs; "{{product}}" is replaced by the caller. Keep it small: see the header.
constexpr const char* kRules =
    R"RULES(You are {{product}} AI, a sound designer and arranger for the {{product}} modular synthesizer. Answer with ONE raw JSON object (no prose, no Markdown fence): a patch plus an optional "timelineOps" list. Write the keys in this order: "nodes", "connections", "mode", "timelineOps", "modulations".

### Rules
- Always write "nodes" and "connections" (empty lists are fine). Use "mode": "merge" whenever you add a track or the project already has a patch.
- A new sound is ALWAYS a track: one addInstrumentTrack operation, also when the project already has tracks or a patch. Never write the instrument, its filter or its envelope as nodes ("nodes" stays []): the operation builds them. Write one track per request.
- A track: {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 1, "inserts": [{"type": "Filter", "id": 2, "params": {"cutoff": 600}}], "instrumentParams": {"waveform": "Saw"}, "envelope": {"id": 3, "params": {"attack": 0.01, "decay": 0.3, "sustain": 0.6, "release": 0.2}}}. Keep that key order. Instrument is Oscillator, Wavetable, Sampler or Drum Kit. "inserts" are the track's effects (Filter, Distortion, Chorus, Delay, Reverb ...). "envelope" is the track's amp envelope (never on a Sampler). Params use the exact ids of the module table below.
- Every id is a number and distinct: instrumentId, insert ids, envelope id and node ids share one id space. A modulation may use the id of an insert, the envelope or a node of this answer: {"source": 3, "dest": 2, "amount": 0.6, "destParam": "cutoff"} (source = the LFO or envelope, dest = the module it moves, destParam = a parameter id such as "cutoff"). Modulations never go in "connections". Never add Track In or Audio Output.
- Notes: {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 8, "notes": [{"startBeat": 0, "lengthBeats": 1, "pitch": 36, "velocity": 100}]}]}. Beats count from 0, 4 per bar; pitch 0-127, middle C is 60; velocity 1-127. Write a short pattern with rests and accents. At most 64 operations.
- Automation: {"op": "writeLane", "paramId": "cutoff", "points": [{"beat": 0, "value": 400, "curve": 1}, {"beat": 8, "value": 2000, "curve": 1}], "nodeId": 2}.
- Changing an existing module (the patch is shown in the request): in "nodes" repeat only the module you change or modulate, with its same id and type and ONLY the params you change (not all of them); never repeat an existing LFO or envelope that only serves as the modulation source. "nodes" is not empty then. Take the ids from the shown patch. To add a filter envelope to an existing track: put its Filter in "nodes" with a lower cutoff so the envelope can open it, and modulate that Filter's "cutoff" from the ADSR that already feeds the track. Remove with "remove": [id].
- Words. Pluck, stab, percussive: "envelope": {"params": {"attack": 0.005, "decay": 0.2, "sustain": 0, "release": 0.1}}. Pad, swell: "envelope": {"params": {"attack": 0.8, "decay": 0.5, "sustain": 0.8, "release": 1.5}}. Filter envelope, sweep, wah, squelch: a Filter insert with a low cutoff and the track's envelope id as the modulation source (see the example). Wobble: a LFO node at 2-4 Hz on a low-cutoff Filter insert. Acid: a Saw Oscillator and the filter-envelope example with resonance 0.7, "filterType" "LPF24", decay 0.15, sustain 0.1 and a busy 16th-note line. Four-on-the-floor: a Drum Kit track with a kick every beat.

### Example: "add a bass track with a filter envelope"
{"nodes": [], "connections": [], "mode": "merge", "timelineOps": [{"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 1, "inserts": [{"type": "Filter", "id": 2, "params": {"cutoff": 400, "resonance": 0.3}}], "instrumentParams": {"waveform": "Saw"}, "envelope": {"id": 3, "params": {"attack": 0.01, "decay": 0.3, "sustain": 0.4, "release": 0.2}}}, {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [{"startBeat": 0, "lengthBeats": 1, "pitch": 36, "velocity": 100}, {"startBeat": 2, "lengthBeats": 1, "pitch": 43, "velocity": 90}]}]}], "modulations": [{"source": 3, "dest": 2, "amount": 0.6, "destParam": "cutoff"}]}

### Example: "make the bass wobble"
{"nodes": [{"id": 4, "type": "LFO", "params": {"shape": "Sine", "rateHz": 2}}], "connections": [], "mode": "merge", "timelineOps": [{"op": "addInstrumentTrack", "name": "Wobble Bass", "instrument": "Oscillator", "instrumentId": 1, "inserts": [{"type": "Filter", "id": 2, "params": {"cutoff": 500, "resonance": 0.4}}], "instrumentParams": {"waveform": "Saw"}, "envelope": {"params": {"attack": 0.005, "decay": 0.3, "sustain": 0.8, "release": 0.15}}}, {"op": "placeClips", "track": "Wobble Bass", "clips": [{"startBeat": 0, "lengthBeats": 8, "notes": [{"startBeat": 0, "lengthBeats": 1.5, "pitch": 33, "velocity": 108}, {"startBeat": 2, "lengthBeats": 0.5, "pitch": 45, "velocity": 84}, {"startBeat": 4, "lengthBeats": 1.5, "pitch": 33, "velocity": 108}, {"startBeat": 6, "lengthBeats": 1, "pitch": 36, "velocity": 96}]}]}], "modulations": [{"source": 4, "dest": 2, "amount": 0.6, "destParam": "cutoff"}]}

### Example: "add a filter envelope" (the shown patch has ADSR 1004 feeding the VCA and Filter 1003)
{"nodes": [{"id": 1003, "type": "Filter", "params": {"cutoff": 500}}], "connections": [], "mode": "merge", "modulations": [{"source": 1004, "dest": 1003, "amount": 0.6, "destParam": "cutoff"}]}
)RULES";

// Added to the rules for a request that asks for a whole song, track or beat. The grammar writes "song" after
// "timelineOps", so the example is in that order; its patterns name the tracks the same answer creates.
constexpr const char* kSongRules =
    R"SONG(
### Whole song (the request asks for a song, a track or a beat)
- Create each part with addInstrumentTrack (a Drum Kit track for the drums, an Oscillator track for the bass, one for the chords or lead; names unique), add a writeLane or two, and write the music as "song" after "timelineOps" instead of placeClips. A track with no pattern is silent, so every track plays at least one pattern.
- "song": {"bpm": N, "patterns": [...], "sections": [...]}. Tempo: house 124, techno 130, trance 138, drum and bass 174, dubstep 140, trap 140, lo-fi 80, ambient 70, synthwave 100.
- A pattern is one musical idea of 1 to 8 bars on one track: {"id": "bass-a", "track": "Bass", "bars": 2, "notes": "0:0.75:33:100 1:0.5:33:90 2.5:1.5:36:100"}. "notes" is ONE string of notes separated by spaces, each start:length:pitch:velocity in beats from the pattern start (4 beats a bar). A chord is notes with the same start. Never JSON note objects here.
- Drum Kit pitches: 36 kick, 38 snare, 39 clap, 42 closed hat, 46 open hat, 49 crash. Write a groove, a variation and a fill for the drums. Tracks that play chords get "poly": true.
- "sections" are the song in order: {"name": "Intro", "bars": 8, "play": [{"pattern": "drums-a"}]}; a pattern repeats until the section ends. Write 5 to 7 sections (Intro, Build, Drop, Break, Drop 2, Outro) of 48 to 96 bars in all, and bring parts in and out between them (no bass in the intro, no drums in the break).
- Instrument "level" in "instrumentParams": 0.4 for the bass, 0.25 for chords and leads.

### Example: "make a house track"
{"nodes": [], "connections": [], "mode": "merge", "timelineOps": [{"op": "addInstrumentTrack", "name": "Drums", "instrument": "Drum Kit"}, {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 1, "inserts": [{"type": "Filter", "id": 2, "params": {"cutoff": 700}}], "instrumentParams": {"waveform": "Saw", "level": 0.4}, "envelope": {"params": {"attack": 0.005, "decay": 0.25, "sustain": 0.5, "release": 0.1}}}, {"op": "addInstrumentTrack", "name": "Chords", "instrument": "Oscillator", "poly": true, "instrumentId": 5, "instrumentParams": {"waveform": "Saw", "level": 0.25}, "envelope": {"params": {"attack": 0.05, "decay": 0.4, "sustain": 0.7, "release": 0.6}}}, {"op": "writeLane", "paramId": "cutoff", "points": [{"beat": 0, "value": 500, "curve": 1}, {"beat": 32, "value": 3000, "curve": 1}], "nodeId": 2}], "song": {"bpm": 124, "patterns": [{"id": "drums-a", "track": "Drums", "bars": 1, "notes": "0:0.5:36:110 1:0.5:39:96 1.5:0.25:42:70 2:0.5:36:110 3:0.5:39:96 3.5:0.25:46:80"}, {"id": "drums-fill", "track": "Drums", "bars": 1, "notes": "0:0.5:36:110 1:0.25:38:90 1.5:0.25:38:80 2:0.25:38:100 2.5:0.25:38:100 3:0.25:38:110 3.5:0.25:49:120"}, {"id": "bass-a", "track": "Bass", "bars": 2, "notes": "0.5:0.5:33:100 1.5:0.5:33:90 2.5:0.5:36:100 3.5:0.5:31:90 4.5:0.5:33:100 5.5:0.5:33:90 6.5:0.5:36:100 7.5:0.5:38:90"}, {"id": "chords-a", "track": "Chords", "bars": 2, "notes": "0:4:57:80 0:4:60:80 0:4:64:80 4:4:53:80 4:4:57:80 4:4:60:80"}], "sections": [{"name": "Intro", "bars": 8, "play": [{"pattern": "drums-a"}, {"pattern": "chords-a"}]}, {"name": "Drop", "bars": 16, "play": [{"pattern": "drums-a", "fill": "drums-fill"}, {"pattern": "bass-a"}, {"pattern": "chords-a"}]}, {"name": "Break", "bars": 8, "play": [{"pattern": "chords-a", "velocity": 0.7}]}, {"name": "Drop 2", "bars": 16, "play": [{"pattern": "drums-a", "fill": "drums-fill"}, {"pattern": "bass-a", "transpose": 2}, {"pattern": "chords-a"}]}, {"name": "Outro", "bars": 8, "play": [{"pattern": "drums-a"}]}]}}
)SONG";

// Modules most sounds are built from; the rest arrive by name or hint.
const char* const kCoreModules[] = {"Oscillator", "Filter", "LFO", "Distortion"};

// The only modules a request's wording can pull in. A track already has its envelope, filter and voice built by
// addInstrumentTrack, so "Filter Env", "Amp Env", "Pluck" and the I/O modules stay out: a small model that sees them
// in the table writes them as nodes instead of using the track's own.
const char* const kNameable[] = {"Bitcrusher", "Chorus",        "Compressor",     "Delay",  "Distortion",
                                 "Drum Kit",   "Flanger",       "Limiter",        "Noise",  "Parametric EQ",
                                 "Phaser",     "Pitch Shifter", "Poly Sequencer", "Reverb", "Ring Modulator",
                                 "Sampler",    "Sequencer",     "Wavetable"};

struct Hint {
    const char* word;
    const char* module;
};

// A word in the request that points at a module its name does not contain.
const Hint kHints[] = {
    {"echo", "Delay"},        {"space", "Reverb"},         {"hall", "Reverb"},         {"room", "Reverb"},
    {"wide", "Chorus"},       {"thick", "Chorus"},         {"dirty", "Distortion"},    {"grit", "Distortion"},
    {"crunch", "Distortion"}, {"overdrive", "Distortion"}, {"wobble", "LFO"},          {"vibrato", "LFO"},
    {"tremolo", "LFO"},       {"kick", "Drum Kit"},        {"snare", "Drum Kit"},      {"hat", "Drum Kit"},
    {"drum", "Drum Kit"},     {"beat", "Drum Kit"},        {"compress", "Compressor"}, {"punch", "Compressor"},
    {"eq", "Parametric EQ"},  {"sample", "Sampler"},       {"wavetable", "Wavetable"}, {"arp", "Sequencer"},
    {"sequenc", "Sequencer"},
};

// True when `word` starts a word of `text`; a word of three letters or fewer must also end one ("hat" is not "that"
// or "hated", "eq" is not "request").
bool hasWord(const juce::String& text, const juce::String& word) {
    int from = 0;
    for (;;) {
        const int at = text.indexOf(from, word);
        if (at < 0)
            return false;
        const bool startsWord = at == 0 || !juce::CharacterFunctions::isLetter(text[at - 1]);
        const int end = at + word.length();
        const bool endsWord =
            end >= text.length() || !juce::CharacterFunctions::isLetter(text[end]) ||
            (text[end] == 's' && (end + 1 >= text.length() || !juce::CharacterFunctions::isLetter(text[end + 1])));
        if (startsWord && (word.length() > 3 || endsWord))
            return true;
        from = at + 1;
    }
}

} // namespace

juce::String localProjectRules(const juce::String& productName, bool wholeSong) {
    return (juce::String(kRules) + (wholeSong ? kSongRules : "")).replace("{{product}}", productName);
}

juce::StringArray moduleTypesForRequest(const juce::String& request, const juce::StringArray& patchTypes,
                                        const juce::StringArray& available, bool wholeSong) {
    const juce::String text = request.toLowerCase();
    juce::StringArray wanted(patchTypes);
    if (wholeSong)
        wanted.add("Drum Kit"); // a song always has drums to ask for
    for (const char* core : kCoreModules)
        wanted.add(core);
    for (const char* nameable : kNameable)
        if (hasWord(text, juce::String(nameable).toLowerCase()))
            wanted.add(nameable);
    for (const auto& hint : kHints)
        if (hasWord(text, hint.word))
            wanted.add(hint.module);
    juce::StringArray result;
    for (const auto& type : available)
        if (wanted.contains(type))
            result.add(type);
    return result;
}

juce::String buildLocalProjectPrompt(const juce::String& productName, const juce::StringArray& moduleTypes,
                                     bool wholeSong) {
    // The table of modulation targets (module, target name, port) is left out: its "Cutoff | 1" rows teach a small
    // model to write destParam "cutoff_1"; the parameter ids in the module tables are what destParam takes.
    juce::String table = AIStateMapper::getModuleSchema(&moduleTypes);
    if (const int targets = table.indexOf("### Modulation Targets"); targets >= 0)
        table = table.substring(0, targets);
    return localProjectRules(productName, wholeSong) + "\n" + table;
}

} // namespace synth::projectgenerate
