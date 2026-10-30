// ThemeSweepSurfaces.cpp -- the registry behind the theme sweep: how each dialog, window, alert and module card is
// opened or built headless. A dialog opener builds the same content with the same LaunchOptions as its call site in
// Source and launches it through PopupMotion::launchDialog (or calls the real opener where a test can).
#include "ThemeSweepSurfaces.h"
#include "../../Accessibility/AccessibilitySettingsFixture.h"
#include "../../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapperInternal.h"
#include "AI/AccountService.h"
#include "AudioEngine/AudioEngine.h"
#include "Auth/InMemoryTokenStore.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/SamplerModule.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Assistant/SignInDialog.h"
#include "UI/Chrome/ExportAudioDialog.h"
#include "UI/Chrome/RenderProgressDialog.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapView.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapWindow.h"
#include "UI/Layout/DetachablePanelHost/DetachedPanelWindow.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Macros/MacroPortConfigDialog/MacroPortConfigDialog.h"
#include "UI/ModuleViews/EQWindow.h"
#include "UI/ModuleViews/ShapeEditor/ShapeEditorWindow.h"
#include "UI/Settings/SettingsWindow.h"
#include <algorithm>

using namespace synth::ui;
using namespace synth::ui::test;

namespace synth::test::theme_sweep {

// Stands in for MainComponent: the command target above the canvas, recording the keys handed to it.
struct AppHost
    : juce::Component
    , juce::ApplicationCommandTarget {
    AppHost()
        : canvas(engine) {
        canvas.getCardKeyboard().setShortcutManager(&shortcuts);
        addAndMakeVisible(canvas);
        canvas.addAndMakeVisible(anchor);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        received.push_back(key);
        return true;
    }
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>&) override {}
    void getCommandInfo(juce::CommandID, juce::ApplicationCommandInfo&) override {}
    bool perform(const InvocationInfo&) override { return false; }

    AudioEngine engine;
    GraphEditor canvas;
    ShortcutManager shortcuts;
    juce::Component anchor;
    std::vector<juce::KeyPress> received;
};

SweepEnvironment::SweepEnvironment(const synth::theme::Theme& theme)
    : theme_(theme) {
    lookAndFeel_.applyTheme(theme_);
    anchor_.setSize(1400, 900);
    anchor_.setLookAndFeel(&lookAndFeel_);
    previousDefault_ = &juce::Desktop::getInstance().getDefaultLookAndFeel();
    juce::Desktop::getInstance().setDefaultLookAndFeel(&lookAndFeel_);
}

juce::Component& SweepEnvironment::appAnchor() {
    if (appHost_ == nullptr)
        appHost_ = std::make_unique<AppHost>();
    return appHost_->anchor;
}

std::vector<juce::KeyPress>& SweepEnvironment::appKeys() {
    appAnchor();
    return appHost_->received;
}

SweepEnvironment::~SweepEnvironment() {
    deleteOpenModalDialogs();
    kept_.clear();
    anchor_.setLookAndFeel(nullptr);
    juce::Desktop::getInstance().setDefaultLookAndFeel(previousDefault_);
}

namespace {

// The Settings window's dependencies (the accessibility tests' fixture, without the gtest base).
struct SettingsDeps {
    SettingsDeps() {
        AccessibilitySettingsTest::useRunSettingsFile(appProperties);
        engine = std::make_unique<AudioEngine>();
        aiService = std::make_unique<synth::AIIntegrationService>(engine->getGraph());
        aiService->setProvider(std::make_unique<synth::test::MockProviderACT>());
        aiChat = std::make_unique<synth::AIChatComponent>(*aiService, appProperties);
    }
    ~SettingsDeps() {
        if (auto* userSettings = appProperties.getUserSettings())
            userSettings->clear();
    }
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<synth::AIIntegrationService> aiService;
    std::unique_ptr<synth::AIChatComponent> aiChat;
    juce::ApplicationProperties appProperties;
    juce::AudioDeviceManager deviceManager;
    ShortcutManager shortcutManager;
    synth::theme::ThemeManager themeManager;
};

// The options the call sites share: a native title bar around the content, centred on the anchor.
juce::DialogWindow* launch(SweepEnvironment& env, juce::Component* content, const juce::String& title, bool resizable,
                           bool escapeCloses = true) {
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content);
    options.dialogTitle = title;
    options.componentToCentreAround = &env.anchor();
    options.useNativeTitleBar = true;
    options.resizable = resizable;
    options.escapeKeyTriggersCloseButton = escapeCloses;
    return PopupMotion::launchDialog(options);
}

juce::DialogWindow* openExport(SweepEnvironment& env, bool stems) {
    auto* dialog = new ExportAudioDialog(16.0, true, 0.0, 8.0, 120.0, true,
                                         juce::File::getSpecialLocation(juce::File::tempDirectory), "Take", stems);
    return launch(env, dialog, dialog->getWindowTitle(), false, false);
}

juce::DialogWindow* openMacroPorts(SweepEnvironment& env) {
    return launch(env, new MacroAutoPortPromptDialog(3), "Macro Ports", false, false);
}

juce::DialogWindow* openMacroConfig(SweepEnvironment& env) {
    using Row = MacroPortConfigDialog::PortRow;
    Row in;
    in.nodeUuid = "in";
    in.isInput = true;
    in.name = "Pitch In";
    Row out;
    out.nodeUuid = "out";
    out.name = "Audio Out";
    return launch(env, new MacroPortConfigDialog("My Macro", {in, out}), "Configure I/O", false, false);
}

juce::DialogWindow* openSettings(SweepEnvironment& env) {
    auto deps = std::make_shared<SettingsDeps>();
    env.keepAlive(deps);
    auto* settings = new SettingsWindow(deps->deviceManager, deps->appProperties, *deps->aiService, *deps->aiChat,
                                        deps->shortcutManager, deps->themeManager, nullptr);
    settings->setSize(SettingsWindow::kDefaultWidth, SettingsWindow::kDefaultHeight);
    auto* window = launch(env, settings, "Settings", true);
    if (window != nullptr)
        window->setResizeLimits(SettingsWindow::kMinWidth, SettingsWindow::kMinHeight, 4096, 4096);
    return window;
}

juce::DialogWindow* openSignIn(SweepEnvironment& env) {
    auto service = std::make_shared<synth::AccountService>(
        "http://mock-host:8787",
        [](const juce::String&, const juce::String&, const juce::StringPairArray&, const juce::String&, int,
           const std::atomic<bool>&) { return synth::AuthClient::HttpResult{}; },
        std::make_unique<synth::InMemoryTokenStore>());
    env.keepAlive(service);
    auto dialog = synth::launchSignInDialog(*service, env.anchor());
    return dialog != nullptr ? dynamic_cast<juce::DialogWindow*>(dialog->getTopLevelComponent()) : nullptr;
}

juce::DialogWindow* openShape(SweepEnvironment& env) {
    auto rig = std::make_shared<ShapeRig>();
    env.keepAlive(rig);
    return openShapeEditorWindow(rig->adapter, 0, &env.appAnchor());
}

juce::DialogWindow* openSamplerMap(SweepEnvironment& env) {
    auto sampler = std::make_shared<SamplerModule>();
    env.keepAlive(sampler);
    auto view =
        std::make_unique<SamplerMapView>(*sampler, [](const std::vector<synth::sampler::RegionSpec>&) { return true; });
    return openSamplerMapWindow(std::move(view), nullptr);
}

juce::DialogWindow* openEq(SweepEnvironment& env) {
    auto eq = std::make_shared<ParametricEQModule>();
    env.keepAlive(eq);
    return launch(env, new EQWindow(*eq), "EQ", true);
}

juce::DialogWindow* openRenderProgress(SweepEnvironment& env) {
    return launch(env, new RenderProgressDialog([] { return 0.4; }), "Rendering to audio", false, false);
}

// The panel a detached window hosts: a few text controls on the window's own background.
struct SamplePanel : juce::Component {
    SamplePanel() {
        title.setText("Mixer", juce::dontSendNotification);
        button.setButtonText("Dock back");
        toggle.setButtonText("Show meters");
        for (auto* c : std::initializer_list<juce::Component*>{&title, &button, &toggle})
            addAndMakeVisible(*c);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(12);
        title.setBounds(area.removeFromTop(24));
        button.setBounds(area.removeFromTop(28).removeFromLeft(120));
        toggle.setBounds(area.removeFromTop(28));
    }
    juce::Label title;
    juce::TextButton button;
    juce::ToggleButton toggle;
};

struct DetachedRig {
    DetachedRig(SweepEnvironment& env) {
        juce::PropertiesFile::Options options;
        options.applicationName = "ThemeSweepTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);
        window = std::make_unique<DetachedPanelWindow>(panel, "Test Panel", "themeSweepWindowBounds", &appProperties,
                                                       &env.lookAndFeel(), &shortcutManager);
        window->setBounds(0, 0, 640, 420);
    }
    ~DetachedRig() {
        window.reset();
        if (auto* userSettings = appProperties.getUserSettings())
            userSettings->clear();
    }
    SamplePanel panel;
    juce::ApplicationProperties appProperties;
    ShortcutManager shortcutManager;
    std::unique_ptr<DetachedPanelWindow> window;
};

struct CardRigImpl : CardRig {
    CardRigImpl(std::unique_ptr<juce::AudioProcessor> p)
        : editor(engine)
        , processor(std::move(p))
        , moduleCard(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor) {}
    juce::Component& card() override { return moduleCard; }
    AudioEngine engine;
    GraphEditor editor;
    std::unique_ptr<juce::AudioProcessor> processor;
    ModuleComponent moduleCard;
};

} // namespace

const std::vector<DialogSurface>& dialogSurfaces() {
    static const std::vector<DialogSurface> surfaces = {
        {"Shape editor", "Source/UI/ModuleViews/ShapeEditor/ShapeEditorWindow.cpp", true, openShape, ""},
        {"Sampler map", "Source/UI/Graph/ModuleComponent/SamplerChrome/SamplerMapWindow.cpp", true, openSamplerMap, ""},
        {"EQ window", "Source/UI/Graph/ModuleComponent/ModuleComponentEQCard.cpp", true, openEq, ""},
        {"Sign in", "Source/UI/Assistant/SignInDialog.cpp", false, openSignIn, ""},
        {"Macro ports prompt", "Source/UI/Graph/GraphEditor/GraphEditorMacroPrompts.cpp", false, openMacroPorts, ""},
        {"Macro configure I/O", "Source/UI/Graph/GraphEditor/GraphEditorMacroPrompts.cpp", false, openMacroConfig, ""},
        {"Settings", "Source/MainComponent/MainComponentSetupToolbar.cpp", false, openSettings, ""},
        {"Render progress", "Source/MainComponent/MainComponentRenderToAudio.cpp", false, openRenderProgress, ""},
        {"Export audio", "Source/MainComponent/MainComponentCallbacks.cpp", false,
         [](SweepEnvironment& env) { return openExport(env, false); }, ""},
        {"Export stems", "Source/MainComponent/MainComponentCallbacks.cpp", false,
         [](SweepEnvironment& env) { return openExport(env, true); }, ""},
    };
    return surfaces;
}

const std::vector<WindowSurface>& windowSurfaces() {
    static const std::vector<WindowSurface> surfaces = {
        {"Main window", "Source/Main.cpp", false, "covered by the main-shell surface"},
        {"Detached panel", "Source/UI/Layout/DetachablePanelHost/DetachedPanelWindow.cpp", true, ""},
        {"Hosted plugin editor", "Source/Plugin/Hosting/HostedPluginEditorWindow.cpp", false,
         "shows a third-party plugin's own editor"},
    };
    return surfaces;
}

juce::DocumentWindow& buildDetachedPanelWindow(SweepEnvironment& env) {
    auto rig = std::make_shared<DetachedRig>(env);
    env.keepAlive(rig);
    return *rig->window;
}

std::unique_ptr<juce::AlertWindow> buildAlertWindow(SweepEnvironment& env) {
    std::unique_ptr<juce::AlertWindow> alert(env.lookAndFeel().createAlertWindow(
        "Unsaved changes", "Do you want to save the changes to this project before closing it?", "Save", "Cancel", {},
        juce::MessageBoxIconType::QuestionIcon, 2, nullptr));
    return alert;
}

std::vector<std::string> moduleTypeNames() {
    std::vector<std::string> names;
    for (const auto& entry : synth::detail::moduleFactory())
        names.push_back(entry.first.toStdString());
    std::sort(names.begin(), names.end());
    return names;
}

std::unique_ptr<CardRig> buildCard(const std::string& type, std::string& whyNot) {
    const auto it = synth::detail::moduleFactory().find(juce::String(type));
    if (it == synth::detail::moduleFactory().end()) {
        whyNot = "not in the module factory";
        return nullptr;
    }
    return std::make_unique<CardRigImpl>(it->second());
}

} // namespace synth::test::theme_sweep
