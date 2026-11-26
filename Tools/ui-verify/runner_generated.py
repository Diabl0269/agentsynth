"""Two scenario options for a check that has no project, or that needs a signed-in account.

`test_account: true` launches the app signed in as the fixed test account (--test-account): a signed-in state with no
sign-in flow, no keychain and no network, using a fake token no server accepts. It only works in a test-mode launch.

`- generateProject: {module: osc}` builds the smallest project holding that module: the default project plus one
timeline track whose instrument is `module` (the text typed into the Add track picker). It expands, when the scenario
is read, into the same steps a person takes, so there is nothing new to run.
"""

GENERATE = "generateProject"


def expand_generate_project(args, step_number, name):
    """The steps `generateProject` stands for. `args` is {module: <picker text>}; raises ValueError when it is not."""
    module = args.get("module") if isinstance(args, dict) else None
    if not isinstance(module, str) or not module.strip() or set(args) - {"module"}:
        raise ValueError("%s: step %d (%s) takes `module: <text typed into the Add track picker>` and nothing else"
                         % (name, step_number, GENERATE))
    return [
        ("click", {"id": "Open our default project"}),
        ("wait", {}),
        ("click", {"id": "toggleBottomPanel"}),
        ("wait", {}),
        ("expect", {"id": "timelinePanel", "showing": True, "within_ms": 3000}),
        ("click", {"id": "timelineAddTrackButton"}),
        ("wait", {}),
        ("type", {"text": module}),
        ("key", {"keys": "return"}),
        ("wait", {}),
    ]
