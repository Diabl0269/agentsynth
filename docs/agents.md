# Agents & AI Integration

This project integrates AI capabilities for sound design assistance.

## Supported Providers
- **Ollama (local)**: talks to a local Ollama instance.
- **Remote (hosted)**: talks to the hosted inference backend. This is the default provider for a
  brand-new install; an install that has launched before but never touched AI settings keeps
  using Ollama.

Both are configurable via the Settings → AI tab.

## AI Features
- **Natural Language Patching**: Text-to-patch generation using an LLM.
- **Experimental Patching**: AI-assisted patching workflow managed by `AIIntegrationService`.

## Documentation & Standards
- See `docs/AI_Engine.md` for internal AI engine details.
- See `docs/AI_Usage_Guide.md` for user-facing AI instructions.
