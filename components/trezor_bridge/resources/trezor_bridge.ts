// Copyright (c) 2021 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

import TrezorConnect, {
  UI,
  UI_EVENT
} from 'trezor-connect'

TrezorConnect.on(UI_EVENT, (event) => {
  if (event.type === UI.REQUEST_PASSPHRASE) {
    const features = event.payload.device.features
    if (features && features.capabilities && features.capabilities.includes('Capability_PassphraseEntry')) {
      // choose to enter passphrase on device
      TrezorConnect.uiResponse({
        type: UI.RECEIVE_PASSPHRASE,
        payload: { passphraseOnDevice: true, value: '', save: true }
      })
    } else {
      TrezorConnect.uiResponse({
        type: UI.RECEIVE_PASSPHRASE,
        payload: { value: '', save: true }
      })
    }
  }
  if (event.type === UI.SELECT_DEVICE) {
    if (event.payload.devices.length > 0) {
      // More then one device connected,
      // We take first in the list now for simpicity
      TrezorConnect.uiResponse({
        type: UI.RECEIVE_DEVICE,
        payload: { device: event.payload.devices[0], remember: true }
      })
    } else {
      console.log('no devices connected, waiting for connection')
    }
  }
})

TrezorConnect.init({
  webusb: false, // webusb is not supported
  debug: true, // see what's going on inside connect
  // lazyLoad: true, // set to "false" (default) if you want to start communication with bridge on application start (and detect connected device right away)
  // set it to "true", then trezor-connect will not be initialized until you call some TrezorConnect.method()
  // this is useful when you don't know if you are dealing with Trezor user
  manifest: {
    email: 'support@brave.com',
    appUrl: 'web-ui-boilerplate'
  },
  env: 'web'
}).then(() => {
  console.log('TrezorConnect is ready!')
  TrezorConnect.getPublicKey({
    path: "m/49'/0'/0'",
    coin: 'btc'
  }).then(response => {
    console.log(response)
  })
}).catch(error => {
  console.log('TrezorConnect init error', `TrezorConnect init error:${error}`)
})
