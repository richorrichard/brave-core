/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
APIProxyControllers,
TransactionInfo,
GetNonceForHardwareTransactionReturnInfo,
GetTransactionMessageToSignReturnInfo,
SignatureVRS,
kLedgerHardwareVendor,
kTrezorHardwareVendor,
ProcessHardwareSignatureReturnInfo
} from '../../constants/types'
import {
signTrezorTransaction,
signLedgerTransaction
} from '../../common/async/lib'
import { getLocale } from '../../../common/locale'
import { EthereumSignedTx } from 'trezor-connect/lib/typescript/trezor/protobuf'
import { Success, Unsuccessful } from 'trezor-connect'
import { getMockedTransactionInfo } from '../constants/mocks'

const getMockedLedgerKeyring = (expectedPath: string, expectedData: string | TransactionInfo, signed?: SignatureVRS) => {
  return {
    type: () => {
      return kLedgerHardwareVendor
    },
    signTransaction: async (path: string, data: string): Promise<SignatureVRS | undefined> => {
      expect(path).toStrictEqual(expectedPath)
      expect(data).toStrictEqual(expectedData)
      return Promise.resolve(signed)
    },
    signed: () => {
      if (!signed) {
        return
      }
      const { v, r, s } = signed
      return {
        v: '0x' + v,
        r: r,
        s: s
      }
    }
  }
}

const getMockedTrezorKeyring = (expectedDevicePath: string, expectedData: string | TransactionInfo, signed?: Success<EthereumSignedTx> | Unsuccessful) => {
  return {
    type: () => {
      return kTrezorHardwareVendor
    },
    signTransaction: async (path: string, data: string): Promise<Success<EthereumSignedTx> | Unsuccessful | undefined> => {
      expect(path).toStrictEqual(expectedDevicePath)
      expect(data).toStrictEqual(expectedData)
      return Promise.resolve(signed)
    },
    signed: () => {
      if (!signed) {
        return
      }
      const { v, r, s } = signed.payload as EthereumSignedTx
      return {
        v: v,
        r: r,
        s: s
      }
    }
  }
}

const getMockedProxyControllers = (expectedId: string,
                                   nonce?: GetNonceForHardwareTransactionReturnInfo,
                                   messageToSign?: GetTransactionMessageToSignReturnInfo | undefined,
                                   keyring?: any,
                                   hardwareSignature?: ProcessHardwareSignatureReturnInfo) => {
  return {
    ethJsonRpcController: {
      getChainId: async () => {
        return '0x123'
      }
    },
    ethTxController: {
      getNonceForHardwareTransaction: (id: string): GetNonceForHardwareTransactionReturnInfo | undefined => {
        expect(id).toStrictEqual(expectedId)
        return nonce
      },
      getTransactionMessageToSign: (id: string): GetTransactionMessageToSignReturnInfo | undefined => {
        expect(id).toStrictEqual(expectedId)
        return messageToSign
      },
      processHardwareSignature: (id: string, v: string, r: string, s: string): ProcessHardwareSignatureReturnInfo | undefined => {
        expect(id).toStrictEqual(expectedId)
        expect(v.startsWith('0x')).toStrictEqual(true)
        expect(r.startsWith('0x')).toStrictEqual(true)
        expect(s.startsWith('0x')).toStrictEqual(true)
        return hardwareSignature
      }
    },
    getKeyringsByType (type: string) {
      expect(type).toStrictEqual(keyring.type())
      return keyring
    }
  }
}

test('Test sign Ledger transaction, nonce failed', () => {
  const txInfo = getMockedTransactionInfo()
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '' })
  const expectedError = { success: false, error: getLocale('braveWalletApproveTransactionError') }
  return expect(signLedgerTransaction(apiProxy as unknown as APIProxyControllers,
    'path', txInfo)).resolves.toStrictEqual(expectedError)
})

test('Test sign Ledger transaction, approved, no message to sign', () => {
  const txInfo = getMockedTransactionInfo()
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '0x1' })
  const expectedError = { success: false, error: getLocale('braveWalletNoMessageToSignError') }
  return expect(signLedgerTransaction(apiProxy as unknown as APIProxyControllers,
    'path', txInfo)).resolves.toStrictEqual(expectedError)
})

test('Test sign Ledger transaction, approved, device error', () => {
  const txInfo = getMockedTransactionInfo()
  const expectedData = 'mockedmessage'
  const messageToSign = { message: expectedData }
  const expectedPath = 'test'
  const mockedKeyring = getMockedLedgerKeyring(expectedPath, expectedData)
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '0x1' }, messageToSign, mockedKeyring)
  const expectedError = { success: false, error: getLocale('braveWalletSignOnDeviceError') }
  return expect(signLedgerTransaction(apiProxy as unknown as APIProxyControllers,
    expectedPath, txInfo)).resolves.toStrictEqual(expectedError)
})

test('Test sign Ledger transaction, approved, processing error', () => {
  const txInfo = getMockedTransactionInfo()
  const expectedData = 'raw_message_to_sign'
  const messageToSign = { message: expectedData }
  const expectedPath = 'path'
  const vrs = { v: 1, r: 'R', s: 'S' } as SignatureVRS
  const mockedKeyring = getMockedLedgerKeyring(expectedPath, expectedData, vrs)
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '0x1' }, messageToSign, mockedKeyring)
  const expectedError = { success: false, error: getLocale('braveWalletProcessTransactionError') }
  return expect(signLedgerTransaction(apiProxy as unknown as APIProxyControllers,
    expectedPath, txInfo)).resolves.toStrictEqual(expectedError)
})

test('Test sign Ledger transaction, approved, processed', () => {
  const txInfo = getMockedTransactionInfo()
  const expectedData = 'raw_message_to_sign'
  const messageToSign = { message: expectedData }
  const expectedPath = 'path'
  const vrs = { v: 1, r: 'R', s: 'S' } as SignatureVRS
  const mockedKeyring = getMockedLedgerKeyring(expectedPath, expectedData, vrs)
  const signatureResponse = { status: true }
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '0x1' }, messageToSign, mockedKeyring, signatureResponse)
  return expect(signLedgerTransaction(apiProxy as unknown as APIProxyControllers,
    expectedPath, txInfo)).resolves.toStrictEqual({ success: true })
})

test('Test sign Trezor transaction, approve failed', () => {
  const txInfo = getMockedTransactionInfo()
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '' })
  const expectedError = { success: false, error: getLocale('braveWalletApproveTransactionError') }
  return expect(signTrezorTransaction(apiProxy as unknown as APIProxyControllers,
         'path', txInfo)).resolves.toStrictEqual(expectedError)
})

test('Test sign Trezor transaction, approved, device error', () => {
  const txInfo = getMockedTransactionInfo()
  const expectedPath = 'path'
  const signed = { success: false, payload: {  error: 'error', code: '111' } } as Unsuccessful
  const mockedKeyring = getMockedTrezorKeyring(expectedPath, txInfo, signed)
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '0x1' }, undefined, mockedKeyring)
  const expectedError = { success: false, error: getLocale('braveWalletSignOnDeviceError') }
  return expect(signTrezorTransaction(apiProxy as unknown as APIProxyControllers,
    expectedPath, txInfo)).resolves.toStrictEqual(expectedError)
})

test('Test sign Trezor transaction, approved, processing error', () => {
  const txInfo = getMockedTransactionInfo()
  const expectedPath = 'path'
  const vrs = { v: '0xV', r: '0xR', s: '0xS' } as EthereumSignedTx
  const signed = { success: true, payload: vrs } as Success<EthereumSignedTx>
  const mockedKeyring = getMockedTrezorKeyring(expectedPath, txInfo, signed)
  const signatureResponse = { status: false }
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '0x1' }, undefined, mockedKeyring, signatureResponse)
  const expectedError = { success: false, error: getLocale('braveWalletProcessTransactionError') }
  return expect(signTrezorTransaction(apiProxy as unknown as APIProxyControllers,
    expectedPath, txInfo)).resolves.toStrictEqual(expectedError)
})

test('Test sign Trezor transaction, approved, processed', () => {
  const txInfo = getMockedTransactionInfo()
  const expectedPath = 'path'
  const vrs = { v: '0xV', r: '0xR', s: '0xS' } as EthereumSignedTx
  const signed = { success: true, payload: vrs } as Success<EthereumSignedTx>
  const mockedKeyring = getMockedTrezorKeyring(expectedPath, txInfo, signed)
  const signatureResponse = { status: true }
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '0x1' }, undefined, mockedKeyring, signatureResponse)
  return expect(signTrezorTransaction(apiProxy as unknown as APIProxyControllers,
    expectedPath, txInfo)).resolves.toStrictEqual({ success: true })
})
