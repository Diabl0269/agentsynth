#pragma once

#include "Branding.h"
#include <juce_core/juce_core.h>

namespace synth {

/**
 * @brief The Polar checkout link for "Upgrade to Pro", with the signed-in account's email
 *        prefilled (`customer_email`, a documented Polar checkout-link query parameter).
 *
 * The server activates Pro by matching the Polar customer's email to the signed-in account's
 * email, so a buyer who types a different address at checkout would stay on Free. Prefilling it
 * makes the matching address the default. An empty `accountEmail` (signed out, or the account's
 * email not fetched; AccountSnapshot::email may be empty) yields the bare link.
 */
inline juce::URL buildUpgradeUrl(const juce::String& accountEmail) {
    juce::URL url{juce::String(synth::branding::kUpgradeUrl)};
    if (accountEmail.trim().isNotEmpty())
        url = url.withParameter("customer_email", accountEmail.trim());
    return url;
}

} // namespace synth
