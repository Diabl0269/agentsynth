# Testing Cloud-Gated Features Locally

Pro-gated features — cloud conversation history, `x-conversation-id` threading — only do anything
interesting against a real server from the private backend repo. The local `AIChatComponent` test
suite fakes the backend (`setHistorySourcesForTesting`), so exercising the real client/server
contract needs an actual server running. This is a development flow, not a test.

## Fast path

`scripts/run-local-cloud-dev.sh` does everything below in one command: it starts a disposable local
Postgres (Docker), migrates it, starts the private backend repo's API server against it (dev IdP,
real Ollama inference, auto-picking a sane model if `llama3.1` is not pulled), then launches this
repo's locally-built Debug app with the override already set.

```bash
scripts/run-local-cloud-dev.sh            # start everything + launch the app
scripts/run-local-cloud-dev.sh --down     # stop the API server and Postgres container
```

It requires a checkout of the private backend repo as a sibling directory by default (override with
`SYNTH_PLATFORM_DIR`) and a Debug `AgentSynth` build already present (`BUILD_DIR`, default `build` —
see [`testing.md`](testing.md#build-flags)). It prints the exact Settings and sign-in steps once the
app launches.

## The two hosts

Only one of them was ever user-configurable:

- **Chat / `patch.generate`** (`RemoteProvider`) — user-configurable via the Settings "Host" field
  for that provider.
- **Auth / entitlement / cloud history** (`AccountService`/`AuthClient`, the `GET`/`DELETE`
  `/v1/conversations*` endpoints) — hardcoded to production (`synth::branding::kApiBaseUrl`) with no
  UI to override it. `synth::branding::resolveApiBaseUrl()` (`Source/Branding.h`) reads an
  `AGENTSYNTH_LOCAL_API_URL` environment variable in **Debug builds only**, so this can be
  redirected without hand-editing `Branding.h` and rebuilding per URL change.
  `AGENTSYNTH_LOCAL_API_URL` is compiled out of Release builds entirely (`#ifndef NDEBUG`) — see the
  comment on `resolveApiBaseUrl()` for why.

## By hand

Run the private backend repo locally first; its in-memory stores are the default, so no Postgres is
needed for this flow. See that repo's own local-development doc for the full setup. The short
version:

```bash
pnpm --filter @platform/api dev   # serves http://localhost:8787
```

Then, to test a Pro-gated flow end to end:

1. Start the private backend repo locally as above.
2. In AgentSynth's Settings, set the chat provider's Host field to `http://localhost:8787`.
3. Launch the locally-built Debug app with the override set:

   ```bash
   AGENTSYNTH_LOCAL_API_URL=http://localhost:8787 "./build/AgentSynth_artefacts/Debug/Agent Synth.app/Contents/MacOS/Agent Synth"
   ```

Production conversation storage is the backend's conversation-history store — Neon-managed Postgres,
180-day retention. Local dev's in-memory store implements the identical `ConversationStore`
interface, so this flow exercises real client/server contract behaviour even without a real Postgres
in the loop.
