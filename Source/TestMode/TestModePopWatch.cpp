// Concern: the no-pop check. Anything added, removed or swapped on screen must animate; a change that is already
// complete in the first frame that shows it popped. The frames come from the same PresentedFrame the stale-pixel check
// uses: what the peer really painted, in order.

#include "TestMode/TestModePopWatch.h"
#include "MainComponent/MainComponent.h"
#include "TestMode/TestModeComponents.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace synth::testmode {

namespace {

constexpr int kMaxFrames = 40;
constexpr double kCaptureMs = 600.0;
constexpr double kQuietMs = 100.0;
// A window that still changes this long after `ms` is not waited for any longer (a caret blink or a playhead never
// lets it rest).
constexpr double kQuietGiveUpMs = 500.0;
constexpr int kFewPixels = 4;

bool pixelsDiffer(juce::Colour a, juce::Colour b, int tolerance) {
    return std::abs((int)a.getRed() - (int)b.getRed()) > tolerance ||
           std::abs((int)a.getGreen() - (int)b.getGreen()) > tolerance ||
           std::abs((int)a.getBlue() - (int)b.getBlue()) > tolerance ||
           std::abs((int)a.getAlpha() - (int)b.getAlpha()) > tolerance;
}

int countDiffering(const juce::Image::BitmapData& a, const juce::Image::BitmapData& b,
                   const std::vector<juce::Point<int>>& at, int tolerance) {
    int n = 0;
    for (const auto& p : at)
        n += pixelsDiffer(a.getPixelColour(p.x, p.y), b.getPixelColour(p.x, p.y), tolerance) ? 1 : 0;
    return n;
}

bool sameSize(const juce::Image& a, const juce::Image& b) { return a.isValid() && a.getBounds() == b.getBounds(); }

} // namespace

// The frames are only read inside the changed pixels, so a full window of 40 frames costs one full scan and a few
// thousand pixel reads each.
PopVerdict classifyPop(const juce::Image& before, const std::vector<juce::Image>& frames, const juce::Image& final,
                       const juce::RectangleList<int>& ignore, int tolerance) {
    PopVerdict verdict;
    if (!sameSize(before, final))
        return verdict;
    const juce::Image::BitmapData was(before, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData now(final, juce::Image::BitmapData::readOnly);
    std::vector<juce::Point<int>> region;
    for (int y = 0; y < was.height; ++y)
        for (int x = 0; x < was.width; ++x)
            if (pixelsDiffer(was.getPixelColour(x, y), now.getPixelColour(x, y), tolerance) &&
                !ignore.containsPoint({x, y})) {
                region.push_back({x, y});
                verdict.region =
                    verdict.region.isEmpty() ? juce::Rectangle<int>(x, y, 1, 1) : verdict.region.getUnion({x, y, 1, 1});
            }
    verdict.regionPixels = (int)region.size();
    if (region.empty())
        return verdict;
    verdict.changed = true;

    const int shownAt = std::min(kFewPixels, verdict.regionPixels / 2);
    const int closeEnough = std::max(kFewPixels, verdict.regionPixels / 100);
    for (int i = 0; i < (int)frames.size(); ++i) {
        if (!sameSize(frames[(size_t)i], final))
            continue;
        const juce::Image::BitmapData shown(frames[(size_t)i], juce::Image::BitmapData::readOnly);
        if (countDiffering(shown, was, region, tolerance) <= shownAt)
            continue; // still the old picture
        const int stillWrong = countDiffering(shown, now, region, tolerance);
        if (verdict.firstFrame < 0) {
            verdict.firstFrame = i;
            verdict.pop = stillWrong <= closeEnough;
        }
        if (stillWrong > closeEnough)
            ++verdict.intermediateFrames;
    }
    verdict.pop = verdict.pop || verdict.firstFrame < 0; // a change in a paint no frame caught
    return verdict;
}

namespace {

// What popWatchStart armed, read by popWatchCheck. Message thread only.
struct Watch {
    bool armed = false;
    juce::Image before;
    std::vector<juce::Image> frames;
    juce::Image lastPresented; ///< the newest frame that differed from the one before it, kept past the capture limit
    double startMs = 0.0;
    double lastChangeMs = 0.0; ///< when the window last presented a frame that differed from the one before it
    std::optional<int> undoSerial;
};

Watch& watch() {
    static Watch w;
    return w;
}

AppUndoManager* appUndoManager() {
    if (auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow()))
        if (auto* mc = dynamic_cast<MainComponent*>(window->getContentComponent()))
            return &mc->getUndoManager();
    return nullptr;
}

juce::var replyObject(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* o = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        o->setProperty(key, value);
    return juce::var(o);
}

bool sameImage(const juce::Image& a, const juce::Image& b) {
    if (!sameSize(a, b))
        return false;
    const juce::Image::BitmapData x(a, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData y(b, juce::Image::BitmapData::readOnly);
    for (int row = 0; row < x.height; ++row)
        if (std::memcmp(x.getLinePointer(row), y.getLinePointer(row), (size_t)x.width * (size_t)x.pixelStride) != 0)
            return false;
    return true;
}

// Called inside the peer's paint, so the copy is the picture the screen got at that moment.
void captureFrame(const juce::Image& presented) {
    auto& w = watch();
    if (!w.armed)
        return;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto& last = w.lastPresented.isValid() ? w.lastPresented : w.before;
    if (sameImage(presented, last))
        return;
    w.lastPresented = presented.createCopy();
    w.lastChangeMs = now; // an animation the desktop animator does not know (a fade on a VBlank driver) is still moving
    if ((int)w.frames.size() < kMaxFrames && now - w.startMs <= kCaptureMs)
        w.frames.push_back(w.lastPresented);
}

StepResult cmdPopWatchStart(const juce::var&, Scratch&, int) {
    auto* window = findMainWindow();
    if (window == nullptr)
        return StepResult::failed("no main window");
    auto& frame = PresentedFrame::shared();
    if (!frame.isFollowing(*window))
        frame.start(*window);
    auto& w = watch();
    w = {};
    w.before = PresentedFrame::paintFullRepaint(*window);
    if (auto* undo = appUndoManager())
        w.undoSerial = undo->getEditSerial();
    w.startMs = juce::Time::getMillisecondCounterHiRes();
    w.lastChangeMs = w.startMs;
    w.armed = true;
    frame.setPaintObserver(captureFrame);
    return StepResult::done(
        replyObject({{"armed", true}, {"undoSerial", w.undoSerial ? juce::var(*w.undoSerial) : juce::var()}}));
}

juce::RectangleList<int> ignoredAreas(const juce::var& args, juce::Component& window) {
    juce::RectangleList<int> ignore;
    if (const auto* ids = args.getProperty("ignore", {}).getArray())
        for (const auto& id : *ids)
            if (const auto found = findComponent(id.toString()); found.component != nullptr)
                ignore.add(window.getLocalArea(found.component, found.component->getLocalBounds()));
    return ignore;
}

bool writeEvidencePng(const juce::Image& image, const juce::File& file) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream stream(file);
    return stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream);
}

void writeEvidence(const juce::String& dir, const PopVerdict& v, const juce::Image& final,
                   const juce::RectangleList<int>& ignore, int tolerance) {
    const juce::File folder(dir);
    const auto& w = watch();
    const auto& first = v.firstFrame >= 0 ? w.frames[(size_t)v.firstFrame] : w.before;
    writeEvidencePng(w.before, folder.getChildFile("before.png"));
    writeEvidencePng(first, folder.getChildFile("first.png"));
    writeEvidencePng(final, folder.getChildFile("final.png"));
    writeEvidencePng(PresentedFrame::markDifferences(first, final, ignore, tolerance),
                     folder.getChildFile("differences.png"));
}

// Waits until `ms` have passed since the start and the animator has been quiet, then judges what the frames showed.
StepResult cmdPopWatchCheck(const juce::var& args, Scratch& s, int attempt) {
    auto* window = findMainWindow();
    auto& w = watch();
    if (window == nullptr || !w.armed)
        return StepResult::failed("no watch: send popWatchStart first");
    const double ms = args.hasProperty("ms") ? static_cast<double>(args["ms"]) : 300.0;
    const double settleMs = args.hasProperty("settleMs") ? static_cast<double>(args["settleMs"]) : 1500.0;
    const int tolerance = static_cast<int>(args.getProperty("tolerance", kStaleTolerance));
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (attempt == 0) {
        s.vars->setProperty("start", now);
        s.vars->setProperty("quietSince", now);
    }
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* top = desktop.getComponent(i))
            if (auto* peer = top->getPeer())
                peer->performAnyPendingRepaintsNow();
    if (desktop.getAnimator().isAnimating())
        s.vars->setProperty("quietSince", now);
    // The window must also have stopped changing: a fade on a VBlank driver (the status bar's message) is no desktop
    // animation, and a check that ended in its tail started the next watch on a picture still mid-fade.
    if (now - w.lastChangeMs < kQuietMs && now - w.startMs < ms + kQuietGiveUpMs)
        s.vars->setProperty("quietSince", now);
    const bool quiet = now - static_cast<double>(s.vars->getProperty("quietSince")) >= kQuietMs;
    if (!(now - w.startMs >= ms && quiet && attempt >= 3)) {
        if (now - static_cast<double>(s.vars->getProperty("start")) > settleMs + ms)
            return StepResult::failed("not idle after " + juce::String(settleMs + ms) + " ms");
        return StepResult::pending();
    }

    PresentedFrame::shared().setPaintObserver({});
    w.armed = false;
    const auto final = PresentedFrame::paintFullRepaint(*window);
    const auto ignore = ignoredAreas(args, *window);
    const auto v = classifyPop(w.before, w.frames, final, ignore, tolerance);
    const auto dir = args.getProperty("dir", {}).toString();
    if (v.pop && dir.isNotEmpty() && juce::File::isAbsolutePath(dir))
        writeEvidence(dir, v, final, ignore, tolerance);
    std::optional<int> serialAfter;
    if (auto* undo = appUndoManager())
        serialAfter = undo->getEditSerial();
    auto* result = new juce::DynamicObject();
    result->setProperty("changed", v.changed);
    result->setProperty("framesSeen", (int)w.frames.size());
    if (v.changed) {
        result->setProperty("pop", v.pop);
        result->setProperty("regionX", v.region.getX());
        result->setProperty("regionY", v.region.getY());
        result->setProperty("regionW", v.region.getWidth());
        result->setProperty("regionH", v.region.getHeight());
        result->setProperty("regionPixels", v.regionPixels);
        result->setProperty("intermediateFrames", v.intermediateFrames);
    }
    if (w.undoSerial && serialAfter) {
        result->setProperty("undoSerialBefore", *w.undoSerial);
        result->setProperty("undoSerialAfter", *serialAfter);
    }
    return StepResult::done(juce::var(result));
}

} // namespace

void registerPopWatchCommands(CommandTable& table) {
    table.add("popWatchStart", {cmdPopWatchStart});
    table.add("popWatchCheck", {cmdPopWatchCheck});
}

} // namespace synth::testmode
