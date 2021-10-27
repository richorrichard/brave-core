import TrezorConnect from 'trezor-connect'
import {
  kTrezorUnlockCommand,
  kTrezorGetAccountsCommand,
  kTrezorBridgeOwners,
  kTrezorSignTransactionCommand
} from '../common/trezor/constants'

const unlock = async (responseId: string, source: any, owner: any) => {
  TrezorConnect.init({
    connectSrc: 'https://connect.trezor.io/8/',
    lazyLoad: false,
    manifest: {
      email: 'support@brave.com',
      appUrl: 'https://brave.com'
    }
  }).then(() => {
    source.postMessage({ id: responseId, command: kTrezorUnlockCommand, result: true }, owner)
  }).catch(error => {
    source.postMessage({ id: responseId, command: kTrezorUnlockCommand, result: false, error: error }, owner)
  })
}

const getAccounts = async (responseId: string, source: any, requestedPaths: any, owner: any) => {
  TrezorConnect.getPublicKey({ bundle: requestedPaths }).then((result) => {
    source.postMessage({ id: responseId, command: kTrezorGetAccountsCommand, payload: result }, owner)
  }).catch((error) => {
    source.postMessage({ id: responseId, command: kTrezorGetAccountsCommand, payload: error }, owner)
  })
}

const signTransaction = async (responseId: string, source: any, owner: any, payload: any) => {
  TrezorConnect.ethereumSignTransaction(payload).then((result) => {
    source.postMessage({ id: responseId, command: kTrezorSignTransactionCommand, payload: result }, owner)
  }).catch((error) => {
    source.postMessage({ id: responseId, command: kTrezorSignTransactionCommand, payload: error }, owner)
  })
}

window.addEventListener('message', (event) => {
  if (event.origin !== event.data.owner || event.type !== 'message' ||
      event.origin in kTrezorBridgeOwners) {
    return
  }
  if (event.data.command === kTrezorUnlockCommand) {
    return unlock(event.data.id, event.source, event.data.owner)
  }
  if (event.data.command === kTrezorGetAccountsCommand) {
    return getAccounts(event.data.id, event.source, event.data.paths, event.data.owner)
  }
  if (event.data.command === kTrezorSignTransactionCommand) {
    return signTransaction(event.data.id, event.source, event.data.owner, event.data.payload)
  }

  return
})
