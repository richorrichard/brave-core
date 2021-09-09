/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_EIP1559_TRANSACTION_H_
#define BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_EIP1559_TRANSACTION_H_

#include <string>
#include <vector>

#include "brave/components/brave_wallet/browser/eip2930_transaction.h"

namespace brave_wallet {

class Eip1559Transaction : public Eip2930Transaction {
 public:
  Eip1559Transaction();
  Eip1559Transaction(const Eip1559Transaction&);
  ~Eip1559Transaction() override;
  bool operator==(const Eip1559Transaction&) const;

  static absl::optional<Eip1559Transaction> FromTxData(
      const mojom::TxData1559Ptr& tx_data);
  static absl::optional<Eip1559Transaction> FromValue(const base::Value& value);

  uint256_t max_priority_fee_per_gas() const {
    return max_priority_fee_per_gas_;
  }
  uint256_t max_fee_per_gas() const { return max_fee_per_gas_; }

  // keccak256(0x02 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas,
  // gasLimit, destination, value, data, access_list]))
  std::vector<uint8_t> GetMessageToSign(uint256_t chain_id = 0) const override;

  // 0x02 || rlp([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit,
  // destination, value, data, accessList, signatureYParity, signatureR,
  // signatureS])
  std::string GetSignedTransaction() const override;

  base::Value ToValue() const override;

  uint256_t GetUpfrontCost(uint256_t block_base_fee = 0) const override;

 protected:
  Eip1559Transaction(uint256_t nonce,
                     uint256_t gas_price,
                     uint256_t gas_limit,
                     const EthAddress& to,
                     uint256_t value,
                     const std::vector<uint8_t>& data,
                     uint256_t chain_id,
                     uint256_t max_priority_fee_per_gas,
                     uint256_t max_fee_per_gas);

  uint256_t max_priority_fee_per_gas_;
  uint256_t max_fee_per_gas_;
};

}  // namespace brave_wallet

#endif  // BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_EIP1559_TRANSACTION_H_
