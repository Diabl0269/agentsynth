# PR Title Convention

`.github/workflows/pr-title.yml`'s "PR Title" job validates the **PR title**, not a commit message,
on every pull request. It is a required status check — see
[`ci-pipeline.md`](ci-pipeline.md#required-status-checks) — so a red "PR Title" check blocks a merge
like any other.

**Why the title and not a commit message.** Merges to `main` are squash-only, and GitHub defaults a
squash commit's subject to the PR title unless someone edits it at merge time. The PR title is
therefore the one thing that reliably becomes the commit subject on `main`; a commit-msg hook on
individual commits would miss it entirely.

**Why enforce a type prefix at all.** The release workflow's version job reads commit subjects on
`main` since the last tag to decide the next semver bump: `feat:` earns a minor, everything else
falls through to `default_bump`. When almost every subject was a plain `FROnnn: ...` with no
conventional-commit prefix, the action could parse none of them — every merge fell through to
`default_bump`, which is how the minor number became a merge counter. Lowering `default_bump` from
`minor` to `patch` stopped the inflation but left the opposite gap: a genuine feature with a bare
`FROnnn: ...` subject took a patch bump too, so the version still carried no signal about what
shipped. Enforcing the type prefix is what makes `feat:` actually appear on real features, which is
the only thing that makes the version number mean anything.

## The rule

```
type(scope)!: subject
```

- **type** (required) — one of `feat` `fix` `docs` `ci` `chore` `refactor` `test` `perf` `build`.
  This list is deliberately narrower than the full Conventional Commits spec: no `style`, no
  `revert`. Neither came up when auditing the last 200 PR titles on this repo, and an unused type is
  just another way to pick the wrong one.
- **scope** (optional, parenthesized) — by convention the ticket id this PR closes, e.g. `FRO112`.
  This is what keeps the ticket-id grep-ability the bare `FROnnn: ...` form gave for free: `git log
  --oneline | grep FRO112` still works, it just also gets a type. Omit the scope for a change with
  no ticket (a drive-by `ci:`/`chore:` fix).
- **`!`** (optional) — marks a breaking change, placed immediately before the colon, with or without
  a scope: `feat!:` or `feat(FRO112)!:`. This is the single-line spelling of a breaking change;
  there is no commit-body `BREAKING CHANGE:` footer to check, because the input is a PR title, not a
  full commit message.
- **subject** (required) — free text after `: `. No enforced casing or trailing-period rule; keep
  titles readable over mechanically consistent.

The allowed type list lives in `pr-title.yml` itself, which prints this doc's path on a failure.

Examples that pass:

- `feat(FRO112): knob-and-graph envelope module card`
- `fix(FRO150): mixer fader taper snaps at unity`
- `docs: split the architecture doc`
- `ci(FRO169): docs integrity guard`
- `feat(FRO87)!: drop the legacy macro I/O port format`

Examples that fail: `FRO163: split the architecture doc` (no type), `Feat(FRO112): ...` (uppercase
type), `chore:bump ccache size` (missing space after colon).

**Making a title check required is a one-way door for open PRs.** The moment it becomes a required
status check it fails every PR open at the time, including ones mid-review whose titles predate the
rule, so it lands non-required and the open PRs are retitled by hand before the branch-protection
flip.
