// Concern: the test-mode command registry -- one handler per protocol command, all run on the message thread.

#include "TestMode/TestModeCommands.h"
#include "MainComponent/MainComponent.h"
#include "ProjectBundle.h"
#include "TestMode/TestModeComponents.h"
#include "TestMode/TestModeInput.h"
#include "TestMode/TestModePresentedFrame.h"
#include <cmath>
#if JUCE_WINDOWS
#include <process.h>
#else
#include <unistd.h>
#endif

namespace synth::testmode {

namespace {

constexpr int kDefaultDragSteps = 12;
// A partial repaint and a full one can leave an anti-aliased pixel a few levels apart: a zoomed canvas half-covers the
// pixel column at its edge and blends it over what was there, not over the background (up to 4 levels seen). A
// left-behind dot, cable or text pixel differs by 9 levels and far more.
constexpr int kStaleTolerance = 6;

int processId() {
#if JUCE_WINDOWS
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

juce::var pointToVar(juce::Point<int> p) {
    auto* o = new juce::DynamicObject();
    o->setProperty("x", p.x);
    o->setProperty("y", p.y);
    return juce::var(o);
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* o = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        o->setProperty(key, value);
    return juce::var(o);
}

// The component named by args.<key>, or an error result.
Lookup lookupArg(const juce::var& args, const char* key) {
    const auto id = args.getProperty(key, {}).toString();
    if (id.isEmpty()) {
        Lookup missing;
        missing.error = juce::String("missing \"") + key + "\"";
        return missing;
    }
    return findComponent(id);
}

juce::Point<int> localPointArg(const juce::var& args, const juce::Component& c, const char* xKey, const char* yKey) {
    const auto centre = centreOf(c);
    return {args.hasProperty(xKey) ? static_cast<int>(args[xKey]) : centre.x,
            args.hasProperty(yKey) ? static_cast<int>(args[yKey]) : centre.y};
}

// The modifier names of args.mods: one string ("shift") or a list of strings. Anything else sets `error` rather than
// being dropped, so a typo in a scenario never silently clicks without its modifier.
juce::ModifierKeys modifiersArg(const juce::var& args, juce::String& error) {
    const auto value = args.getProperty("mods", {});
    juce::StringArray names;
    if (const auto* list = value.getArray()) {
        for (const auto& item : *list) {
            if (!item.isString()) {
                error = "\"mods\" must hold modifier names (cmd, shift, alt, ctrl)";
                return {};
            }
            names.add(item.toString());
        }
    } else if (value.isString()) {
        names.add(value.toString());
    } else if (!value.isVoid()) {
        error = "\"mods\" must be a modifier name or a list of them (cmd, shift, alt, ctrl)";
        return {};
    }
    return modifiersFromNames(names, error);
}

MainComponent* mainComponent() {
    if (auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow()))
        return dynamic_cast<MainComponent*>(window->getContentComponent());
    return nullptr;
}

StepResult cmdPing(const juce::var&, Scratch&, int) {
    const auto version = juce::JUCEApplicationBase::getInstance() != nullptr
                             ? juce::JUCEApplicationBase::getInstance()->getApplicationVersion()
                             : juce::String();
    return StepResult::done(object({{"pid", processId()}, {"version", version}}));
}

StepResult cmdList(const juce::var& args, Scratch&, int) {
    const int depth = args.hasProperty("depth") ? static_cast<int>(args["depth"]) : 6;
    const bool all = static_cast<bool>(args.getProperty("all", false));
    if (args.hasProperty("root")) {
        const auto found = lookupArg(args, "root");
        return found.component != nullptr ? StepResult::done(describeTree(*found.component, depth, all))
                                          : StepResult::failed(found.error);
    }
    juce::Array<juce::var> tops;
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* top = desktop.getComponent(i); top != nullptr && (all || top->isShowing()))
            tops.add(describeTree(*top, depth, all));
    return StepResult::done(tops);
}

StepResult cmdClick(const juce::var& args, Scratch&, int) {
    const auto found = lookupArg(args, "id");
    if (found.component == nullptr)
        return StepResult::failed(found.error);
    juce::String error;
    const auto keys = modifiersArg(args, error);
    const auto button = args.getProperty("button", "left").toString() == "right" ? Button::right : Button::left;
    if (error.isNotEmpty())
        return StepResult::failed(error);
    const auto point = localPointArg(args, *found.component, "x", "y");
    const int clicks = args.hasProperty("clicks") ? static_cast<int>(args["clicks"]) : 1;
    if (!click(*found.component, point, button, clicks, keys, error))
        return StepResult::failed(error);
    return StepResult::done(object({{"id", found.component->getComponentID()}, {"point", pointToVar(point)}}));
}

StepResult cmdWheel(const juce::var& args, Scratch&, int) {
    const auto found = lookupArg(args, "id");
    if (found.component == nullptr)
        return StepResult::failed(found.error);
    juce::String error;
    const auto keys = modifiersArg(args, error);
    if (error.isNotEmpty())
        return StepResult::failed(error);
    const auto point = localPointArg(args, *found.component, "x", "y");
    const float dx = args.hasProperty("dx") ? static_cast<float>(static_cast<double>(args["dx"])) : 0.0f;
    const float dy = args.hasProperty("dy") ? static_cast<float>(static_cast<double>(args["dy"])) : 0.0f;
    if (!wheel(*found.component, point, dx, dy, keys, error))
        return StepResult::failed(error);
    return StepResult::done(object({{"id", found.component->getComponentID()}, {"point", pointToVar(point)}}));
}

// A mouse move with no button down: what a user does to rest the pointer on something (a hover, a tooltip).
StepResult cmdHover(const juce::var& args, Scratch&, int) {
    const auto found = lookupArg(args, "id");
    if (found.component == nullptr)
        return StepResult::failed(found.error);
    const auto point = localPointArg(args, *found.component, "x", "y");
    juce::String error;
    if (!sendMouse({found.component, point, juce::ModifierKeys(), false, Button::left}, error))
        return StepResult::failed(error);
    return StepResult::done(object({{"id", found.component->getComponentID()}, {"point", pointToVar(point)}}));
}

// One held-button step apart from the others, so a scenario can read the app with the button still down. `press`
// arrives first and then presses, as a hand does; a release is sent where the pointer ends up.
StepResult sendHeldMouse(const juce::var& args, bool buttonDown, bool arriveFirst) {
    const auto found = lookupArg(args, "id");
    if (found.component == nullptr)
        return StepResult::failed(found.error);
    juce::String error;
    const auto keys = modifiersArg(args, error);
    if (error.isNotEmpty())
        return StepResult::failed(error);
    const auto point = localPointArg(args, *found.component, "x", "y");
    if (arriveFirst && !sendMouse({found.component, point, keys, false, Button::left}, error))
        return StepResult::failed(error);
    if (!sendMouse({found.component, point, keys, buttonDown, Button::left}, error))
        return StepResult::failed(error);
    return StepResult::done(object({{"id", found.component->getComponentID()}, {"point", pointToVar(point)}}));
}

StepResult cmdMouseDown(const juce::var& args, Scratch&, int) { return sendHeldMouse(args, true, true); }
StepResult cmdMouseMoveHeld(const juce::var& args, Scratch&, int) { return sendHeldMouse(args, true, false); }
StepResult cmdMouseUp(const juce::var& args, Scratch&, int) { return sendHeldMouse(args, false, false); }

// A drag is one hop per mouse event, so timers and async repaints run between the moves (JUCE's drag threshold and
// auto-scroll both depend on that).
StepResult cmdDrag(const juce::var& args, Scratch& s, int attempt) {
    const int steps = juce::jmax(2, args.hasProperty("steps") ? static_cast<int>(args["steps"]) : kDefaultDragSteps);
    juce::String error;
    const auto keys = modifiersArg(args, error);
    if (error.isNotEmpty())
        return StepResult::failed(error);

    const int index = attempt; // 0 press, 1..steps moves, steps + 1 release
    if (!s.vars->hasProperty("ax")) {
        const auto from = lookupArg(args, "from");
        const auto to = lookupArg(args, "to");
        if (from.component == nullptr || to.component == nullptr)
            return StepResult::failed(from.component == nullptr ? from.error : to.error);
        auto* peer = from.component->getPeer();
        if (peer == nullptr || to.component->getPeer() != peer)
            return StepResult::failed("both ends of a drag must be on the same native window");
        auto& top = peer->getComponent();
        const auto a = top.getLocalPoint(from.component, localPointArg(args, *from.component, "fromX", "fromY"));
        const auto b = top.getLocalPoint(to.component, localPointArg(args, *to.component, "toX", "toY"));
        s.target = &top;
        s.vars->setProperty("ax", a.x);
        s.vars->setProperty("ay", a.y);
        s.vars->setProperty("bx", b.x);
        s.vars->setProperty("by", b.y);
    }
    if (s.target == nullptr)
        return StepResult::failed("the window closed during the drag");

    const juce::Point<int> a(s.vars->getProperty("ax"), s.vars->getProperty("ay"));
    const juce::Point<int> b(s.vars->getProperty("bx"), s.vars->getProperty("by"));
    const float t = juce::jlimit(0.0f, 1.0f, static_cast<float>(index) / static_cast<float>(steps));
    const juce::Point<int> where(juce::roundToInt(a.x + (b.x - a.x) * t), juce::roundToInt(a.y + (b.y - a.y) * t));

    MouseStep move{s.target.getComponent(), where, keys, true, Button::left};
    if (index == 0) {
        move.buttonDown = false; // arrive first, then press
        if (!sendMouse(move, error))
            return StepResult::failed(error);
        move.buttonDown = true;
    } else if (index == steps + 1) {
        move.buttonDown = false;
    }
    if (!sendMouse(move, error))
        return StepResult::failed(error);
    return index == steps + 1 ? StepResult::done(object({{"from", pointToVar(a)}, {"to", pointToVar(b)}}))
                              : StepResult::pending();
}

StepResult cmdKey(const juce::var& args, Scratch&, int) {
    juce::String error;
    const auto key = keyPressFromText(args.getProperty("keys", {}).toString(), error);
    if (error.isNotEmpty())
        return StepResult::failed(error);
    juce::Component* target = nullptr;
    if (args.hasProperty("target")) {
        const auto found = lookupArg(args, "target");
        if (found.component == nullptr)
            return StepResult::failed(found.error);
        target = found.component;
        if (target->getWantsKeyboardFocus())
            target->grabKeyboardFocus();
    } else {
        target = findMainWindow();
    }
    if (target == nullptr)
        return StepResult::failed("no window");
    bool handled = false;
    if (!pressKey(*target, key, handled, error))
        return StepResult::failed(error);
    return StepResult::done(object({{"handled", handled}}));
}

StepResult cmdGetState(const juce::var& args, Scratch&, int) {
    const auto found = lookupArg(args, "id");
    return found.component != nullptr ? StepResult::done(describeState(*found.component))
                                      : StepResult::failed(found.error);
}

StepResult cmdScreenshot(const juce::var& args, Scratch&, int) {
    const auto found = findComponent(args.hasProperty("id") ? args["id"].toString() : juce::String("window"));
    if (found.component == nullptr)
        return StepResult::failed(found.error);
    const auto path = args.getProperty("path", {}).toString();
    if (path.isEmpty() || !juce::File::isAbsolutePath(path))
        return StepResult::failed("\"path\" must be an absolute file path");
    const float scale = args.hasProperty("scale") ? static_cast<float>(static_cast<double>(args["scale"])) : 1.0f;
    const ScopedOffscreenPaint offscreen; // not what the screen shows: the presented frame ignores it
    const auto image = found.component->createComponentSnapshot(found.component->getLocalBounds(), true, scale);
    if (!image.isValid())
        return StepResult::failed("the component has no size to capture");
    const juce::File file(path);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream stream(file);
    if (!stream.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, stream))
        return StepResult::failed("could not write " + path);
    return StepResult::done(object({{"path", path}, {"width", image.getWidth()}, {"height", image.getHeight()}}));
}

StepResult cmdOpenProject(const juce::var& args, Scratch& s, int attempt) {
    const juce::File bundle(args.getProperty("path", {}).toString());
    const auto expected = bundle.getFileNameWithoutExtension();
    auto* mc = mainComponent();
    if (mc == nullptr)
        return StepResult::failed("no main window");
    if (attempt == 0) {
        if (!ProjectBundle::isBundle(bundle))
            return StepResult::failed("not a project bundle: " + bundle.getFullPathName());
        if (mc->isProjectDirty())
            return StepResult::failed("the open project has unsaved changes; opening would ask first");
        if (ProjectBundle::hasAutosave(bundle))
            return StepResult::failed("the project has an autosave sidecar; opening would ask first");
        mc->openProjectFromCommandLine(bundle);
    }
    juce::ignoreUnused(s);
    return mc->getCurrentPatchName() == expected ? StepResult::done(object({{"name", expected}}))
                                                 : StepResult::pending();
}

StepResult cmdSaveProject(const juce::var& args, Scratch&, int) {
    const juce::File bundle(args.getProperty("path", {}).toString());
    auto* mc = mainComponent();
    if (mc == nullptr)
        return StepResult::failed("no main window");
    if (!bundle.hasFileExtension(".agsproj"))
        return StepResult::failed("not a project bundle path: " + bundle.getFullPathName());
    return mc->saveProjectForTest(bundle) ? StepResult::done(object({{"path", bundle.getFullPathName()}}))
                                          : StepResult::failed("could not save " + bundle.getFullPathName());
}

StepResult cmdWaitForIdle(const juce::var& args, Scratch& s, int attempt) {
    const double timeoutMs = args.hasProperty("timeoutMs") ? static_cast<double>(args["timeoutMs"]) : 5000.0;
    const double settleMs = args.hasProperty("settleMs") ? static_cast<double>(args["settleMs"]) : 100.0;
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
    if (now - static_cast<double>(s.vars->getProperty("quietSince")) >= settleMs && attempt >= 3)
        return StepResult::done(object({{"waitedMs", now - static_cast<double>(s.vars->getProperty("start"))}}));
    if (now - static_cast<double>(s.vars->getProperty("start")) > timeoutMs)
        return StepResult::failed("not idle after " + juce::String(timeoutMs) + " ms");
    return StepResult::pending();
}

bool writePng(const juce::Image& image, const juce::File& file) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream stream(file);
    return stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream);
}

StepResult cmdTrackPresentedFrame(const juce::var& args, Scratch&, int) {
    auto& frame = PresentedFrame::shared();
    if (static_cast<bool>(args.getProperty("stop", false))) {
        frame.stop();
        return StepResult::done(object({{"following", false}}));
    }
    auto* window = findMainWindow();
    if (window == nullptr)
        return StepResult::failed("no main window");
    frame.start(*window);
    return StepResult::done(
        object({{"following", true}, {"width", frame.image().getWidth()}, {"height", frame.image().getHeight()}}));
}

// Compared again every 50 ms until the frame equals a full repaint, so paints still on their way (a fade, a glide, a
// blinking caret) settle first; whatever still differs after `settleMs` is a stale pixel.
StepResult cmdCheckStalePixels(const juce::var& args, Scratch& s, int attempt) {
    auto* window = findMainWindow();
    if (window == nullptr || !PresentedFrame::shared().isFollowing(*window))
        return StepResult::failed("not following the window: send trackPresentedFrame first");
    const double settleMs = args.hasProperty("settleMs") ? static_cast<double>(args["settleMs"]) : 1500.0;
    const int tolerance = static_cast<int>(args.getProperty("tolerance", kStaleTolerance));
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (attempt == 0)
        s.vars->setProperty("start", now);
    else if (now - static_cast<double>(s.vars->getProperty("compared")) < 50.0)
        return StepResult::pending();
    s.vars->setProperty("compared", now);
    juce::RectangleList<int> ignore;
    if (const auto* ids = args.getProperty("ignore", {}).getArray())
        for (const auto& id : *ids)
            if (const auto found = findComponent(id.toString()); found.component != nullptr)
                ignore.add(window->getLocalArea(found.component, found.component->getLocalBounds()));
    auto& frame = PresentedFrame::shared();
    const auto presented = frame.image().createCopy();
    const auto difference = frame.compareWithFullRepaint(ignore, tolerance);
    const double waited = now - static_cast<double>(s.vars->getProperty("start"));
    if (difference.pixels == 0) {
        const auto counts = frame.takePaintCounts();
        return StepResult::done(object({{"differing", 0},
                                        {"waitedMs", waited},
                                        {"paints", counts.paints},
                                        {"wholeWindowPaints", counts.wholeWindow}}));
    }
    if (waited < settleMs)
        return StepResult::pending();
    const auto b = difference.bounds;
    juce::String message = juce::String(difference.pixels) + " pixels differ from a full repaint, in x " +
                           juce::String(b.getX()) + " y " + juce::String(b.getY()) + " w " +
                           juce::String(b.getWidth()) + " h " + juce::String(b.getHeight());
    const auto dir = args.getProperty("dir", {}).toString();
    if (dir.isNotEmpty() && juce::File::isAbsolutePath(dir)) {
        const juce::File folder(dir);
        writePng(presented, folder.getChildFile("presented.png"));
        writePng(difference.fullRepaint, folder.getChildFile("full-repaint.png"));
        writePng(PresentedFrame::markDifferences(presented, difference.fullRepaint, ignore, tolerance),
                 folder.getChildFile("differences.png"));
        message << " (presented.png, full-repaint.png, differences.png in " << dir << ")";
    }
    frame.start(*window); // the next check starts from what a full repaint shows
    return StepResult::failed(message);
}

StepResult cmdQuit(const juce::var&, Scratch&, int) {
    // After the reply has gone out: quit() only stops the dispatch loop, so the socket thread still writes it.
    juce::Timer::callAfterDelay(150, [] {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->quit();
    });
    return StepResult::done(object({{"quitting", true}}));
}

// What the native Quit menu item reaches (applicationShouldTerminate -> systemRequestedQuit), without the key: with
// unsaved changes the app asks first. After the reply, like quit.
StepResult cmdRequestQuit(const juce::var&, Scratch&, int) {
    juce::Timer::callAfterDelay(150, [] {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
    });
    return StepResult::done(object({{"requested", true}}));
}

} // namespace

CommandTable CommandTable::standard() {
    CommandTable table;
    table.add("ping", {cmdPing});
    table.add("list", {cmdList});
    table.add("click", {cmdClick});
    table.add("wheel", {cmdWheel});
    table.add("hover", {cmdHover});
    table.add("mouseDown", {cmdMouseDown});
    table.add("mouseMoveHeld", {cmdMouseMoveHeld});
    table.add("mouseUp", {cmdMouseUp});
    table.add("drag", {cmdDrag});
    table.add("key", {cmdKey});
    table.add("getState", {cmdGetState});
    table.add("screenshot", {cmdScreenshot});
    table.add("openProject", {cmdOpenProject});
    table.add("saveProject", {cmdSaveProject});
    table.add("waitForIdle", {cmdWaitForIdle});
    table.add("trackPresentedFrame", {cmdTrackPresentedFrame});
    table.add("checkStalePixels", {cmdCheckStalePixels});
    table.add("quit", {cmdQuit});
    table.add("requestQuit", {cmdRequestQuit});
    return table;
}

const CommandSpec* CommandTable::find(const juce::String& name) const {
    const auto it = commands_.find(name);
    return it == commands_.end() ? nullptr : &it->second;
}

juce::StringArray CommandTable::names() const {
    juce::StringArray out;
    for (const auto& entry : commands_)
        out.add(entry.first);
    return out;
}

} // namespace synth::testmode
