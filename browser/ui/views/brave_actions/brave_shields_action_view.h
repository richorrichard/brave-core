// Copyright (c) 2021 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_UI_VIEWS_BRAVE_ACTIONS_BRAVE_SHIELDS_ACTION_VIEW_H_
#define BRAVE_BROWSER_UI_VIEWS_BRAVE_ACTIONS_BRAVE_SHIELDS_ACTION_VIEW_H_

#include <memory>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/prefs/pref_member.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/view.h"

class Profile;

class BraveShieldsActionView : public views::LabelButton {
 public:
  class Delegate {
   public:
    virtual void OnRewardsStubButtonClicked() = 0;
    virtual gfx::Size GetToolbarActionSize() = 0;

   protected:
    ~Delegate() {}
  };

  explicit BraveShieldsActionView(Delegate* delegate);
  ~BraveShieldsActionView() override;
  void Update();
  // views::LabelButton:
  std::unique_ptr<views::LabelButtonBorder> CreateDefaultBorder()
      const override;

  SkPath GetHighlightPath() const;

 private:
  gfx::Size CalculatePreferredSize() const override;
  void ButtonPressed();

  Delegate* delegate_;

  DISALLOW_COPY_AND_ASSIGN(BraveShieldsActionView);
};

#endif  // BRAVE_BROWSER_UI_VIEWS_BRAVE_ACTIONS_BRAVE_SHIELDS_ACTION_VIEW_H_
