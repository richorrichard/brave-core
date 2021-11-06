/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
/* global window */

import { publicToAddress, toChecksumAddress, bufferToHex } from 'ethereumjs-util'
import { Transaction } from 'ethereumjs-tx'
import {
  TrezorDerivationPaths, TrezorBridgeAccountsPayload, HardwareWalletAccount
} from '../../components/desktop/popup-modals/add-account-modal/hardware-wallet-connect/types'
import {
  kTrezorHardwareVendor,
  SignHardwareMessageOperationResult,
  SignHardwareTransactionOperationResult,
  TransactionInfo
} from '../../constants/types'
import {
  TrezorCommand,
  UnlockResponse,
  GetAccountsResponsePayload,
  TrezorAccount,
  SignTransactionCommandPayload,
  TrezorFrameCommand,
  SignMessageCommandPayload,
  SignMessageResponsePayload,
  SignTransactionResponsePayload
} from '../../common/trezor/trezor-messages'
import { sendTrezorCommand } from '../../common/trezor/trezor-bridge-transport'
import { getLocale } from '../../../common/locale'
import { hardwareDeviceIdFromAddress } from '../hardwareDeviceIdFromAddress'
import { TrezorKeyring } from '../api/hardwareKeyring'

export default class TrezorBridgeKeyring extends TrezorKeyring {
  protected deviceId: string
  constructor () {
    super()
    this.unlocked = false
  }

  protected unlocked: Boolean

  type = (): string => {
    return kTrezorHardwareVendor
  }

  getAccounts = async (from: number, to: number, scheme: string): Promise<HardwareWalletAccount[] | Error> => {
    if (from < 0) {
      from = 0
    }
    if (!this.isUnlocked() && !(await this.unlock())) {
      return new Error(getLocale('braveWalletUnlockError'))
    }
    const paths = []

    const addZeroPath = (from > 0 || to < 0)
    if (addZeroPath) {
      // Add zero address to calculate device id.
      paths.push(this.getPathForIndex(0, TrezorDerivationPaths.Default))
    }
    for (let i = from; i <= to; i++) {
      paths.push(this.getPathForIndex(i, scheme))
    }
    const accounts = await this.getAccountsFromDevice(paths, addZeroPath)
    if (!accounts.success) {
      throw Error(accounts.error)
    }
    return accounts.accounts
  }

  signTransaction = async (path: string, txInfo: TransactionInfo, chainId: string): Promise<SignHardwareTransactionOperationResult> => {
    if (!this.isUnlocked() && !(await this.unlock())) {
      return { success: false, error: getLocale('braveWalletUnlockError') }
    }
    const data = await this.sendTrezorCommand<SignTransactionResponsePayload>({
      command: TrezorCommand.SignTransaction,
      // @ts-ignore
      id: crypto.randomUUID(),
      payload: this.prepareTransactionPayload(path, txInfo, chainId),
      origin: window.origin
    })
    if (!data || !data.payload) {
      return { success: false, error: getLocale('braveWalletProcessTransactionError') }
    }
    if (!data.payload.success) {
      return { success: false, error: data.payload.payload.error, code: data.payload.payload.code }
    }
    return { success: true, payload: data.payload.payload }
  }

  signPersonalMessage = async (path: string, message: string): Promise<SignHardwareMessageOperationResult> => {
    if (!this.isUnlocked() && !(await this.unlock())) {
      return { success: false, error: getLocale('braveWalletUnlockError') }
    }
    const data = await this.sendTrezorCommand<SignMessageResponsePayload>({
      command: TrezorCommand.SignMessage,
      // @ts-ignore
      id: crypto.randomUUID(),
      payload: this.prepareSignMessagePayload(path, message),
      origin: window.origin
    })
    if (!data) {
      return { success: false, error: getLocale('braveWalletProcessMessageError') }
    }
    if (!data.payload.success) {
      const unsuccess = data.payload
      return { success: false, error: unsuccess.payload.error, code: unsuccess.payload.code }
    }
    return { success: true, payload: data.payload.payload.signature }
  }

  isUnlocked = () => {
    return this.unlocked
  }

  unlock = async (): Promise<Boolean | Error> => {
    const data = await this.sendTrezorCommand<UnlockResponse>({
      // @ts-ignore
      id: crypto.randomUUID(),
      origin: window.origin,
      command: TrezorCommand.Unlock })
    if (!data) {
      return new Error(getLocale('braveWalletUnlockError'))
    }
    this.unlocked = data.result
    if (data.result) {
      return true
    }
    return false
  }

  private async sendTrezorCommand<T> (command: TrezorFrameCommand): Promise<T | false> {
    return sendTrezorCommand<T>(command)
  }

  private getHashFromAddress = async (address: string) => {
    return hardwareDeviceIdFromAddress(address)
  }

  private getDeviceIdFromAccountsList = async (accountsList: TrezorAccount[]) => {
    const zeroPath = this.getPathForIndex(0, TrezorDerivationPaths.Default)
    for (const value of accountsList) {
      if (value.serializedPath !== zeroPath) {
        continue
      }
      const address = this.publicKeyToAddress(value.publicKey)
      return this.getHashFromAddress(address)
    }
    return ''
  }

  private normalize (buf: any) {
    return bufferToHex(buf).toString()
  }
  private prepareSignMessagePayload = (path: string, message: string): SignMessageCommandPayload => {
    return { path: path, message: message }
  }
  private prepareTransactionPayload = (path: string, txInfo: TransactionInfo, chainId: string): SignTransactionCommandPayload => {
    const txParams = {
      nonce: txInfo.txData.baseData.nonce,
      gasPrice: txInfo.txData.baseData.gasPrice,
      gasLimit: txInfo.txData.baseData.gasLimit,
      to: txInfo.txData.baseData.to,
      value: txInfo.txData.baseData.value,
      data: Buffer.from(txInfo.txData.baseData.data)
    }
    const tx = new Transaction(txParams)
    return {
      path: path,
      transaction: {
        to: this.normalize(tx.to),
        value: this.normalize(tx.value),
        data: this.normalize(tx.data).replace('0x', ''),
        chainId: parseInt(chainId, 16),
        nonce: this.normalize(tx.nonce),
        gasLimit: this.normalize(tx.gasLimit),
        gasPrice: this.normalize(tx.gasPrice)
      }
    }
  }

  private publicKeyToAddress = (key: string) => {
    const buffer = Buffer.from(key, 'hex')
    const address = publicToAddress(buffer, true).toString('hex')
    return toChecksumAddress(`0x${address}`)
  }

  private getAccountsFromDevice = async (paths: string[], skipZeroPath: Boolean): Promise<TrezorBridgeAccountsPayload> => {
    const requestedPaths = []
    for (const path of paths) {
      requestedPaths.push({ path: path })
    }
    const data = await this.sendTrezorCommand<GetAccountsResponsePayload>({
      command: TrezorCommand.GetAccounts,
      // @ts-ignore
      id: crypto.randomUUID(),
      paths: requestedPaths,
      origin: window.origin })
    if (!data || !data.payload.success) {
      return { success: false, error: getLocale('braveWalletCreateBridgeError'), accounts: [] }
    }

    let accounts = []
    const accountsList = data.payload.payload as TrezorAccount[]
    this.deviceId = await this.getDeviceIdFromAccountsList(accountsList)
    const zeroPath = this.getPathForIndex(0, TrezorDerivationPaths.Default)
    for (const value of accountsList) {
      // If requested addresses do not have zero indexed adress we add it
      // intentionally to calculate device id and should not add it to
      // returned accounts
      if (skipZeroPath && (value.serializedPath === zeroPath)) {
        continue
      }
      accounts.push({
        address: this.publicKeyToAddress(value.publicKey),
        derivationPath: value.serializedPath,
        name: this.type(),
        hardwareVendor: this.type(),
        deviceId: this.deviceId
      })
    }
    return { success: true, accounts: [...accounts] }
  }

  private getPathForIndex = (index: number, scheme: string) => {
    if (scheme === TrezorDerivationPaths.Default) {
      return `m/44'/60'/0'/${index}`
    } else {
      throw Error(getLocale('braveWalletDeviceUnknownScheme'))
    }
  }
}
