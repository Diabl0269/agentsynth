# Agent Synth AI Sound Designer: Usage Guide

This guide provides practical instructions and tips for effectively using the AI Sound Designer in Agent Synth to create and modify synthesizer patches with natural language.

## 1. Getting Started with the AI Sound Designer

1.  **Open the AI Chat Panel**: In the Agent Synth application, locate and open the AI chat panel. This is typically accessible via a dedicated button or menu option.
2.  **Choose Hosted or Local**: By default, a new install uses **Hosted** mode — no setup required, but your prompt, current patch, and (if you have a timeline arrangement open) a compact summary of its tracks/clips/lanes are sent to Agent Synth's servers for processing (a notice next to the model picker says so whenever hosted mode is active; see Settings → AI for the toggle and the same disclosure on hover). Switch to **Ollama (local)** in Settings → AI to keep everything on this machine instead; that requires your own Ollama server running and accessible.
3.  **Select an AI Model**: In local (Ollama) mode, use the model picker dropdown to choose an available AI model (e.g., `qwen3-coder-next:latest`). In hosted mode the picker shows "Model chosen automatically" — the service selects its own model server-side, so there's nothing to pick.
4.  **Start Chatting**: Type your requests or descriptions in the input field and press "Send" or Enter.

## 2. Prompting Best Practices

The AI responds best to clear, concise, and specific prompts. Think about the characteristics of the sound you want, or the changes you want to make to an existing patch.

### What to include in your prompts:

*   **Sound Characteristics**: Describe the timbre, mood, or texture.
    *   *Good:* "Create a bright, percussive synth bass."
    *   *Better:* "I need a deep, resonant bass sound with a quick attack and a long decay."
*   **Module Types**: You can specify modules you want to use or exclude.
    *   *Good:* "Use an oscillator and a filter to make a pluck sound."
    *   *Better:* "Generate a lead sound using a square wave oscillator, a resonant low-pass filter, and a short ADSR envelope."
*   **Parameters**: Mention specific parameter values or ranges.
    *   *Good:* "Set the filter cutoff high."
    *   *Better:* "Set the filter cutoff to around 80% and the resonance to 50%."
*   **Connections**: Describe the signal flow.
    *   *Good:* "Connect the LFO to the filter."
    *   *Better:* "Connect the LFO's output to the filter's cutoff input, with a moderate modulation amount."
*   **Actions**: Clearly state what you want the AI to do (create, modify, change, add, remove).
    *   "**Create** a spooky ambient pad."
    *   "**Change** the oscillator's waveform to a saw."
    *   "**Add** a delay effect to the current patch."

### Tips for Effective Prompts:

*   **Be Specific**: Vague prompts lead to vague results.
*   **Iterate**: If the first attempt isn't perfect, refine your request. You can say things like "Make it brighter," or "Reduce the sustain on the last sound."
*   **Use Agent Synth Terminology**: While natural language is understood, using module names (e.g., Oscillator, Filter, ADSR) and parameter names (e.g., Cutoff, Frequency) from Agent Synth can yield more precise results.
*   **Review JSON**: If the AI provides a JSON patch, expanding and reviewing it can help you understand how the AI interpreted your request.

## 3. Example Prompts

Here are some examples of effective prompts you can use:

*   "Create a classic subtractive synth bass with a square wave, low-pass filter, and a short, punchy ADSR."
*   "Generate a shimmering, ethereal pad sound. Use a sine wave oscillator, a long release ADSR, and a reverb effect."
*   "Modify the current patch: increase the filter cutoff slightly and add a slow LFO to modulate the oscillator's pitch."
*   "Add a delay module with medium feedback and a wet/dry mix of 50%."
*   "Design a gritty distortion effect chain for the input."
*   "Give me a sequence that plays C3, E3, G3, C4 in a loop."
*   "I want a percussive sound, similar to a wood block. Use a short decay."

## 4. Timeline Changes

The AI can also arrange, not just patch. Ask it to add a track, place clips, or draw automation
("add a bass track and put a four-bar riff at the top", "automate the filter cutoff opening across
the first 8 bars") and it answers with a **Timeline Changes** card instead of — or alongside — the
usual patch card. The card shows a plain-English summary of exactly what it would do ("Adds midi
track "Bass" (unbound - bind it in the timeline panel); places 1 clip (8 notes) at 0-4 on "Bass""),
and nothing touches your arrangement until you press **Apply timeline changes**. The whole batch
lands as a single edit, so one Cmd+Z takes all of it back. Two things it deliberately leaves to
you: a new track arrives **unbound** — pick the module it should play through in the timeline
panel's track header — and it never imports or records audio, so it only ever writes MIDI clips and
automation, never audio clips. If a suggestion can't be applied (it names a track you don't have,
or a value outside a parameter's range), the card says so and offers no button rather than failing
silently. It can also place a ready-made MIDI clip in one step by attaching a `.mid` file's notes to
its answer — the safest note data the AI can hand back, since a `.mid` blob can only ever carry notes.

### The Patch / Arrange selector

With the timeline feature switched on (Preferences → Show timeline), a small **Patch / Arrange**
selector appears next to the model picker — in Hosted and local (Ollama) mode alike. It decides —
explicitly, with no keyword guessing — what your message asks for:

- **Patch** (the default): patch creation and editing, exactly as before. With a local model,
  timeline suggestions can still ride along on a patch answer when the model volunteers them.
- **Arrange**: the answer is *only* a Timeline Changes card — tracks, clips, notes and
  automation. Along with your message the model receives a compact summary of your arrangement,
  your track list, and the list of automatable parameters. In Hosted mode this goes to the
  arrangement service (the same information the hosted-mode privacy notice covers); in local mode
  it goes to your own Ollama model and nothing leaves your machine, as always.

Everything downstream is identical to the flow above: the card shows the validated summary,
nothing is applied until you press **Apply timeline changes**, and a suggestion that fails
validation shows the reason with no button. With the timeline feature off, the selector is hidden
and requests route exactly as before.

## 5. Troubleshooting

*   **"Error: No AI provider selected."**: In local (Ollama) mode, ensure you have selected an AI model from the dropdown. If no models appear, check if your Ollama server is running and accessible at `http://localhost:11434`. (In hosted mode the picker shows "Model chosen automatically" instead — that's expected, not an error.)
*   **"Error fetching models"**: This only appears in local (Ollama) mode, and means Agent Synth couldn't connect to the Ollama server. Verify the server is running and there are no firewall issues. Check the application logs for "AI Discovery Error" messages.
*   **Nothing happens after sending a prompt, in hosted mode**: The hosted service may not be reachable yet in this environment. Switch to Ollama (local) in Settings → AI as a fallback, or check with your Agent Synth administrator.
*   **AI provides text, but no patch is applied**: The AI's response might not contain a valid JSON patch in the expected ````json` block format, or the JSON might be malformed. Try rephrasing your prompt to explicitly ask for a JSON patch (e.g., "Provide the patch as a JSON block").
*   **Unexpected Patch Behavior**: The AI might generate a patch that doesn't sound as expected. Review the generated JSON (by expanding the patch card) to understand the AI's interpretation and refine your prompt accordingly.

---
