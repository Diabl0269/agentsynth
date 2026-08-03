# Agent Synth AI Sound Designer: Usage Guide

This guide provides practical instructions and tips for effectively using the AI Sound Designer in Agent Synth to create and modify synthesizer patches with natural language.

## 1. Getting Started with the AI Sound Designer

1.  **Open the AI Chat Panel**: In the Agent Synth application, locate and open the AI chat panel. This is typically accessible via a dedicated button or menu option.
2.  **Select an AI Model**: Use the model picker dropdown to choose an available AI model (e.g., `qwen3-coder-next:latest`). Ensure your Ollama server (or other AI backend) is running and accessible if using local models.
3.  **Start Chatting**: Type your requests or descriptions in the input field and press "Send" or Enter.

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

## 4. Troubleshooting

*   **"Error: No AI provider selected."**: Ensure you have selected an AI model from the dropdown. If no models appear, check if your Ollama server is running and accessible at `http://localhost:11434`.
*   **"Error fetching models"**: Your Agent Synth application might not be able to connect to the Ollama server. Verify the server is running and there are no firewall issues. Check the application logs for "AI Discovery Error" messages.
*   **AI provides text, but no patch is applied**: The AI's response might not contain a valid JSON patch in the expected ````json` block format, or the JSON might be malformed. Try rephrasing your prompt to explicitly ask for a JSON patch (e.g., "Provide the patch as a JSON block").
*   **Unexpected Patch Behavior**: The AI might generate a patch that doesn't sound as expected. Review the generated JSON (by expanding the patch card) to understand the AI's interpretation and refine your prompt accordingly.

---
