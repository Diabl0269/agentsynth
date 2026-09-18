# PR title convention

Enforced by `.github/workflows/pr-title.yml` ("PR Title" job) on every pull request, and it
validates the **PR title**, not a commit message. Merges to `main` are squash-only, and GitHub
defaults a squash commit's subject to the PR title unless someone edits it at merge time -- so
the PR title is the one thing that reliably becomes the commit subject on `main`, and a
commit-msg hook on individual commits would miss it entirely.

## Why

`build-artifacts.yml`'s version job (`mathieudutour/github-tag-action`) reads commit subjects on
`main` since the last tag to decide the next semver bump: `feat:` earns a minor, everything else
falls through to `default_bump`. Before FRO182, 150 of the last 200 commit subjects on `main`
were plain `FROnnn: ...` with no conventional-commit prefix, so the action could parse almost
none of them -- every merge fell through to `default_bump`, which is exactly why the repo reached
v0.246.0 with a minor number that had become a merge counter (FRO169 changed `default_bump` from
`minor` to `patch` to stop the inflation, but that alone leaves the opposite gap: a genuine
feature whose subject is a bare `FROnnn: ...` now takes a patch bump too, so the version still
carries no signal about what shipped). Enforcing the type prefix is what makes `feat:` actually
appear on real features, which is the only way the version number means anything again.

## The rule

```
type(scope)!: subject
```

- **type** (required) -- one of `feat` `fix` `docs` `ci` `chore` `refactor` `test` `perf` `build`.
  This list is deliberately narrower than the full Conventional Commits spec (no `style`, no
  `revert`): those two didn't come up when auditing the last 200 PR titles on this repo, and an
  unused type is just another way to pick the wrong one.
- **scope** (optional, parenthesized) -- by convention, the ticket id this PR closes, e.g.
  `FRO112`. This is what keeps the ticket-id grep-ability the old bare `FROnnn: ...` convention
  gave for free: `git log --oneline | grep FRO112` still works, it just also gets a type. Omit
  the scope for a change with no ticket (a drive-by `ci:`/`chore:` fix).
- **`!`** (optional) -- marks a breaking change, placed immediately before the colon (with or
  without a scope: `feat!:` or `feat(FRO112)!:`). This is the single-line spelling of a breaking
  change; there is no commit-body `BREAKING CHANGE:` footer to check because the input is a PR
  title, not a full commit message.
- **subject** (required) -- free text after `: `. No enforced casing or trailing-period rule --
  keep titles readable over mechanically consistent.

Examples that pass:

- `feat(FRO112): knob-and-graph envelope module card`
- `fix(FRO150): mixer fader taper snaps at unity`
- `docs: split docs/architecture.md`
- `ci(FRO169): docs integrity guard`
- `feat(FRO87)!: drop the legacy macro I/O port format`

Examples that fail: `FRO163: split docs/architecture.md` (no type), `Feat(FRO112): ...`
(uppercase type), `chore:bump ccache size` (missing space after colon).

## Rollout

The check landed **not required** (FRO182) -- the moment it becomes a required status check it
fails every PR open at the time, including ones mid-review with titles predating this rule. FRO170
made it required, in the same branch-protection pass as the "Docs" check (`.github/CLAUDE.md`
names the six strings now required; renaming or reordering them there is what actually gates a
merge) -- the two open PRs that predated the rule were retitled by hand first. A red "PR Title"
check now blocks a normal merge same as any other required check.
