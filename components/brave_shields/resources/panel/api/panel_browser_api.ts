// Copyright (c) 2021 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

import * as BraveShields from 'gen/brave/components/brave_shields/common/brave_shields_panel.mojom.m.js'
// Provide access to all the generated types
export * from 'gen/brave/components/brave_shields/common/brave_shields_panel.mojom.m.js'

interface API {
  pageCallbackRouter: BraveShields.PageCallbackRouter
  panelHandler: BraveShields.PanelHandlerRemote
}

let panelBrowserAPIInstance: API
class PanelBrowserAPI implements API {
  pageCallbackRouter = new BraveShields.PageCallbackRouter()
  panelHandler = new BraveShields.PanelHandlerRemote()

  constructor () {
    const factory = BraveShields.PanelHandlerFactory.getRemote()
    factory.createPanelHandler(
      this.pageCallbackRouter.$.bindNewPipeAndPassRemote(),
      this.panelHandler.$.bindNewPipeAndPassReceiver()
    )
  }
}

export default function getPanelBrowserAPI () {
  if (!panelBrowserAPIInstance) {
    panelBrowserAPIInstance = new PanelBrowserAPI()
  }
  return panelBrowserAPIInstance
}
