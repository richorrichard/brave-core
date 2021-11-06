/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import LedgerBridgeKeyring from '../../common/ledgerjs/eth_ledger_bridge_keyring'
import TrezorBridgeKeyring from '../../common/trezor/trezor_bridge_keyring'
import {
  kLedgerHardwareVendor, kTrezorHardwareVendor
} from '../../constants/types'
import { KeyringControllerRemote } from 'gen/brave/components/brave_wallet/common/brave_wallet.mojom.m.js'

export type HardwareKeyring = LedgerBridgeKeyring | TrezorBridgeKeyring

// Lazy instances for keyrings
let ledgerHardwareKeyring: LedgerBridgeKeyring
let trezorHardwareKeyring: TrezorBridgeKeyring
let keyringController: KeyringControllerRemote

export function getKeyringByType (type: string | undefined): HardwareKeyring | KeyringControllerRemote {
  if (type === kLedgerHardwareVendor) {
    if (!ledgerHardwareKeyring) {
      ledgerHardwareKeyring = new LedgerBridgeKeyring()
    }
    return ledgerHardwareKeyring
  } else if (type === kTrezorHardwareVendor) {
    if (!trezorHardwareKeyring) {
      trezorHardwareKeyring = new TrezorBridgeKeyring()
    }
    return trezorHardwareKeyring
  }
  if (!keyringController) {
    /** @type {!braveWallet.mojom.KeyringControllerRemote} */
    keyringController = new KeyringControllerRemote()
  }
  return keyringController
}
