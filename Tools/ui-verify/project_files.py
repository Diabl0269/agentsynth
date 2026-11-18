"""Project bundle helpers of the scenario runner (a saved project must open after the run)."""

import os
import re
import shutil


def localise_scratch_files(bundle, scratch):
    """Makes a saved project open after the run: any file the project names inside the run's scratch folder (an own
    copy of a fixture sound pack instrument, say) is copied into the bundle's `Samples/` and named there by a
    bundle-relative path, because the scratch folder is deleted when the run ends (FRO1887: a check's project opened
    with an empty instrument). Returns the copied file names."""
    project_json = os.path.join(bundle, "project.json")
    if not scratch or not os.path.isfile(project_json):
        return []
    with open(project_json, encoding="utf-8") as handle:
        text = handle.read()
    prefixes = {scratch, os.path.realpath(scratch)}
    pattern = re.compile(r'"((?:%s)/[^"]+)"' % "|".join(re.escape(p) for p in sorted(prefixes, key=len, reverse=True)))
    copied = {}
    taken = set()

    def swap(match):
        source = match.group(1)
        if source not in copied:
            if not os.path.isfile(source):
                return match.group(0)
            name = os.path.basename(source)
            stem, ext = os.path.splitext(name)
            n = 1
            while name in taken:
                n += 1
                name = "%s-%d%s" % (stem, n, ext)
            taken.add(name)
            os.makedirs(os.path.join(bundle, "Samples"), exist_ok=True)
            shutil.copyfile(source, os.path.join(bundle, "Samples", name))
            copied[source] = name
        return '"Samples/%s"' % copied[source]

    rewritten = pattern.sub(swap, text)
    if copied:
        with open(project_json, "w", encoding="utf-8") as handle:
            handle.write(rewritten)
    return sorted(copied.values())
