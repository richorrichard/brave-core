/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_BRAVE_WALLET_ERC_TOKEN_IMAGES_SOURCE_H_
#define BRAVE_BROWSER_BRAVE_WALLET_ERC_TOKEN_IMAGES_SOURCE_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/url_data_source.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace base {
class FilePath;
}  // namespace base

namespace brave_wallet {

// This serves background image data.
class ERCTokenImagesSource : public content::URLDataSource {
 public:
  explicit ERCTokenImagesSource(const base::FilePath& base_path);

  ~ERCTokenImagesSource() override;

  ERCTokenImagesSource(const ERCTokenImagesSource&) = delete;
  ERCTokenImagesSource& operator=(const ERCTokenImagesSource&) = delete;
  std::string getImagePath();

 private:
  FRIEND_TEST_ALL_PREFIXES(ERCTokenImagesSourceTest, GetMimeType);
  friend class ERCTokenImagesSourceTest;

  // content::URLDataSource overrides:
  std::string GetSource() override;
  void StartDataRequest(const GURL& url,
                        const content::WebContents::Getter& wc_getter,
                        GotDataCallback callback) override;
  std::string GetMimeType(const std::string& path) override;
  bool AllowCaching() override;

  void OnGotImageFile(GotDataCallback callback,
                      absl::optional<std::string> input);

  base::FilePath base_path_;
  base::WeakPtrFactory<ERCTokenImagesSource> weak_factory_;
};

}  // namespace brave_wallet

#endif  // BRAVE_BROWSER_BRAVE_WALLET_ERC_TOKEN_IMAGES_SOURCE_H_
