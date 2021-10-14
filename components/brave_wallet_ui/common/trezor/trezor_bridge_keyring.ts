/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
/* global window */

const { EventEmitter } = require('events')
import { Transaction }  from 'ethereumjs-tx'
//const { TransactionFactory } = require('@ethereumjs/tx');
//const { Common} = require('@ethereumjs/common');
import {
  TrezorDerivationPaths, TrezorBridgeAccountsPayload
} from '../../components/desktop/popup-modals/add-account-modal/hardware-wallet-connect/types'

import {
  kTrezorHardwareVendor,
  TransactionInfo
} from '../../constants/types'
const ethUtil = require('ethereumjs-util')

const kTrezorBridgeFrameId = 'trezor-untrusted-frame'
const kTrezorUnlockCommand = 'trezor-unlock'
const kTrezorGetAccountsCommand = 'trezor-get-accounts'
const kTrezorSignTransactionCommand = 'trezor-sign-transaction'

const kTrezorBridgeUrl = 'chrome-untrusted://trezor-bridge'
export default class TrezorBridgeKeyring extends EventEmitter {
  constructor () {
    super()
  }

  type = () => {
    return kTrezorHardwareVendor
  }

  getAccounts = (from: number, to: number, scheme: string) => {
    return new Promise(async (resolve, reject) => {
      if (from < 0) {
        from = 0
      }
      try {
        if (!this.isUnlocked() && !(await this.unlock())) {
          return reject(new Error('Unable to unlock device, try to reconnect'))
        }
      } catch (e) {
        reject(e)
        return
      }
      this._getAccounts(from, to, scheme).then(resolve).catch(reject)
    })
  }

  isUnlocked = () => {
    return this.unlocked
  }

  unlock = () => {
    return new Promise(async (resolve, reject) => {
      let bridge = document.getElementById(kTrezorBridgeFrameId) as HTMLIFrameElement
      if (!bridge) {
        bridge = await this._createBridge()
      }
      const unlockEvent = { command: kTrezorUnlockCommand, id: new Date().getTime(), owner: window.origin }
      if (!bridge.contentWindow) {
        throw Error('internal error, unable to create iframe')
      }
      const unlockCallback = (event: any) => {
        if (event.origin !== kTrezorBridgeUrl || event.type !== 'message') {
          return
        }
        if (unlockEvent.id !== event.data.id) {
          return
        }
        if (event.data.result) {
          resolve(true)
        } else {
          reject(false)
        }
        window.removeEventListener('message', unlockCallback)
      }
      window.addEventListener('message', unlockCallback)
      bridge.contentWindow.postMessage(unlockEvent, kTrezorBridgeUrl)
    })
  }

  _getTrezorAccounts = async (paths: string[]): Promise<TrezorBridgeAccountsPayload> => {
    return new Promise(async (resolve, reject) => {
      let bridge = document.getElementById(kTrezorBridgeFrameId) as HTMLIFrameElement
      if (!bridge) {
        reject(Error('iframe is not available'))
        return
      }
      const requestedPaths = []
      for (const path of paths) {
        requestedPaths.push({ path: path })
      }
      const kGetAccountEvent = { command: kTrezorGetAccountsCommand, id: new Date().getTime(), paths: requestedPaths, owner: window.origin  }
      if (!bridge.contentWindow) {
        throw Error('internal error, iframe doesnt exists')
      }
      const getAccountsCallback = (event: any) => {
        if (event.origin !== kTrezorBridgeUrl || event.type !== 'message') {
          return
        }
        if (kGetAccountEvent.id !== event.data.id) {
          return
        }
        if (event.data.payload.success) {
          let accounts = []
          for (const value of event.data.payload.payload) {
            const buffer = Buffer.from(value.publicKey, 'hex')
            const address = ethUtil.publicToAddress(buffer, true).toString('hex')
            accounts.push({
              address: ethUtil.toChecksumAddress(`0x${address}`),
              derivationPath: value.serializedPath,
              name: this.type(),
              hardwareVendor: this.type(),
              deviceId: 'todo'
            })
          }
          resolve({ success: true, accounts: [...accounts] })
        } else {
          reject({ success: false, error: 'Unable to get accounts' })
        }
        window.removeEventListener('message', getAccountsCallback)
      }
      window.addEventListener('message', getAccountsCallback)
      bridge.contentWindow.postMessage(kGetAccountEvent, kTrezorBridgeUrl)
    })
  }
  _normalize(buf: any) {
    return ethUtil.bufferToHex(buf).toString();
  }
  _prepareTransactionPayload = (path: string, txInfo: TransactionInfo, chainId: string) => {
    const txParams = {
      nonce: txInfo.txData.baseData.nonce,
      gasPrice: txInfo.txData.baseData.gasPrice,
      gasLimit: txInfo.txData.baseData.gasLimit,
      to: txInfo.txData.baseData.to,
      value: txInfo.txData.baseData.value,
      data: Buffer.from(txInfo.txData.baseData.data)
    }
    const tx = new Transaction(txParams)
    console.log("chainId:", chainId)
    return {
      path: path,
      transaction: {
        to: this._normalize(tx.to),
        value: this._normalize(tx.value),
        data: this._normalize(tx.data).replace('0x', ''),
        chainId: parseInt(chainId, 16),
        nonce: this._normalize(tx.nonce),
        gasLimit: this._normalize(tx.gasLimit),
        gasPrice: this._normalize(tx.gasPrice)
      }
    }
    /*
    if (typeof tx.getChainId === 'function') {
      return txParams

    }

    const unsignedTx = TransactionFactory.fromTxData(txParams, { common: new Common({chain: chainId}) });
    console.log(unsignedTx)
    const unfrozenTx = TransactionFactory.fromTxData(tx.toJSON(), {
      common: tx.common,
      freeze: false,
    });
    unfrozenTx.v = new ethUtil.BN(
      ethUtil.addHexPrefix(tx.common.chainId()),
      'hex',
    );

   return nullptr
       */
  }
  signTransaction = async (path: string, txInfo: TransactionInfo, chainId: string) => {
    return new Promise(async (resolve, reject) => {

      const payload = this._prepareTransactionPayload(path, txInfo, chainId)
      console.log(payload)

      if (!this.isUnlocked() && !(await this.unlock())) {
        reject(Error('Unable to unlock device, try to reconnect'))
        return
      }

      let bridge = document.getElementById(kTrezorBridgeFrameId) as HTMLIFrameElement
      if (!bridge || !bridge.contentWindow) {
        reject(Error('iframe is not available'))
        return
      }

      const kSignTransactionEvent = {
          command: kTrezorSignTransactionCommand,
          id: new Date().getTime(),
          owner: window.origin,
          payload: payload
        }
        const signTransactionCallback = (event: any) => {
          if (event.origin !== kTrezorBridgeUrl || event.type !== 'message') {
            return
          }
          if (kSignTransactionEvent.id !== event.data.id) {
            return
          }
          resolve(event.data.payload.payload)
          window.removeEventListener('message', signTransactionCallback)
        }
        window.addEventListener('message', signTransactionCallback)
        bridge.contentWindow.postMessage(kSignTransactionEvent, kTrezorBridgeUrl)  
    })
  }

  _createBridge = async (): Promise<HTMLIFrameElement> => {
    return new Promise(async (resolve, reject) => {
      let element = document.createElement('iframe')
      element.id = kTrezorBridgeFrameId
      element.src = kTrezorBridgeUrl
      element.style.display = 'none'
      element.onload = () => {
        resolve(element)
      }
      document.body.appendChild(element)
    })
  }

  /* PRIVATE METHODS */
  _getPathForIndex = (index: number, scheme: string) => {
    if (scheme === TrezorDerivationPaths.Default) {
      return `m/44'/60'/0'/${index}`
    } else {
      throw Error(`Unknown scheme: ${scheme}`)
    }
  }

  _getAccounts = async (from: number, to: number, scheme: string) => {
    const paths = []
    for (let i = from; i <= to; i++) {
      paths.push(this._getPathForIndex(i, scheme))
    }

    const accounts = await this._getTrezorAccounts(paths)
    console.log(accounts)
    if (!accounts.success) {
      throw Error(accounts.error)
    }
    console.log(accounts.accounts)
    return accounts.accounts
  }
}
