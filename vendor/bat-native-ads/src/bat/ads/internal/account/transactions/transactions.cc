/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ads/internal/account/transactions/transactions.h"

#include <string>

#include "base/check_op.h"
#include "base/guid.h"
#include "base/time/time.h"
#include "bat/ads/confirmation_type.h"
#include "bat/ads/internal/database/tables/transactions_database_table.h"
#include "bat/ads/transaction_info.h"

namespace ads {
namespace transactions {

TransactionInfo Add(const double value,
                    const ConfirmationType& confirmation_type,
                    AddTransactionCallback callback) {
  DCHECK_NE(ConfirmationType::kUndefined, confirmation_type.value());

  TransactionInfo transaction;
  transaction.id = base::GenerateGUID();
  transaction.created_at = base::Time::Now().ToDoubleT();
  transaction.value = value;
  transaction.confirmation_type = confirmation_type;

  database::table::Transactions database_table;
  database_table.Save({transaction}, [=](const bool success) {
    if (!success) {
      callback(/* success */ false, {});
      return;
    }

    callback(/* success */ true, transaction);
  });

  return transaction;
}

void GetForDateRange(const base::Time& from_time,
                     const base::Time& to_time,
                     GetTransactionsCallback callback) {
  database::table::Transactions database_table;
  database_table.GetForDateRange(
      from_time, to_time,
      [=](const bool success, const TransactionList& transactions) {
        if (!success) {
          callback(/* success */ false, {});
          return;
        }

        callback(/* success */ true, transactions);
      });
}

void RemoveAll(RemoveAllTransactionsCallback callback) {
  database::table::Transactions database_table;
  database_table.Delete([callback](const bool success) {
    if (!success) {
      callback(/* success */ false);
      return;
    }

    callback(/* success */ true);
  });
}

}  // namespace transactions
}  // namespace ads
