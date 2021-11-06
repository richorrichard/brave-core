/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { HardwareWalletAccount } from 'components/brave_wallet_ui/components/desktop/popup-modals/add-account-modal/hardware-wallet-connect/types'
import { SignHardwareMessageOperationResult, SignHardwareTransactionOperationResult, TransactionInfo } from 'components/brave_wallet_ui/constants/types'

export abstract class HardwareKeyring {
  abstract getAccounts (from: number, to: number, scheme: string): Promise<HardwareWalletAccount[] | Error>
  abstract type (): string
  abstract isUnlocked (): Boolean
  abstract unlock (): Promise<Boolean | Error>
}

export abstract class TrezorKeyring extends HardwareKeyring {
  abstract signTransaction (path: string, txInfo: TransactionInfo, chainId: string): Promise<SignHardwareTransactionOperationResult>
  abstract signPersonalMessage (path: string, message: string): Promise<SignHardwareMessageOperationResult>
}

export abstract class LedgerKeyring extends HardwareKeyring {
  abstract signPersonalMessage (path: string, address: string, message: string): Promise<SignHardwareMessageOperationResult>
  abstract signTransaction (path: string, rawTxHex: string): Promise<SignHardwareTransactionOperationResult>
}
