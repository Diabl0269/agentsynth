# AI Sound Designer: Usage Guide

Practical instructions for using the AI Sound Designer in Agent Synth to create and modify patches,
and to arrange, with natural language.

## Getting started

1. **Open the AI chat panel.** It is reachable from a dedicated button in the app chrome.
2. **Choose hosted or local.** A new install uses **Hosted** mode: no setup required, but your
   prompt, current patch, and — if you have a timeline arrangement open — a compact summary of its
   tracks, clips and lanes are sent to Agent Synth's servers for processing. A notice next to the
   model picker says so whenever hosted mode is active, and Settings → AI carries the same
   disclosure on the toggle itself. Switch to **Ollama (local)** in Settings → AI to keep everything
   on this machine instead; that requires your own Ollama server running and reachable.
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
- **Review the JSON.** Expanding a patch card's JSON view shows exactly how the AI read your
  request.

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

## Timeline changes

The AI can arrange, not just patch. Ask it to add a track, place clips or draw automation — "add a
bass track and put a four-bar riff at the top", "automate the filter cutoff opening across the first
8 bars" — and it answers with a **Timeline Changes** card instead of, or alongside, the usual patch
card.

The card shows a plain-English summary of exactly what it would do (`Adds midi track "Bass"
(unbound - bind it in the timeline panel); places 1 clip (8 notes) at 0-4 on "Bass"`), and nothing
touches your arrangement until you press **Apply timeline changes**. The whole batch lands as a
single edit, so one Cmd+Z takes all of it back.

Two things it deliberately leaves to you: a new track arrives **unbound**, so you pick the module it
plays through in the timeline panel's track header; and it never imports or records audio, so it
only ever writes MIDI clips and automation, never audio clips.

If a suggestion cannot be applied — it names a track you do not have, or a value outside a
parameter's range — the card says so and offers no button, rather than failing silently.

It can also place a ready-made MIDI clip in one step by attaching a `.mid` file's notes to its
answer.

### The Patch / Arrange selector

A small **Patch / Arrange** selector sits next to the model picker, in hosted and local mode alike,
once a timeline is open. It decides — explicitly, with no keyword guessing — what your message asks
for:

- **Patch**, the default: patch creation and editing. With a local model, timeline suggestions can
  still ride along on a patch answer when the model volunteers them.
- **Arrange**: the answer is *only* a Timeline Changes card — tracks, clips, notes and automation.
  Along with your message the model receives a compact summary of your arrangement, your track list,
  and the list of automatable parameters. In hosted mode that goes to the arrangement service, the
  same information the hosted-mode privacy notice covers; in local mode it goes to your own Ollama
  model and nothing leaves your machine.

Everything downstream is identical either way: the card shows the validated summary, nothing is
applied until you press Apply, and a suggestion that fails validation shows the reason with no
button. With the timeline closed the selector is hidden, and hiding it resets it to Patch.

## Troubleshooting

- **"Error: No AI provider selected."** In local mode, make sure a model is selected in the
  dropdown. If no models appear, check that your Ollama server is running and reachable at
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
- **The patch does not sound as expected.** Expand the patch card to review the generated JSON,
  which shows how the AI interpreted the request, and refine the prompt accordingly.
- **A request takes longer than expected and times out.** The request timeout defaults to 4 minutes
  and is configurable in Settings → AI → Request Timeout, with presets of 2, 4, 6 and 10 minutes. A
  large local model on modest hardware can legitimately need more than the default.
