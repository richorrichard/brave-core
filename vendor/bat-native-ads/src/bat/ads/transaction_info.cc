/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ads/transaction_info.h"

#include "base/check.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "bat/ads/internal/number_util.h"

namespace ads {

TransactionInfo::TransactionInfo() = default;

TransactionInfo::TransactionInfo(const TransactionInfo& info) = default;

TransactionInfo::~TransactionInfo() = default;

bool TransactionInfo::operator==(const TransactionInfo& rhs) const {
  return id == rhs.id && DoubleEquals(created_at, rhs.created_at) &&
         DoubleEquals(value, rhs.value) &&
         confirmation_type == rhs.confirmation_type &&
         DoubleEquals(redeemed_at, rhs.redeemed_at);
}

bool TransactionInfo::operator!=(const TransactionInfo& rhs) const {
  return !(*this == rhs);
}

void TransactionInfo::ToDictionary(base::Value* dictionary) const {
  DCHECK(dictionary);

  dictionary->SetKey("id", base::Value(id));

  dictionary->SetKey("timestamp_in_seconds",
                     base::Value(base::NumberToString(created_at)));

  dictionary->SetKey("estimated_redemption_value", base::Value(value));

  dictionary->SetKey("confirmation_type",
                     base::Value(std::string(confirmation_type)));

  dictionary->SetKey("redeemed_at",
                     base::Value(base::NumberToString(redeemed_at)));
}

void TransactionInfo::FromDictionary(base::DictionaryValue* dictionary) {
  DCHECK(dictionary);

  // Id
  const std::string* id_value = dictionary->FindStringKey("id");
  if (id_value) {
    id = *id_value;
  }

  // Created at
  const std::string* created_at_value =
      dictionary->FindStringKey("timestamp_in_seconds");
  if (created_at_value) {
    base::StringToDouble(*created_at_value, &created_at);
  }

  // Estimated redemption value
  value = dictionary->FindDoubleKey("value").value_or(0.0);

  // Confirmation type
  const std::string* confirmation_type_value =
      dictionary->FindStringKey("confirmation_type");
  if (confirmation_type_value) {
    confirmation_type = ConfirmationType(*confirmation_type_value);
  }

  // Redeemed at
  const std::string* redeemed_at_value =
      dictionary->FindStringKey("redeemed_at");
  if (redeemed_at_value) {
    base::StringToDouble(*redeemed_at_value, &redeemed_at);
  }
}

}  // namespace ads
