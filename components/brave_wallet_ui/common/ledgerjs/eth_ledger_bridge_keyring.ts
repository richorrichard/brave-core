/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
/* global window */

import {
  HardwareWalletAccount,
  LedgerDerivationPaths
} from '../../components/desktop/popup-modals/add-account-modal/hardware-wallet-connect/types'

import {
  kLedgerHardwareVendor, SignatureVRS,
  SignHardwareMessageOperationResult,
  SignHardwareTransactionOperationResult
} from '../../constants/types'

import Eth from '@ledgerhq/hw-app-eth'
import TransportWebHID from '@ledgerhq/hw-transport-webhid'
import { getLocale } from '../../../common/locale'
import { hardwareDeviceIdFromAddress } from '../hardwareDeviceIdFromAddress'
import { LedgerKeyring } from '../api/hardwareKeyring'

export default class LedgerBridgeKeyring extends LedgerKeyring {
  protected app: Eth
  protected deviceId: string

  constructor () {
    super()
  }

  type = (): string => {
    return kLedgerHardwareVendor
  }

  getAccounts = async (from: number, to: number, scheme: LedgerDerivationPaths): Promise<HardwareWalletAccount[] | Error> => {
    if (from < 0) {
      from = 0
    }
    if (!this.isUnlocked() && !(await this.unlock())) {
      return new Error(getLocale('braveWalletUnlockError'))
    }
    const accounts = []
    for (let i = from; i <= to; i++) {
      const path = this.getPathForIndex(i, scheme)
      const address = await this.app.getAddress(path)
      accounts.push({
        address: address.address,
        derivationPath: path,
        name: this.type(),
        hardwareVendor: this.type(),
        deviceId: this.deviceId
      })
    }
    return accounts
  }

  isUnlocked = (): Boolean => {
    return this.app !== undefined
  }

  unlock = async (): Promise<Boolean | Error> => {
    if (this.app) {
      return !this.app
    }
    try {
      this.app = new Eth(await TransportWebHID.create())
      if (this.app) {
        const zeroPath = this.getPathForIndex(0, LedgerDerivationPaths.LedgerLive)
        const address = await this.app.getAddress(zeroPath)
        this.deviceId = address?.address ? await hardwareDeviceIdFromAddress(address?.address) : ''
      }
    } catch (e) {
      return new Error(e.message)
    }
    return this.isUnlocked()
  }

  signTransaction = async (path: string, rawTxHex: string): Promise<SignHardwareTransactionOperationResult> => {
    if (!this.isUnlocked() && !(await this.unlock())) {
      return { success: false, error: getLocale('braveWalletUnlockError') }
    }
    const signed = await this.app.signTransaction(path, rawTxHex)
    return { success: true, payload: signed }
  }

  signPersonalMessage = async (path: string, address: string, message: string): Promise<SignHardwareMessageOperationResult> => {
    if (!this.isUnlocked() && !(await this.unlock())) {
      return { success: false, error: getLocale('braveWalletUnlockError') }
    }
    try {
      const data = await this.app.signPersonalMessage(path, message)
      const signature = this.createMessageSignature(data, message, address)
      if (!signature) {
        return { success: false, error: getLocale('braveWalletLedgerValidationError') }
      }
      return { success: true, payload: signature }
    } catch (e) {
      return { success: false, error: e.message }
    }
  }

  private createMessageSignature = (result: SignatureVRS, message: string, address: string) => {
    let v = (result.v - 27).toString()
    if (v.length < 2) {
      v = `0${v}`
    }
    const signature = `0x${result.r}${result.s}${v}`
    return signature
  }

  private getPathForIndex = (index: number, scheme: LedgerDerivationPaths): string => {
    if (scheme === LedgerDerivationPaths.LedgerLive) {
      return `m/44'/60'/${index}'/0/0`
    }
    return `m/44'/60'/${index}'/0`
  }
}
