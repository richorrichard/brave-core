/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "chrome/browser/chrome_browser_main.h"
#include "brave/browser/brave_browser_process_impl.h"
#include "brave/browser/ui/webui/brave_untrusted_web_ui_controller_factory.h"
#include "chrome/browser/ui/webui/chrome_untrusted_web_ui_controller_factory.h"

#define BrowserProcessImpl BraveBrowserProcessImpl
#define ChromeUntrustedWebUIControllerFactory               \
  BraveUntrustedWebUIControllerFactory::RegisterInstance(); \
  ChromeUntrustedWebUIControllerFactory
#include "../../../../chrome/browser/chrome_browser_main.cc"
#undef ChromeUntrustedWebUIControllerFactory
#undef BrowserProcessImpl
