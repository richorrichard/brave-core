
const kTrezorUnlockCommand = 'trezor-unlock'
const kTrezorGetAccountsCommand = 'trezor-get-accounts'
const kTrezorSignTransactionCommand = 'trezor-sign-transaction'

const unlock = async(responseId, source, owner) => {
    console.log(owner)
    TrezorConnect.init({
        connectSrc: 'https://connect.trezor.io/8/',
        lazyLoad: false, // this param will prevent iframe injection until TrezorConnect.method will be called
        debug: true,
        manifest: {
            email: 'developer@xyz.com',
            appUrl: 'http://your.application.com',
        }
    }).then(function(result) {
      console.log('TrezorConnect is Ready:', result)
      source.postMessage({ id: responseId, command: kTrezorUnlockCommand, result: true }, owner)
    }).catch(error => {
      console.log(error)
      source.postMessage({ id: responseId, command: kTrezorUnlockCommand, result: false, error: error}, owner)
    })
}

const getAccounts = async(responseId, source, requestedPaths, owner) => {
    TrezorConnect.getPublicKey({ bundle: requestedPaths }).then((result) => {
        console.log(result)
        source.postMessage({ id: responseId, command: kTrezorGetAccountsCommand, payload: result }, owner)
    }).catch((error) => {
        console.log(error)
        source.postMessage({ id: responseId, command: kTrezorGetAccountsCommand, payload: erorr }, owner)
    })
}

const signTransaction = async(responseId, source, owner, payload) => {
    console.log(JSON.stringify(payload))
    TrezorConnect.ethereumSignTransaction(payload).then((result) => {
        console.log(result)
        source.postMessage({ id: responseId, command: kTrezorSignTransactionCommand, payload: result }, owner)
    }).catch((error) => {
        console.log(error)
        source.postMessage({ id: responseId, command: kTrezorSignTransactionCommand, payload: erorr }, owner)
    })
}

window.addEventListener('message', (event) => {
    console.log(event)
    if (event.origin !== event.data.owner || event.type !== 'message')
        return
    console.log(event.data)
    if (event.data.command === kTrezorUnlockCommand) {
        return unlock(event.data.id, event.source, event.data.owner)
    }
    if (event.data.command === kTrezorGetAccountsCommand) {
        return getAccounts(event.data.id, event.source, event.data.paths, event.data.owner)
/*
        return signTransaction(event.data.id, event.source, event.data.owner, {
            "path": "m/44'/60'/0'/1",
            "transaction": {
                "to": "0x6637526cd87a977b7235348576862e332e418577",
                "value": "0x03782dace9d90000",
                "data": "",
                "chainId": 1337,
                "nonce": "0x01",
                "gasLimit": "0x5208",
                "gasPrice": "0x22ecb25c00"
            }
        })
*/
    }
    if (event.data.command === kTrezorSignTransactionCommand) {
        return signTransaction(event.data.id, event.source, event.data.owner, event.data.payload)
    }

    
})
