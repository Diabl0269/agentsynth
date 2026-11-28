"""The equality half of the runner's `expect` step: one getState field against a wanted value.

A wanted value is a plain value, `{remembered: name}` (what a `remember` step kept) or `{differsFrom: name}` (anything
but what it kept, for a display that must have moved).
"""


def compare_field(component, field, want, got, remembered):
    """The problem text for one field, or None when it matches."""
    kept = remembered or {}
    if isinstance(want, dict) and "differsFrom" in want:
        if got == kept.get(want["differsFrom"], object()):
            return "%s of %r: expected it to move, still %r" % (field, component, got)
        return None
    if isinstance(want, dict) and "remembered" in want:
        if want["remembered"] not in kept:
            return "nothing remembered as %r" % want["remembered"]
        want = kept[want["remembered"]]
    if got != want:
        return "%s of %r: expected %r, got %r" % (field, component, want, got)
    return None
