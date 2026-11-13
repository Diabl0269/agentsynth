#include "AccountFlowPanel.h"
#include "AccountButtonStyle.h"
#include "Branding.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth {

namespace {
constexpr int kPad = 16;
constexpr int kTitleH = 26;
constexpr int kButtonH = 30;
constexpr int kGap = 8;

int wrappedHeight(const juce::String& text, const juce::Font& font, int width) {
    juce::AttributedString attributed(text);
    attributed.setFont(font);
    juce::TextLayout layout;
    layout.createLayout(attributed, (float)width);
    return (int)std::ceil(layout.getHeight()) + 4;
}
} // namespace

AccountFlowPanel::AccountFlowPanel(Flow flow, Services services)
    : services_(std::move(services)) {
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    addAndMakeVisible(pageHost_);
    pageHost_.addAndMakeVisible(title_);
    title_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    pageHost_.addAndMakeVisible(body_);
    body_.setJustificationType(juce::Justification::topLeft);
    body_.setMinimumHorizontalScale(1.0f);

    showPage(flow == Flow::manage ? Page::manageChoice : Page::deleteConfirm);
}

AccountFlowPanel::~AccountFlowPanel() {
    // The panel can go by Esc, a choice or a click away; the opener gets focus back in every case. The
    // hosting window hands modality back one turn later, so focus is asked for then.
    if (auto* opener = services_.opener.getComponent())
        juce::MessageManager::callAsync([safe = services_.opener] {
            if (auto* o = safe.getComponent())
                if (o->isShowing())
                    o->grabKeyboardFocus();
        });
}

void AccountFlowPanel::paint(juce::Graphics& g) { g.fillAll(findColour(juce::ResizableWindow::backgroundColourId)); }

void AccountFlowPanel::lookAndFeelChanged() {
    for (size_t i = 0; i < buttons_.size(); ++i)
        if (looks_[i] == Look::destructive)
            applyDestructiveLook(*buttons_[i]);
}

bool AccountFlowPanel::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        requestClose();
        return true;
    }
    return false;
}

void AccountFlowPanel::requestClose() {
    closeRequested_ = true;
    synth::ui::closeHostingWindow(*this);
}

juce::TextButton* AccountFlowPanel::findButtonForTest(const juce::String& text) const {
    for (auto& button : buttons_)
        if (button->getButtonText() == text)
            return button.get();
    return nullptr;
}

void AccountFlowPanel::showPage(Page page) {
    page_ = page;
    retirePage();
    title_.setVisible(true);
    body_.setVisible(true);

    const auto openPortal = [this] { openPortalAndClose(); };

    switch (page) {
    case Page::manageChoice:
        setChoices(
            "Manage subscription", "Choose what you would like to do.",
            {{"Change payment or see invoices", "Opens the billing portal in your browser", Look::normal, openPortal},
             {"Cancel my subscription", "Tell us why, then cancel in the billing portal", Look::normal,
              [this] { showPage(Page::cancelSurvey); }}},
            true);
        break;
    case Page::cancelSurvey:
        setSurvey(LeavingSurveyPanel::Kind::cancel);
        break;
    case Page::deleteConfirm:
        setChoices("Delete your account?",
                   "Your account, sign-ins on every device, conversation history and shared prompt samples are "
                   "deleted right away. Invoices stay with our payment provider.",
                   {{"Cancel", "Close without deleting anything (Esc)", Look::normal, [this] { requestClose(); }},
                    {"Delete account", "Continue to delete your account for good", Look::destructive,
                     [this] { showPage(Page::deleteSurvey); }}},
                   false);
        break;
    case Page::deleteSurvey:
        setSurvey(LeavingSurveyPanel::Kind::deleteAccount);
        break;
    case Page::deleting:
        setChoices("Deleting your account", "One moment...", {}, false);
        break;
    case Page::deleteFailed:
        break; // set by showDeleteFailure()
    }
    fadeIn();
}

void AccountFlowPanel::retirePage() {
    for (auto& button : buttons_) {
        pageHost_.removeChildComponent(button.get());
        retired_.push_back(std::move(button));
    }
    buttons_.clear();
    looks_.clear();
    if (survey_ != nullptr) {
        pageHost_.removeChildComponent(survey_.get());
        retired_.push_back(std::move(survey_));
    }
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<AccountFlowPanel>(this)] {
        if (auto* panel = safe.getComponent())
            panel->retired_.clear();
    });
}

void AccountFlowPanel::setChoices(const juce::String& title, const juce::String& body, std::vector<ButtonSpec> specs,
                                  bool stacked) {
    stacked_ = stacked;
    title_.setText(title, juce::dontSendNotification);
    body_.setText(body, juce::dontSendNotification);
    setTitle(title);

    for (auto& spec : specs) {
        auto button = std::make_unique<juce::TextButton>(spec.text);
        button->setTitle(spec.text);
        button->setTooltip(spec.tooltip);
        button->onClick = std::move(spec.onClick);
        pageHost_.addAndMakeVisible(*button);
        looks_.push_back(spec.look);
        buttons_.push_back(std::move(button));
    }
    lookAndFeelChanged();

    const int innerW = kWidth - 2 * kPad;
    const int bodyH = body.isEmpty() ? 0 : wrappedHeight(body, body_.getFont(), innerW);
    const int buttonsH = buttons_.empty() ? 0 : stacked ? (int)buttons_.size() * (kButtonH + kGap) : kButtonH + kGap;
    contentHeight_ = kPad + kTitleH + kGap + bodyH + kGap + buttonsH + kPad;
    setSize(kWidth, contentHeight_);
    resized();

    if (!buttons_.empty())
        buttons_.front()->grabKeyboardFocus();
}

void AccountFlowPanel::setSurvey(LeavingSurveyPanel::Kind kind) {
    title_.setVisible(false);
    body_.setVisible(false);
    survey_ = std::make_unique<LeavingSurveyPanel>(kind);
    setTitle("Why are you leaving?");
    pageHost_.addAndMakeVisible(*survey_);

    const bool isCancel = kind == LeavingSurveyPanel::Kind::cancel;
    const juce::String kindKey = LeavingSurveyPanel::kindKey(kind);

    survey_->onSkip = [this, isCancel] {
        if (isCancel)
            openPortalAndClose();
        else
            performDelete();
    };
    survey_->onContinue = [this, isCancel, kindKey](const LeavingSurveyPanel::Answer& answer) {
        auto post = [&](AccountRequests::SurveyDone done) {
            if (services_.account != nullptr && services_.requests != nullptr)
                services_.requests->submitExitSurvey(*services_.account, kindKey, answer.reasons, answer.comment,
                                                     std::move(done));
            else if (done)
                done(false);
        };
        if (isCancel) {
            post({}); // never held up by the survey: the portal opens now
            openPortalAndClose();
            return;
        }
        // The account's token stops working once it is deleted, so the answer goes first; the delete
        // follows whether or not it was accepted.
        showPage(Page::deleting);
        juce::Component::SafePointer<AccountFlowPanel> self(this);
        post([self](bool) {
            if (auto* panel = self.getComponent())
                panel->performDelete();
        });
    };

    contentHeight_ = LeavingSurveyPanel::kPreferredHeight;
    setSize(kWidth, contentHeight_);
    resized();
    survey_->grabKeyboardFocus();
}

void AccountFlowPanel::openPortalAndClose() {
    if (services_.openUrl)
        services_.openUrl(juce::URL(juce::String(branding::kBillingPortalUrl)));
    requestClose();
}

void AccountFlowPanel::performDelete() {
    if (services_.account == nullptr || services_.requests == nullptr)
        return;
    if (page_ != Page::deleting)
        showPage(Page::deleting);

    // Outlives the panel on purpose: a delete that finishes after the popover was dismissed must still
    // sign the app out.
    juce::Component::SafePointer<AccountFlowPanel> self(this);
    auto* account = services_.account;
    auto onDeleted = services_.onAccountDeleted;
    services_.requests->deleteAccount(*account, [self, account, onDeleted](const AuthClient::DeleteAccountResult& r) {
        if (r.ok) {
            account->signOut(); // clears the stored tokens, same as the Sign out button
            if (onDeleted)
                onDeleted();
            if (auto* panel = self.getComponent())
                panel->requestClose();
            return;
        }
        if (auto* panel = self.getComponent())
            panel->showDeleteFailure(r);
    });
}

void AccountFlowPanel::showDeleteFailure(const AuthClient::DeleteAccountResult& result) {
    page_ = Page::deleteFailed;
    retirePage();
    title_.setVisible(true);
    body_.setVisible(true);

    const ButtonSpec close{"Close", "Close this panel (Esc)", Look::normal, [this] { requestClose(); }};

    if (result.isSubscriptionActive()) {
        setChoices("Couldn't delete your account", "Cancel your subscription first, then delete your account.",
                   {{"Manage subscription",
                     "Opens the billing portal, where you can change payment details, see invoices or cancel",
                     Look::normal, [this] { showPage(Page::manageChoice); }},
                    close},
                   false);
    } else if (result.isUnauthorised()) {
        setChoices("Couldn't delete your account", "Your sign-in has expired. Sign in again, then try once more.",
                   {close}, false);
    } else {
        const juce::String message =
            result.errorMessage.isNotEmpty()
                ? result.errorMessage
                : juce::String("Couldn't reach the server. Check your connection and try again.");
        setChoices("Couldn't delete your account", message,
                   {{"Try again", "Try deleting your account again", Look::normal, [this] { performDelete(); }}, close},
                   false);
    }
    fadeIn();
}

void AccountFlowPanel::fadeIn() {
    pageHost_.setAlpha(1.0f);
    if (!isShowing() || synth::ui::animationsOff())
        return;

    pageHost_.setAlpha(0.0f);
    fade_.start(
        vblank_, synth::ui::motionMs(160.0, 80.0), synth::ui::easeOutCubic, [this](float t) { pageHost_.setAlpha(t); },
        [this] { pageHost_.setAlpha(1.0f); });
}

void AccountFlowPanel::resized() {
    pageHost_.setBounds(getLocalBounds());
    auto area = pageHost_.getLocalBounds();

    if (survey_ != nullptr) {
        survey_->setBounds(area);
        return;
    }

    area = area.reduced(kPad);
    title_.setBounds(area.removeFromTop(kTitleH));
    area.removeFromTop(kGap);
    const int bodyH = body_.getText().isEmpty() ? 0 : wrappedHeight(body_.getText(), body_.getFont(), area.getWidth());
    body_.setBounds(area.removeFromTop(bodyH));
    area.removeFromTop(kGap);

    if (stacked_) {
        for (auto& button : buttons_) {
            button->setBounds(area.removeFromTop(kButtonH));
            area.removeFromTop(kGap);
        }
        return;
    }
    auto row = area.removeFromTop(kButtonH);
    for (size_t i = buttons_.size(); i-- > 0;) {
        buttons_[i]->setBounds(
            row.removeFromRight(juce::jmax(90, 12 + (int)buttons_[i]->getButtonText().length() * 8)));
        row.removeFromRight(kGap);
    }
}

} // namespace synth
