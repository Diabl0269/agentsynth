# Accounts, Trial and Quota

Signing in, the per-install device id that identifies an anonymous caller, and what the UI does when
a trial or a plan allowance runs out.

## Sign-in surface

The AI panel's account UI is `Source/UI/Assistant/AccountRow.h/.cpp` — a slim status row showing
"Sign in", "Signing in..." or the email plus "Sign out" — and
`Source/UI/Assistant/SignInDialog.h/.cpp`, the modal device-code flow, launched the same way
`SettingsWindow` is, with its content handed to
`juce::DialogWindow::LaunchOptions::content.setOwned(...)` rather than subclassing `DialogWindow`.

`AIChatComponent` owns an `AccountRow` member unconditionally. With no `AccountService` attached —
`setAccountService()` never called — the row is invisible and contributes zero height to `resized()`,
so every caller and test that never attaches one sees byte-identical layout.

### Rotation before use

`AccountService::completeSignIn()` is the one funnel through which both sign-in flows pass once a
fresh access/refresh token pair is in hand, and it holds the invariant: **persist the rotated
refresh token, and confirm the save succeeded, before exposing the access token or notifying
listeners.** Every sign-in and every refresh returns a *new* refresh token, and the auth service
revokes the whole token family if a consumed one reappears; a crash between "use" and "persist"
would leave a dead token in the keychain and silently sign the user out on the next launch. A failed
save publishes a SignedOut snapshot with an error rather than continuing.

`completeSignIn()` also re-checks for cancellation before persisting. Without that check, a
`cancelSignIn()` or `signOut()` landing in the window between "the round trip completed" and "the
tokens are persisted and published" would be silently overwritten, signing the user back in a moment
after they asked not to be.

### Single owner per callback slot

`AccountService::onStateChanged` and `onAccessTokenChanged` are single `std::function` members, not
multicast listener lists. `AIChatComponent::setAccountService(AccountService*)` is the **sole**
setter of both: it installs `onStateChanged` to call `accountRow.refresh()` and
`onAccessTokenChanged` to call `aiService.setAuthToken(token)`. Neither `AccountRow` nor
`SignInDialog` ever assigns those callbacks itself.

- `AccountRow::refresh()` re-reads the snapshot for its own UI, then forwards to a currently-open
  `SignInDialog`, tracked via a non-owning `juce::Component::SafePointer<SignInDialog>` so a dialog
  closed by any path — its own Cancel button, auto-close on success, or the native title bar — never
  leaves a dangling pointer.
- `SignInDialog::refresh()` re-reads the snapshot for its own UI: the code, the status text, and the
  one-time auto-open of the verification URL.

If a second call site ever needs `AccountService`'s callbacks it must go through
`setAccountService()`'s fan-out, extending `refresh()` or its callers, rather than assigning
`onStateChanged`/`onAccessTokenChanged` directly: a second direct assignment anywhere silently
steals the slot from `AIChatComponent`.

Both callback lambdas installed by `setAccountService()` capture a
`juce::Component::SafePointer<AIChatComponent>`, not `this`. This is required because
`AccountService::publishSnapshot()` and `setAccessTokenFromWorker()` copy the `std::function` out of
the member *before* dispatching it via `MessageManager::callAsync()`, so a callback already queued
at the moment `AIChatComponent` is destroyed still runs; the `SafePointer` is what makes that safe.
`~AIChatComponent()` also clears both slots on its stored `AccountService*` as a second layer of
defence.

This is why `MainComponent.h` declares `accountService` **before** `aiChatComponent`: members are
destroyed in reverse declaration order, so `aiChatComponent`, which owns those two callback slots,
is torn down first, while `accountService` is still alive to have them cleared.

`MainComponent::initialiseCommon()` calls `aiChatComponent.setAccountService(&accountService)`
**before** `accountService.attemptSilentSignIn()`, so the wiring is live for any state changes the
silent sign-in attempt produces. `accountService` is default-constructed with the production host
and `KeychainTokenStore`; on a machine with no stored refresh token that attempt is a fast, silent
no-op, which is why every `MainComponent`-constructing test runs at its normal speed.

## Device id and anonymous trial

`Source/Auth/DeviceIdStore.h/.cpp` generates a stable per-install identifier (`juce::Uuid`, dashed
string form) the first time it runs and persists it under the app's standard settings folder
(`userApplicationDataDirectory/<kSettingsFolderName>/device_id`, the same
`getSpecialLocation`/`kSettingsFolderName` convention `ThemeManager` and `SnippetManager` use), then
reuses it for the lifetime of the install. If the file is missing, empty, or its contents do not
look like a plausible id, a fresh one is generated and written rather than crashing or leaving the
id blank — a lost or corrupted id just makes the backend see this install as new, which is harmless.

**Not a secret.** Unlike the refresh token, which is Keychain-backed via `KeychainTokenStore`, the
device id grants no account access by itself; it is only a "this install" signal, so it is
deliberately stored in a plain file. Both `AccountService`, for `AuthClient`, and `RemoteProvider`
construct their own `DeviceIdStore` in their production constructors and read the same persisted
value; their test constructors take an explicit `deviceId` string instead, defaulting to empty with
the field or header omitted, so unit tests never touch the real per-install file.

Where it is sent:

- `AuthClient::requestDeviceCode()`, `pollDeviceToken()` and `refreshToken()` — a `device_id` form
  field alongside `client_id`, whenever non-empty.
- `RemoteProvider` — an `X-Device-Id` header on every `/v1/capability/*` request, sent whether or
  not `Authorization` is also set: an anonymous free-request-tier signal when there is no bearer
  token, an anti-abuse signal once there is one.

**Trial exhausted.** When the free trial is used up the capability endpoint answers
`402 {"error":{"code":"TRIAL_EXHAUSTED","message":"..."}}`, which `RemoteProvider`
[maps](remote-provider.md#error-kind-mapping) to `AIErrorKind::TrialExhausted` with the server's
message carried through unchanged. That response reaches `AIChatComponent` through the same path
every other provider error does, appended to the conversation as an assistant bubble, so it needs no
new UI surface. **Why no upgrade button here:** the caller is not signed in yet, so "upgrade" is not
the right verb. The server's message invites signing in to continue, and the `AccountRow` "Sign in"
affordance is already visible immediately above the chat whenever an `AccountService` is attached,
so opening `SignInDialog` is one click away without this path auto-launching it.

A service-wide `503 {"error":{"code":"SERVICE_CAPACITY_EXCEEDED","message":"..."}}` is handled the
same way but mapped to the distinct `AIErrorKind::ServiceCapacityExceeded`: it is a cap on the
service as a whole, unrelated to the caller's own trial or quota, and gets its own message rather
than being confused with either.

## Quota and the upgrade path

Once a caller is signed in, the backend answers `429 {"error":{"code":"QUOTA_EXCEEDED","message":"..."}}`
when the monthly request quota is exhausted, mapped to `AIErrorKind::Quota` with the server's
message carried through unchanged. Unlike `TrialExhausted`, this *is* the "you are signed in, you
are over your plan, upgrading raises it" moment, so `AIChatComponent` gives it a distinct treatment
instead of the flat error bubble every other kind gets:

- The assistant bubble carries the server's message verbatim, with no `"Error: "` prefix, the same
  as `TrialExhausted` and `ServiceCapacityExceeded`, plus an **"Upgrade to Pro"** button opening
  `synth::branding::kUpgradeUrl` (`Source/Branding.h`, a static checkout link) via the injected
  `urlOpener`. `AIChatComponent::setUrlOpenerForTesting()` swaps that for a non-browser-launching
  fake in tests.
- The button is carried on `MessageData::showUpgradeAction`, which is deliberately **not**
  reconstructed by the history-replay loop in `AIChatComponent`'s constructor: a New Chat or an app
  restart drops it along with the rest of that turn's transient UI state, the same way Cancel-button
  and spinner state never survive a reload.
- A `Quota` error also triggers `AccountService::refreshEntitlement()`, a fire-and-forget re-fetch of
  `GET /v1/entitlement` that updates the account's cached plan, limit and usage without touching
  sign-in state, so a user who upgrades mid-session and immediately retries sees their new plan
  reflected without restarting the app.

**`PlanBadge`** (`Source/UI/Assistant/PlanBadge.h/.cpp`) is a small usage indicator in the same
bottom-chrome stack as `AccountRow` and the model picker, showing `"Free - 240 / 1000 this month"`
or `"Pro - 1,203 / 10,000 this month"`. It follows `AccountRow`'s exact zero-height-when-absent
contract (`setAccountService()`, `refresh()`, `getPreferredHeight()`): invisible and contributing
nothing to layout until an `AccountService` is attached *and* has a known entitlement
(`AccountSnapshot::entitlementKnown`).

`entitlementKnown` is false until the first successful fetch, and a fetch failure is non-fatal — it
mirrors how a failed `fetchMe()` leaves `email` empty rather than failing sign-in — so callers must
check it before trusting `plan`, `monthlyRequestLimit` or `requestsUsed` rather than treating a
default-constructed plan as "Free". `AccountService::completeSignIn()` populates it alongside
`fetchMe()`, and `refreshEntitlement()` updates it on demand.
`AuthClient::fetchEntitlement()` degrades to `requestsUsed = 0` rather than failing the whole parse
if a server does not send that field.
