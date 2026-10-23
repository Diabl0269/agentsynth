// KeyGlideTestHost.h -- a pinned clock and a set of "physically down" keys for tests of a held key.
#pragma once

#include "UI/Layout/KeyGlide/KeyGlideHost.h"
#include <set>

namespace key_glide_test {

struct FakeKeys {
    double now = 1000.0;
    std::set<int> down;
    bool shift = false;
    bool alt = false;

    synth::ui::KeyGlideHost host() {
        synth::ui::KeyGlideHost h;
        h.nowMs = [this] { return now; };
        h.isArrowDown = [this](int code) { return down.count(code) > 0; };
        h.isShiftDown = [this] { return shift; };
        h.isAltDown = [this] { return alt; };
        return h;
    }
};

} // namespace key_glide_test
