// Copyright (c) 2019 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

#include <utility>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/views/bubble/bubble_contents_wrapper.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/widget/widget.h"

#define RenderViewHostChanged AddNewContents(content::WebContents* source, \
                      std::unique_ptr<content::WebContents> new_contents,  \
                      const GURL& target_url,                              \
                      WindowOpenDisposition disposition,                   \
                      const gfx::Rect& initial_rect,                       \
                      bool user_gesture,                                   \
                      bool* was_blocked) {                                 \
  auto* panel_widget = views::Widget::GetTopLevelWidgetForNativeView(source->GetNativeView()); \
  auto* widget = panel_widget->parent(); \
  auto* browser = chrome::FindBrowserWithWindow(widget->GetNativeWindow()); \
  browser->GetDelegateWeakPtr()->AddNewContents(source, std::move(new_contents), target_url, WindowOpenDisposition::NEW_POPUP, \
                          initial_rect, user_gesture, was_blocked); \
} \
void BubbleContentsWrapper::RenderViewHostChanged
#include "../../../../../../chrome/browser/ui/views/bubble/bubble_contents_wrapper.cc"
#undef RenderViewHostChanged
