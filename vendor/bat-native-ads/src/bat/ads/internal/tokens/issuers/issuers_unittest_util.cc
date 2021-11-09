/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ads/internal/tokens/issuers/issuers_unittest_util.h"

#include "bat/ads/internal/tokens/issuers/issuer_info_aliases.h"
#include "bat/ads/internal/tokens/issuers/issuer_types.h"
#include "bat/ads/internal/tokens/issuers/issuers_info.h"
#include "bat/ads/internal/tokens/issuers/issuers_util.h"

namespace ads {

namespace {

IssuerInfo BuildIssuer(const IssuerType type,
                       const std::vector<std::string>& public_keys) {
  IssuerInfo issuer;
  issuer.type = type;
  issuer.public_keys = public_keys;

  return issuer;
}

}  // namespace

IssuersInfo BuildIssuers(
    const int ping,
    const std::vector<std::string>& confirmations_public_keys,
    const std::vector<std::string>& payments_public_keys) {
  IssuersInfo issuers;

  issuers.ping = ping;

  if (!confirmations_public_keys.empty()) {
    const IssuerInfo& confirmations_issuer =
        BuildIssuer(IssuerType::kConfirmations, confirmations_public_keys);
    issuers.issuers.push_back(confirmations_issuer);
  }

  if (!payments_public_keys.empty()) {
    const IssuerInfo& payments_issuer =
        BuildIssuer(IssuerType::kPayments, payments_public_keys);
    issuers.issuers.push_back(payments_issuer);
  }

  return issuers;
}

void BuildAndSetIssuers() {
  const IssuersInfo& issuers =
      BuildIssuers(7200000,
                   {"JsvJluEN35bJBgJWTdW/8dAgPrrTM1I1pXga+o7cllo=",
                    "crDVI1R6xHQZ4D9cQu4muVM5MaaM1QcOT4It8Y/CYlw="},
                   {"JiwFR2EU/Adf1lgox+xqOVPuc6a/rxdy/LguFG5eaXg=",
                    "XgxwreIbLMu0IIFVk4TKEce6RduNVXngDmU3uixly0M=",
                    "bPE1QE65mkIgytffeu7STOfly+x10BXCGuk5pVlOHQU="});

  SetIssuers(issuers);
}

}  // namespace ads
