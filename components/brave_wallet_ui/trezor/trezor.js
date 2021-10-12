
const kTrezorUnlockCommand = 'trezor-unlock'
const kWalletOrigin = 'chrome://wallet'
const kTrezorGetAccountsCommand = 'trezor-get-accounts'

const unlock = async(responseId, source) => {
    TrezorConnect.init({
        connectSrc: 'https://localhost:8088/',
        lazyLoad: false, // this param will prevent iframe injection until TrezorConnect.method will be called
        debug: true,
        manifest: {
            email: 'developer@xyz.com',
            appUrl: 'http://your.application.com',
        }
    }).then(function(result) {
      console.log('TrezorConnect is Ready:', result)
      source.postMessage({ id: responseId, command: kTrezorUnlockCommand, result: true }, kWalletOrigin)
    }).catch(error => {
      console.log(error)
      source.postMessage({ id: responseId, command: kTrezorUnlockCommand, result: false, error: error}, kWalletOrigin)
    })
}

const getAccounts = async(responseId, source, requestedPaths) => {
    TrezorConnect.getPublicKey({ bundle: requestedPaths }).then((result) => {
        console.log(result)
        source.postMessage({ id: responseId, command: kTrezorGetAccountsCommand, payload: result }, kWalletOrigin)
    }).catch((error) => {
        console.log(error)
        source.postMessage({ id: responseId, command: kTrezorGetAccountsCommand, payload: erorr }, kWalletOrigin)
    })

}

window.addEventListener('message', (event) => {
    console.log(event)
    if (event.origin !== kWalletOrigin || event.type !== 'message')
        return
    console.log(event.data)
    if (event.data.command === kTrezorUnlockCommand) {
        return unlock(event.data.id, event.source)
    }
    if (event.data.command === kTrezorGetAccountsCommand) {
        return getAccounts(event.data.id, event.source, event.data.paths)
    }
})
