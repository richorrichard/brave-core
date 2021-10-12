/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
/* global window */

const { EventEmitter } = require('events')

import {
  TrezorDerivationPaths, TrezorBridgeAccountsPayload
} from '../../components/desktop/popup-modals/add-account-modal/hardware-wallet-connect/types'

import {
  kTrezorHardwareVendor
} from '../../constants/types'
const ethUtil = require('ethereumjs-util')

const kTrezorBridgeFrameId = 'trezor-untrusted-frame'
const kTrezorUnlockCommand = 'trezor-unlock'
const kTrezorGetAccountsCommand = 'trezor-get-accounts'
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
      const unlockEvent = { command: kTrezorUnlockCommand, id: new Date().getTime() }
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
      const kGetAccountEvent = { command: kTrezorGetAccountsCommand, id: new Date().getTime(), paths: requestedPaths }
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
        if (event.data.payload) {
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

  signTransaction = async (path: string, rawTxHex: string) => {
    if (!this.isUnlocked() && !(await this.unlock())) {
      return new Error('Unable to unlock device, try to reconnect')
    }
    return this.bridge_.signTransaction(path, rawTxHex)
  }

  _createBridge = async (): Promise<HTMLIFrameElement> => {
    return new Promise(async (resolve, reject) => {
      let element = document.createElement('iframe')
      element.src = kTrezorBridgeUrl
      element.id = kTrezorBridgeFrameId
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
