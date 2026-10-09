#pragma once

#include "AccountRequests.h"
#include "LeavingSurveyPanel.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth {

/**
 * @class AccountFlowPanel
 * @brief The popover content behind the Account tab's "Manage subscription" and "Delete account"
 *        buttons: a small stack of pages in one call-out (choices, the leaving question, a
 *        confirmation, progress, a failure), each fading in over the last.
 *
 * Esc closes the whole panel and the opener gets keyboard focus back, however the panel closes.
 */
class AccountFlowPanel : public juce::Component {
public:
    enum class Flow { manage, deleteAccount };
    enum class Page { manageChoice, cancelSurvey, deleteConfirm, deleteSurvey, deleting, deleteFailed };

    struct Services {
        AccountService* account = nullptr;
        const AccountRequests* requests = nullptr;
        std::function<void(const juce::URL&)> openUrl;
        std::function<void()> onAccountDeleted; // runs once the account is gone and the app is signed out
        juce::Component::SafePointer<juce::Component> opener;
    };

    static constexpr int kWidth = 360;

    AccountFlowPanel(Flow flow, Services services);
    ~AccountFlowPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    Page getPage() const { return page_; }
    bool wasCloseRequested() const { return closeRequested_; }

    // Testing hooks
    juce::String getBodyTextForTest() const { return body_.getText(); }
    juce::TextButton* findButtonForTest(const juce::String& text) const;
    LeavingSurveyPanel* getSurveyForTest() const { return survey_.get(); }

private:
    enum class Look { normal, destructive };
    struct ButtonSpec {
        juce::String text;
        juce::String tooltip;
        Look look = Look::normal;
        std::function<void()> onClick;
    };

    void showPage(Page page);
    void setChoices(const juce::String& title, const juce::String& body, std::vector<ButtonSpec> specs, bool stacked);
    void setSurvey(LeavingSurveyPanel::Kind kind);
    void fadeIn();
    void retirePage();
    void requestClose();
    void openPortalAndClose();
    void performDelete();
    void showDeleteFailure(const AuthClient::DeleteAccountResult& result);
    void lookAndFeelChanged() override;

    Services services_;
    Page page_ = Page::manageChoice;
    bool closeRequested_ = false;
    bool stacked_ = false;
    int contentHeight_ = 0;

    juce::Component pageHost_;
    juce::Label title_;
    juce::Label body_;
    std::vector<std::unique_ptr<juce::TextButton>> buttons_;
    std::vector<Look> looks_;
    std::unique_ptr<LeavingSurveyPanel> survey_;
    // A page's controls are still running their own click handler when the next page replaces them.
    std::vector<std::unique_ptr<juce::Component>> retired_;

    juce::VBlankAnimatorUpdater vblank_{this};
    synth::ui::AnimationDriver fade_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccountFlowPanel)
};

} // namespace synth
