TrezorConnect.init({
    connectSrc: 'https://localhost:8088/',
    lazyLoad: false, // this param will prevent iframe injection until TrezorConnect.method will be called
    debug: true,
    manifest: {
        email: 'developer@xyz.com',
        appUrl: 'http://your.application.com',
    }
}).then(function() {
    console.log('TrezorConnect is Ready')
    TrezorConnect.getPublicKey({path: "m/49'/0'/0'"}).then(function(result) {
        console.log(result)
    }).catch(error => {
        console.log(error)
    })
}).catch(error => {
    console.log(error)
})
