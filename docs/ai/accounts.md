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

### Where the refresh token lives per platform

macOS: the Keychain. Windows: the Windows Credential Manager (a generic credential named after the
same service string, kept per machine for the signed-in user), so sign-in survives a restart. Linux:
in memory only, so the user signs in again on each launch. All three sit behind `KeychainTokenStore`.

### Keychain access and permission prompts

macOS pins a Keychain item to the code signature of the build that created it, so a build signed
differently (every ad-hoc build, a different identity) can be asked for permission on each access.
`KeychainTokenStore` therefore touches the Keychain as little as it can: it reads the item once per
process and serves later `load()` calls from memory, skips a `save()` of an unchanged token, and
updates the one item in place instead of deleting and re-adding it (a refused delete used to leave
the old item behind, so the add failed as a duplicate and sign-in could not persist its token). A
refused delete on sign-out falls back to blanking the stored value, which `load()` reads as signed
out. The data-protection Keychain would avoid prompts entirely but needs a provisioned entitlement
that ad-hoc and Apple Development builds do not have (`errSecMissingEntitlement`, -34018); stable
signing, `scripts/dev-sign-app.sh` locally and Developer ID for releases, is what stops the prompt
after the first Always Allow.

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

## Account tab

Settings > Account (`Source/UI/Settings/AccountSettingsTab`), next to the AI tab; it exists only when the
window was given an `AccountService`. Signed out it shows one line and a Sign in button (the same
`launchSignInDialog` the AI panel's row uses). Signed in it shows the email, the plan line
(`PlanBadge::formatText`, "Pro · 42 of 500 requests this month"), "Renews <date>" or, once the
subscription is set to stop, "Ends <date>" (`PlanBadge::formatPeriodLine`, from the snapshot's
`periodEndIso` / `cancelAtPeriodEnd`), Sign out, and:

- **Manage subscription** (Pro) opens a popover (`AccountFlowPanel`, a call-out growing out of the button) with
  "Change payment or see invoices" (opens `kBillingPortalUrl`) and "Cancel my subscription" (the leaving
  question first, then the portal). Free shows **Upgrade to Pro**, the existing checkout link.
- **Delete account…** opens a confirmation that lists what is removed, then the leaving question, then
  `DELETE /v1/account` (`AuthClient::deleteAccount`). 204 signs out locally (stored tokens cleared like Sign out)
  and the tab says "Your account was deleted."; 409 `SUBSCRIPTION_ACTIVE` says to cancel first and offers Manage
  subscription; any other error shows the server's message, or a network line, with Try again.
- **The leaving question** (`LeavingSurveyPanel`): eight reason toggles, an optional comment (2000 characters),
  Skip and Continue. Continue sends `POST /v1/exit-survey` `{kind: "cancel"|"delete", reasons, comment?}`
  (`AuthClient::submitExitSurvey`), Skip sends nothing, and neither blocks leaving: a failed post is ignored. For
  delete the answer is posted first (the token dies with the account), then the delete follows regardless.

Requests run through `AccountRequests` (a detached worker with a copied `AuthClient`, the result handed back on the
message thread), like the Feedback tab's sync. Unlike the other calls there is no refresh-and-retry on a 401: the
client has none, so an expired session shows "sign in again". Esc closes a panel and focus returns to its button.
The tab watches the snapshot with a 250 ms timer that runs only while it is showing, because the AI chat owns
`onStateChanged`. Tests: `Tests/UI/Settings/Account*Tests.cpp`, `LeavingSurveyPanelTests.cpp`,
`Tests/Account/AuthClient/AuthClientExitSurveyTests.cpp`.

## Device id and anonymous trial

`Source/Auth/DeviceIdStore.h/.cpp` generates a stable per-install identifier (`juce::Uuid`, dashed
string form) the first time it runs and persists it under the app's standard settings folder
(`userApplicationDataDirectory/<kSettingsFolderName>/device_id`, the same
`getSpecialLocation`/`kSettingsFolderName` convention `ThemeManager` and `SnippetManager` use), then
reuses it for the lifetime of the install. If the file is missing, empty, or its contents do not
look like a plausible id, a fresh one is generated and written rather than crashing or leaving the
id blank — a lost or corrupted id just makes the backend see this install as new, which is harmless.

**Not a secret.** Unlike the refresh token, which is held by `KeychainTokenStore` (Keychain on macOS, Credential Manager on Windows), the
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
  `urlOpener`, with the signed-in account's email appended as `?customer_email=` by
  `buildUpgradeUrl()` (`Source/AI/UpgradeUrl.h`), because the server activates Pro by matching the
  Polar customer's email to the account's. The bare link is used when no email is known. `AIChatComponent::setUrlOpenerForTesting()` swaps that for a non-browser-launching
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
