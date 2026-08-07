# P2·17 — Production-viable inference provider: review & recommendation

**Date:** 2026-08-07
**Scope:** review the P2·17 finding as written, survey alternatives, recommend one.
**Sources for the problem statement:** `roadmap.html` (AI Engine Extraction — Roadmap), tasks P2·10 / P2·12 / P2·15 / P2·16 / P6·5, §03 Unit economics; `Diabl0269/synth-platform` `packages/inference/src/index.ts`.

---

## 1. The finding is real, but understated

P2·17 frames this as *"the on-demand tier can't sustain real traffic."* The measured numbers say something harder.

From §03 Unit economics, one `patch.generate` request is:

| Component | Tokens |
|---|---|
| System prompt (schema + rules + few-shot, post-P2·9) | 7,400 |
| Current patch JSON | 1,500 |
| Trimmed recent history | 750 |
| **Input subtotal** | **9,650** |
| Generated patch (output) | 1,000 |
| **Total per message** | **10,650** |

Groq's published limits for `openai/gpt-oss-20b` are **30 RPM / 1K RPD / 8K TPM / 200K TPD**, organization-wide.

Three consequences, in increasing order of severity:

1. **A single request exceeds the entire per-minute budget.** 10,650 > 8,000 TPM. This is not a throughput ceiling you grow into — the provider cannot cleanly serve request number one. That matches the observed `Requested ~8500–8700 / Limit 8000` and the fact that it reproduces back-to-back.
2. **The daily cap is the harder wall, and P2·17 doesn't mention it.** 200,000 TPD ÷ 10,650 ≈ **18.8 generations per day, org-wide**. The Pro plan sells 500 messages/month ≈ 16.4/day. **One paying subscriber consumes the entire organization's daily allowance.** The free tier (1,000 users × 30 msgs/mo ≈ 10.6M tokens/day) needs ~53× the daily cap.
3. **The Developer plan does not fix this.** Groq's rate-limit docs list `openai/gpt-oss-20b` on the **Developer Plan** at the same 30 RPM / 1K RPD / 8K TPM / 200K TPD. The widely-repeated "Dev tier = ~10× limits" is true for older Llama models (`llama-3.1-8b-instant` goes 14.4K → 500K RPD) but the gpt-oss family is not in that uplift.

> ### The decision-changing fact
> **Chasing the Groq upgrade is chasing a fix that isn't there.** Whether the self-serve Dev Tier flow is broken is a side issue — the published paid tier for this model doesn't clear the bar either. P2·17's first suggested action ("check Groq support directly for a rate-limit increase") should be demoted from plan-A to a 2-minute sanity check.
>
> **Verify before acting on this:** open the rate-limits page in the console while logged in and read the Free vs Developer tabs for `openai/gpt-oss-20b`. The public docs table could be stale, and this single number decides whether Groq stays on the shortlist at all.

### What P2·17 gets right

- The fix slots into the existing seam. `packages/inference/src/index.ts` is already a clean `switch (INFERENCE_PROVIDER)` returning an AI-SDK `LanguageModel`; adding a case is ~30 lines. No architecture change needed.
- Evaluating in parallel with the rest of Phase 2 is correct — nothing downstream blocks on this.

### What it's missing

- The TPD wall (above) — it's worse than the TPM wall and changes the urgency.
- **Prompt caching as a cost axis.** 7,400 of 9,650 input tokens (77%) are a byte-identical prefix on every request. Groq offers no prompt caching. Gemini gives 90% off cached tokens automatically; Fireworks 50%. P2·3's stated rationale for moving the prompt server-side was *literally* "you cannot use vendor prompt caching on it" — that payoff has never been collected, because the vendor chosen doesn't offer it.
- **P6·5 (fallback inference provider) should be pulled forward into this task.** You're opening the seam anyway; a second `case` is ~15 more lines.

---

## 2. Requirements this decision has to satisfy

Derived from the roadmap, in priority order:

| # | Requirement | Source |
|---|---|---|
| R1 | Headroom ≫ 10,650 tokens/request; realistically ≥100K TPM and ≥20M TPD to survive a 100-subscriber launch | §03 + P2·17 |
| R2 | Schema-constrained decoding through AI SDK `generateObject` | P2·4, P2·10, P2·12 |
| R3 | Reachable through `packages/inference`'s `LanguageModel` seam without new architecture | P2·17 |
| R4 | COGS/message ≪ $0.0165 (the break-even that would put Pro at 0% margin) | §03 |
| R5 | Minimal new vendor surface — new subprocessor must be disclosed in P4·5, new secret must be managed in `apps/infra` | P4·5, P2·15 |
| R6 | Doesn't invalidate the P2·8/P2·11/P2·12 eval baseline (i.e. same model family if possible) | P2·8 rationale |

R6 is the one that quietly matters most. The eval harness exists specifically so model swaps aren't guesses. A provider change that is *also* a model-family change is a re-baseline, not a swap.

---

## 3. Options surveyed

Cost/message computed at 9,650 input + 1,000 output. Margin is on Pro ($9/mo, 500 msgs, net $8.24 after Polar's ~4% + 40¢).

| Option | Model | $/M in → out | $/msg | Pro COGS/mo | Margin | Ceiling | New vendor? |
|---|---|---|---|---|---|---|---|
| **Groq (today)** | gpt-oss-20b | 0.075 → 0.30 | $0.00102 | $0.51 | 94% | **8K TPM / 200K TPD** ❌ | — |
| **★ Vertex AI open models** | gpt-oss-20b-maas | 0.075 → 0.30 | **$0.00102** | **$0.51** | **94%** | **1,200 QPM** | **No** |
| Vertex AI open models | gpt-oss-120b-maas | 0.15 → 0.60 | $0.00205 | $1.03 | 88% | 650 QPM | No |
| **Cerebras Developer** | gpt-oss-120b | 0.35 → 0.75 | $0.00413 | $2.07 | 75% | **1K RPM / 1M TPM**, no daily cap | Yes |
| Together AI | gpt-oss-20b | 0.05 → 0.20 | $0.00068 | $0.34 | 96% | Dynamic, unpublished | Yes |
| Fireworks AI | gpt-oss-20b | 0.05 → 0.20 | $0.00068 | $0.34 | 96% | 6,000 RPM; spend-gated tiers | Yes |
| Vertex/Gemini | 2.5 Flash-Lite | 0.10 → 0.40 | $0.00137 | $0.69 | 92% | DSQ (no preset limit) | No — *retires 16 Oct 2026* |
| Vertex/Gemini | 3.1 Flash-Lite | 0.25 → 1.50 | $0.00391 | $1.96 | 76% | DSQ (no preset limit) | No |
| OpenAI | GPT-5 nano | 0.05 → 0.40 | $0.00088 | $0.44 | 95% | Tiered, scales with spend | Yes |
| OpenRouter | any | passthrough + 5.5% | +5.5% | — | — | inherits upstream | Yes (+ extra hop) |
| Self-host GPU | gpt-oss-20b | ~$0.70/hr L4 | — | **~$500/mo** | negative | yours | No, but ops |

### Notes per option

**Vertex AI open models (`openai/gpt-oss-20b-maas`)** — GA since Aug 2025, `us-central1`, OpenAI-compatible `chat/completions` at
`https://us-central1-aiplatform.googleapis.com/v1/projects/${PROJECT}/locations/us-central1/endpoints/openapi/chat/completions`.
Pricing is *identical to Groq's* ($0.075/$0.30 for 20b, $0.15/$0.60 for 120b). Rate limits are 1,200 QPM (20b) / 650 QPM (120b), with quota-increase requests available. Structured output is a documented MaaS capability. Auth is a Google OAuth bearer token — i.e. IAM, not an API key.

**Cerebras** — free tier is unusable (5 RPM / 30K TPM **and an 8,192-token context cap**, below your 9,650-token prompt). The **Developer tier is self-serve for $10 in credits** and jumps to 1K RPM / 1M TPM with hourly/daily restrictions removed. Only serves gpt-oss-**120b**, not 20b. Fastest provider measured on this model (~1,869 t/s).

**Together AI** — cheapest, but publishes **no** rate limits by design: "dynamic rate limits adjust with usage, no fixed per-model limits published," and they explicitly warn that *"sudden spikes far above your recent usage may be throttled."* That is precisely the launch-day failure mode. Fixed capacity requires a dedicated endpoint (i.e. a monthly commit).

**Fireworks AI** — same price as Together, with a knowable 6,000 RPM ceiling and spend-gated tiers ($50/mo with a card → $500 → $5K → $50K). JSON-schema constrained decoding is first-class, and notably **does not** require every property in `required` (it applies `unevaluatedProperties: false` instead) — the opposite of the Groq behaviour that caused P2·16.

**Gemini Flash-Lite on Vertex** — uses Dynamic Shared Quota: no preset rate limit at all on pay-as-you-go. Plus implicit context caching (automatic, 90% off cached tokens on Gemini 2.5+, 2,048-token minimum, no storage cost) which would apply to your entire 7,400-token prefix. With cache hits, 3.1 Flash-Lite lands near $0.0024/msg. **But**: different model family ⇒ full eval re-baseline (violates R6), and 2.5 Flash-Lite (the cheap one) dies 16 Oct 2026.

**Self-hosting** — decisively no. An always-on L4 is ~$0.70/hr ≈ $500/mo; break-even against Vertex's $0.001/msg is ~500,000 messages/month. Cloud Run GPU's scale-to-zero doesn't rescue it either: loading 20B weights on a cold start is tens of seconds, and setting `min-instances=1` to avoid that re-creates the $500/mo floor. Revisit only if inference spend passes ~$500/mo — which §03 says won't happen this side of thousands of subscribers.

**OpenRouter** — inverts your design. `packages/inference` *is* the adapter-and-failover layer; OpenRouter adds 5.5%, a network hop, and a third party to name as a subprocessor in P4·5, to provide a seam you already own.

---

## 4. Recommendation

### Primary: Vertex AI Open Model API — `openai/gpt-oss-20b-maas`, `us-central1`

**Rationale, strongest first:**

1. **It is the same model at the same price.** Byte-identical weights to what Groq serves, $0.075/$0.30 either way. Every P2·8/P2·11/P2·12 measurement transfers. The frozen v1 prompt transfers. §03's unit-economics table needs **zero** edits — $0.001/msg, 94% margin, unchanged. This is the only option on the list that costs you nothing in re-measurement (R6 ✅, R4 ✅).
2. **The ceiling problem disappears.** 1,200 QPM against a workload that currently cannot complete one request per minute. Quota increases are a documented request path if you ever get near it (R1 ✅).
3. **Zero new vendor.** Same GCP project (`agentsynth`), same region as your Cloud Run service, same billing account and the 3-ILS budget tripwire from P2·14, same Cloud Logging / Error Reporting you just wired in P2·6. Nothing new to disclose in P4·5 — Google is already your subprocessor via Cloud Run (R5 ✅).
4. **It deletes a secret rather than adding one.** Auth becomes the Cloud Run runtime service account holding `roles/aiplatform.user`. The Secret Manager `GROQ_API_KEY` resource in `apps/infra` goes away, and with it the rotation burden and the reason the service is locked down so tightly today. Strictly better security posture, less Pulumi state.
5. **Smallest possible diff.** `@ai-sdk/openai-compatible` is already a dependency (it's your Ollama path), and you already have the `fetch`-override pattern in that file for the `rewriteEmptyAdditionalProperties` fix. Adding a `vertex` case that mints a Google auth token in the same override is ~30 lines plus a test (R3 ✅).
6. **The upgrade lever stays inside one env var.** `openai/gpt-oss-120b-maas` is a one-line change at $0.002/msg (88% margin) if 20b's pass rate disappoints — and it's the exact 20b-vs-120b comparison P2·12 wanted to run, now runnable without a second vendor. If gpt-oss plateaus entirely, Gemini 3.1 Flash-Lite is the *same provider, same auth, same SDK*.

**What has to be verified before this closes** (half a day, in this order):

- **Structured output.** Does Vertex's OpenAI-compatible `response_format.json_schema` behave like Groq's strict mode (every property in `required`) or like Fireworks' looser mode? P2·16 already converted the `ModulationSchema` optional fields, so the schema should satisfy the stricter reading either way — but *verify with one live `patch.generate`*, exactly as P2·16 demanded. This is the single highest-risk unknown.
- **TPM, not just QPM.** Google publishes QPM for the open models but not TPM. Check `Vertex AI API` quotas in the console for the project, then run a short burst test at realistic prompt size.
- **Latency.** Groq is ~1,000 t/s; Vertex MaaS will not match that. Measure p50/p95 for a 1,000-token patch. Anything under ~15s is fine against a spinner (and against the 180s local baseline), but you want the number before P4·6 flips the default to hosted.
- **Region.** OpenAI open models are `us-central1` only. That matches your Cloud Run region — but there is no in-Vertex regional failover for this model, which is the argument for the secondary below.

### Secondary: Cerebras Developer tier as the failover — i.e. land P6·5 now

$10 of self-serve credits buys 1K RPM / 1M TPM with no daily cap, on `gpt-oss-120b` — same family, so the prompt transfers without a re-baseline. `@ai-sdk/cerebras` exists. It's the priciest per message on the shortlist ($0.004), which is irrelevant for a path that only carries traffic during a Vertex incident.

Two reasons to do it in this task rather than deferring to P6·5:
- The seam is already open and the second `case` is ~15 lines.
- It hedges the one Vertex risk you can't design around: single-region, single-model availability.

It also gives you a fast lane. Cerebras is the highest-throughput provider measured on gpt-oss-120b — if Vertex latency comes back disappointing, promoting Cerebras to primary is an env-var change, not a project.

### Explicitly not recommended

| Option | Why not |
|---|---|
| **Wait for Groq Dev Tier** | Published Developer limits for `openai/gpt-oss-20b` are identical to Free. Even if the upgrade flow were fixed today, 200K TPD ≈ 19 generations/day org-wide. Not a rate-limit-increase problem. |
| **Together AI** | Cheapest per token, but unpublished dynamic limits that explicitly throttle spikes. Trading a known ceiling for an unknown one. |
| **Fireworks AI** | Genuinely good (cheap, 6K RPM, sane schema handling). Loses to Vertex only on R5/R6 — new vendor, new key, new subprocessor, for a ~$0.17/subscriber/month saving on a line item that is already 6% of revenue. Keep as the fallback-to-the-fallback. |
| **Gemini Flash-Lite** | Best long-term quality story and the only option that collects P2·3's prompt-caching payoff — but it's a model-family change, so it costs a full eval re-baseline. Do it deliberately as a quality decision after the harness re-runs, not as an incident fix. |
| **OpenRouter** | 5.5% + a hop + a subprocessor, to buy a failover layer `packages/inference` already is. |
| **Self-hosted GPU** | ~$500/mo floor vs $0.51/mo of tokens. Break-even ~500K msgs/month. |

---

## 5. Concrete change

`packages/inference/src/index.ts` — add one case, following the existing Ollama pattern:

```ts
// New dep: google-auth-library (Apache-2.0). @ai-sdk/openai-compatible already present.
const DEFAULT_VERTEX_MODEL_ID = "openai/gpt-oss-20b-maas";
const VERTEX_LOCATION = "us-central1";   // OpenAI open models are us-central1 only

function vertexModel(): LanguageModel {
  const project = process.env.GOOGLE_CLOUD_PROJECT;
  if (!project) throw new Error("GOOGLE_CLOUD_PROJECT is not set");

  const auth = new GoogleAuth({ scopes: "https://www.googleapis.com/auth/cloud-platform" });

  const vertex = createOpenAICompatible({
    name: "vertex",
    baseURL:
      `https://${VERTEX_LOCATION}-aiplatform.googleapis.com/v1/projects/${project}` +
      `/locations/${VERTEX_LOCATION}/endpoints/openapi`,
    supportsStructuredOutputs: true,
    // ADC on Cloud Run resolves to the runtime service account; tokens are ~1h, so mint per
    // request rather than at construction (the library caches internally).
    fetch: async (url, init) => {
      const token = await (await auth.getClient()).getAccessToken();
      return fetch(url, {
        ...init,
        headers: { ...init?.headers, Authorization: `Bearer ${token.token}` },
      });
    },
  });
  return vertex(process.env.INFERENCE_MODEL_ID ?? DEFAULT_VERTEX_MODEL_ID);
}
```

Plus, in `apps/infra/index.ts`:
- grant the Cloud Run runtime SA `roles/aiplatform.user`
- enable `aiplatform.googleapis.com`
- set `INFERENCE_PROVIDER=vertex` on the service
- **remove** the `GROQ_API_KEY` Secret Manager secret and its accessor binding

### Tests & docs (per the repo's planning rules)

**Tests**
| Test | File | Verifies |
|---|---|---|
| `getLanguageModel` returns a Vertex model when `INFERENCE_PROVIDER=vertex` | `packages/inference/test/index.test.ts` | Registry wiring |
| Throws a clear error when `GOOGLE_CLOUD_PROJECT` is unset | same | Config failure is diagnosable, matching the `GROQ_API_KEY` precedent |
| Outgoing request carries `Authorization: Bearer` and hits the `endpoints/openapi` path | same (mocked `fetch`) | Auth + URL shape without a live call |
| Unknown provider still throws | same | Regression on the existing guard |
| **Live**: one real `patch.generate` returns a schema-valid patch | manual, recorded in the PR | The P2·16 lesson — mocked schema tests did not catch Groq's strict-`required` rejection |

**Docs**
- `synth-platform` root README — provider table: add `vertex`, mark `groq` as retired-with-reason.
- `apps/infra/README.md` — the `roles/aiplatform.user` grant, the removed secret, and the fact that the model is region-locked to `us-central1`.
- `agentsynth/docs/AI_Engine.md` — provider list and the hosted-path note.
- `roadmap.html` §03 — no numbers change (that's the point), but add a line stating the vendor moved at constant cost.

---

## 6. Suggested rewrite of P2·17's framing

> **Was:** "Groq's on-demand tier can't sustain real traffic — survey alternatives, consider a rate-limit increase."
>
> **Is:** "Groq's published limits for `openai/gpt-oss-20b` — 8K TPM and 200K TPD, *identical on the Developer plan* — cannot serve a single 10,650-token request cleanly, and cap the entire organization at ~19 generations/day. This is not a tier problem. Move to Vertex AI's `gpt-oss-20b-maas` (same weights, same price, 1,200 QPM, same GCP project, IAM instead of an API key), and land P6·5's failover to Cerebras in the same PR."

Two additional follow-ups this review surfaced, worth their own tasks rather than folding in here:

- **P2·3's prompt-caching payoff is still unclaimed.** 7,400 of 9,650 input tokens are an identical prefix on every request and no chosen vendor has ever cached them. Worth a task once traffic is dense enough for cache hits to land.
- **P2·11's stale-prompt gap is still open.** The hosted prompt is a frozen pre-Noise snapshot; a provider swap does not fix it, and it will keep depressing the hosted pass rate relative to the local path regardless of who serves the tokens.

---

## Sources

- [Groq — Rate limits (console docs)](https://console.groq.com/docs/rate-limits)
- [Groq — Structured outputs](https://console.groq.com/docs/structured-outputs)
- [Groq free-tier limits, 2026](https://tokenmix.ai/blog/groq-free-tier-limits-2026) · [Groq pricing 2026](https://www.eesel.ai/blog/groq-pricing) · [CloudZero: Groq pricing](https://www.cloudzero.com/blog/groq-pricing/)
- [Groq community — unable to upgrade to Developer tier](https://community.groq.com/t/unable-to-upgrade-to-developer-tier/747) · [Groq self-serve support](https://groq.com/self-serve-support)
- [Vertex AI — gpt-oss & Qwen3 GA as Open Model APIs](https://discuss.google.dev/t/now-ga-openais-gpt-oss-qwen3-models-on-vertex-ai-as-open-model-apis/253945)
- [Vertex AI — structured output for open models](https://cloud.google.com/vertex-ai/generative-ai/docs/maas/capabilities/structured-output) · [gpt-oss-20b model card](https://docs.cloud.google.com/vertex-ai/generative-ai/docs/maas/openai/gpt-oss-20b)
- [Vertex AI — quotas & system limits (dynamic shared quota)](https://docs.cloud.google.com/vertex-ai/generative-ai/docs/quotas)
- [Vertex AI — using OpenAI libraries](https://blevinscm.github.io/genai-docs/migrate/openai/Using-OpenAI-libraries-with-Vertex-AI/)
- [Gemini API pricing](https://ai.google.dev/gemini-api/docs/pricing) · [Gemini API rate limits](https://ai.google.dev/gemini-api/docs/rate-limits) · [Vertex AI context caching](https://docs.cloud.google.com/vertex-ai/generative-ai/docs/context-cache/context-cache-overview)
- [Cerebras — rate limits](https://inference-docs.cerebras.ai/support/rate-limits) · [Artificial Analysis: gpt-oss-120b providers](https://artificialanalysis.ai/models/gpt-oss-120b/providers)
- [Together AI — rate limits](https://docs.together.ai/docs/rate-limits) · [Fireworks — structured responses](https://docs.fireworks.ai/structured-responses/structured-response-formatting) · [Fireworks vs Together pricing 2026](https://www.morphllm.com/comparisons/fireworks-vs-together)
- [OpenRouter pricing 2026 — the 5.5% fee](https://ofox.ai/blog/openrouter-pricing-hidden-markup-breakdown-2026/)
- [Cloud Run — GPU support](https://docs.cloud.google.com/run/docs/configuring/services/gpu) · [NVIDIA L4 cloud pricing](https://getdeploying.com/gpus/nvidia-l4)
- [OpenAI — GPT-5 nano model](https://developers.openai.com/api/docs/models/gpt-5-nano)
- [AI SDK — providers and models](https://ai-sdk.dev/providers/ai-sdk-providers)
