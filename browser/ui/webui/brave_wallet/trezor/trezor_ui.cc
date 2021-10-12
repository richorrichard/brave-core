/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/brave_wallet/trezor/trezor_ui.h"

#include "brave/common/webui_url_constants.h"
#include "components/grit/brave_components_resources.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_data_source.h"

namespace trezor {

UntrustedTrezorUI::UntrustedTrezorUI(content::WebUI* web_ui)
    : ui::UntrustedWebUIController(web_ui) {
  auto* untrusted_source =
      content::WebUIDataSource::Create(kUntrustedTrezorURL);
  untrusted_source->SetDefaultResource(IDR_BRAVE_WALLET_TREZOR_BRIDGE_HTML);
  untrusted_source->AddResourcePath("trezor.js",
                                    IDR_BRAVE_WALLET_TREZOR_BRIDGE_JS);
  untrusted_source->AddFrameAncestor(GURL(kBraveUIWalletPageURL));
  untrusted_source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc,
      std::string("script-src 'self' https://connect.trezor.io/;"));
  untrusted_source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc,
      std::string("frame-src https://localhost:8088/;"));
  untrusted_source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::StyleSrc,
      std::string("style-src 'unsafe-inline';"));

  // Register the URLDataSource
  auto* browser_context = web_ui->GetWebContents()->GetBrowserContext();
  content::WebUIDataSource::Add(browser_context, untrusted_source);
}

UntrustedTrezorUI::~UntrustedTrezorUI() = default;

std::unique_ptr<content::WebUIController>
UntrustedTrezorUIConfig::CreateWebUIController(content::WebUI* web_ui) {
  return std::make_unique<UntrustedTrezorUI>(web_ui);
}

UntrustedTrezorUIConfig::UntrustedTrezorUIConfig()
    : WebUIConfig(content::kChromeUIUntrustedScheme, kUntrustedTrezorHost) {}

}  // namespace trezor
