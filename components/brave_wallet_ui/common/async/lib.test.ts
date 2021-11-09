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
ProcessHardwareSignatureReturnInfo,
SignHardwareTransactionType
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

const signTransactionWithLedger = (vrs?: SignatureVRS, signatureResponse?: boolean): Promise<SignHardwareTransactionType> => {
  const txInfo = getMockedTransactionInfo()
  const expectedData = 'raw_message_to_sign'
  const messageToSign = { message: expectedData }
  const expectedPath = 'path'
  const mockedKeyring = getMockedLedgerKeyring(expectedPath, expectedData, vrs)
  const signed = signatureResponse ? { status: signatureResponse } : undefined
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '0x1' }, messageToSign,
                                              mockedKeyring, signed)
  return signLedgerTransaction(apiProxy as unknown as APIProxyControllers,
      expectedPath, txInfo)
}

const hardwareTransactionErrorResponse = (errorId: string): SignHardwareTransactionType => {
  return { success: false, error: getLocale(errorId) }
}

const signTransactionWithTrezor = (signed: Success<EthereumSignedTx> | Unsuccessful, signatureResponse?: ProcessHardwareSignatureReturnInfo) => {
  const txInfo = getMockedTransactionInfo()
  const expectedPath = 'path'
  const mockedKeyring = getMockedTrezorKeyring(expectedPath, txInfo, signed)
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '0x1' }, undefined, mockedKeyring, signatureResponse)
  return signTrezorTransaction(apiProxy as unknown as APIProxyControllers,
    expectedPath, txInfo)
}

test('Test sign Ledger transaction, nonce failed', () => {
  const txInfo = getMockedTransactionInfo()
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '' })
  return expect(signLedgerTransaction(apiProxy as unknown as APIProxyControllers,
    'path', txInfo)).resolves.toStrictEqual(hardwareTransactionErrorResponse('braveWalletApproveTransactionError'))
})

test('Test sign Ledger transaction, approved, no message to sign', () => {
  const txInfo = getMockedTransactionInfo()
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '0x1' })
  return expect(signLedgerTransaction(apiProxy as unknown as APIProxyControllers,
    'path', txInfo)).resolves.toStrictEqual(hardwareTransactionErrorResponse('braveWalletNoMessageToSignError'))
})

test('Test sign Ledger transaction, approved, device error', () => {
  return expect(signTransactionWithLedger()).resolves.toStrictEqual(
    hardwareTransactionErrorResponse('braveWalletSignOnDeviceError'))
})

test('Test sign Ledger transaction, approved, processing error', () => {
  return expect(signTransactionWithLedger({ v: 1, r: 'R', s: 'S' })).resolves.toStrictEqual(
      hardwareTransactionErrorResponse('braveWalletProcessTransactionError'))
})

test('Test sign Ledger transaction, approved, processed', () => {
  return expect(signTransactionWithLedger({ v: 1, r: 'R', s: 'S' }, true)).resolves.toStrictEqual({ success: true })
})

test('Test sign Trezor transaction, approve failed', () => {
  const txInfo = getMockedTransactionInfo()
  const apiProxy = getMockedProxyControllers(txInfo.id, { nonce: '' })
  return expect(signTrezorTransaction(apiProxy as unknown as APIProxyControllers,
         'path', txInfo)).resolves.toStrictEqual(
          hardwareTransactionErrorResponse('braveWalletApproveTransactionError'))
})

test('Test sign Trezor transaction, approved, device error', () => {
  return expect(signTransactionWithTrezor({ success: false, payload: {  error: 'error', code: '111' } }))
                .resolves.toStrictEqual(hardwareTransactionErrorResponse('braveWalletSignOnDeviceError'))
})

test('Test sign Trezor transaction, approved, processing error', () => {
  return expect(signTransactionWithTrezor({ success: true, payload: { v: '0xV', r: '0xR', s: '0xS' } }, { status: false })).resolves.toStrictEqual(
      hardwareTransactionErrorResponse('braveWalletProcessTransactionError'))
})

test('Test sign Trezor transaction, approved, processed', () => {
  return expect(signTransactionWithTrezor({ success: true, payload: { v: '0xV', r: '0xR', s: '0xS' } }, { status: true })).resolves.toStrictEqual(
    { success: true })
})
