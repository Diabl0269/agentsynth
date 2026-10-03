# AI Sound Designer: Usage Guide

Practical instructions for using the AI Sound Designer in Agent Synth to create and modify patches,
and to arrange, with natural language, from one chat input.

## Getting started

1. **Open the AI chat panel.** It is reachable from a dedicated button in the app chrome.
2. **Choose hosted or local.** A new install uses **Hosted** mode: no setup required, but your
   prompt, current patch, and — if you have a timeline arrangement open — a compact summary of its
   tracks, clips and lanes are sent to Agent Synth's servers for processing. A notice next to the
   model picker says so whenever hosted mode is active, and Settings → AI carries the same
   disclosure on the toggle itself. Switch to **Ollama (local)** in Settings → AI to keep everything
   on this machine instead; that requires your own Ollama server (version 0.34.4 or newer)
   running and reachable.
3. **Select a model.** In local mode, use the model picker to choose an available model. In hosted
   mode the picker shows "Model chosen automatically" — the service selects its own model
   server-side, so there is nothing to pick.
4. **Start chatting.** Type a request in the input field and press Send or Enter.

## Prompting

The AI responds best to clear, concise, specific prompts. Describe the sound you want, or the change
you want made to the existing patch.

What to include:

- **Sound characteristics** — timbre, mood, texture. "A deep, resonant bass with a quick attack and
  a long decay" beats "a bass sound".
- **Module types** — name the modules you want used or avoided: "a square wave oscillator, a
  resonant low-pass filter and a short ADSR".
- **Parameters** — "set the filter cutoff to around 80% and the resonance to 50%".
- **Connections** — "connect the LFO's output to the filter's cutoff input, with a moderate
  modulation amount".
- **The action** — create, modify, change, add, remove.

Tips:

- **Be specific.** Vague prompts give vague results.
- **Iterate.** Refine with follow-ups like "make it brighter" or "reduce the sustain".
- **Use Agent Synth's own terminology.** Module names (Oscillator, Filter, ADSR) and parameter names
  (Cutoff, Frequency) yield more precise results than paraphrases.
- **Review the details.** **Show details** on the card lists every change and the JSON, which shows
  exactly how the AI read your request.

Example prompts:

- "Create a classic subtractive synth bass with a square wave, low-pass filter, and a short, punchy
  ADSR."
- "Generate a shimmering, ethereal pad sound. Use a sine wave oscillator, a long release ADSR, and a
  reverb effect."
- "Modify the current patch: increase the filter cutoff slightly and add a slow LFO to modulate the
  oscillator's pitch."
- "Add a delay module with medium feedback and a wet/dry mix of 50%."
- "Design a gritty distortion effect chain for the input."
- "Give me a sequence that plays C3, E3, G3, C4 in a loop."
- "I want a percussive sound, similar to a wood block. Use a short decay."

## One message, one card

There is one input and no mode to pick. Ask for anything that changes the project in one message:
a sound, modules, tracks, notes, automation, modulation, or several at once. For example:

- "Add a bass track playing a four-bar riff, with a filter whose cutoff opens over the first 8 bars."
- "Add an LFO that wobbles the bass filter, and a pad track holding a C minor chord for 8 bars."
- "Make the lead brighter and automate its delay mix up in the last 4 bars."

The answer is **one card** with **one Apply** button. The card lists what it would do, one line per
part: the patch change first ("Merges a patch that adds 1 module, adds 1 modulation"), then the
timeline change ("Adds instrument track "Bass" ...; writes 2 points to Filter
cutoff"). Nothing touches the project until you press **Apply**, and the whole answer lands as a
single edit, so one Cmd+Z takes all of it back: the tracks, the modules, the notes and the lanes
together.

A new instrument track arrives with its instrument already built and wired, and the answer may
modulate or automate the modules it just created. It never imports or records audio, so it only
writes MIDI clips and automation, never audio clips. It can also place a ready-made MIDI clip in one
step by attaching a `.mid` file's notes to its answer.

If the answer cannot be applied (it names a track you do not have, a value outside a parameter's
range, or asks to replace the patch while adding a track), the card says why and offers no button,
rather than failing silently. **Show details** on the card lists every module change and the JSON
behind the answer; the thumbs let you rate it.

A question that asks for no change ("how does FM differ from subtractive?") gets a plain text
answer with a local model. In hosted mode every message is answered with a card, since the hosted
service only edits. A hosted answer that builds tracks can take 20 to 30 seconds; the thinking line
under the conversation counts the wait.

## Troubleshooting

- **"Error: No AI provider selected."** In local mode, make sure a model is selected in the
  dropdown. If no models appear, check that your Ollama server (0.34.4 or newer) is running and reachable at
  `http://localhost:11434`. In hosted mode the picker shows "Model chosen automatically" instead —
  that is expected, not an error.
- **"Error fetching models"** appears only in local mode and means Agent Synth could not connect to
  the Ollama server. Verify the server is running and that no firewall is in the way. Check the
  application logs for "AI Discovery Error" messages.
- **Nothing happens after sending a prompt, in hosted mode.** The hosted service may not be
  reachable from this environment. Switch to Ollama (local) in Settings → AI as a fallback.
- **The AI replies with text but no patch is applied.** The response may not contain a valid JSON
  patch in the expected block format, or the JSON may be malformed. Rephrase the prompt to ask
  explicitly for a JSON patch.
- **The patch does not sound as expected.** Open the card's details to review the generated JSON,
  which shows how the AI interpreted the request, and refine the prompt accordingly.
- **A request takes longer than expected and times out.** The request timeout defaults to 4 minutes
  and is configurable in Settings → AI → Request Timeout, with presets of 2, 4, 6 and 10 minutes. A
  large local model on modest hardware can legitimately need more than the default.
