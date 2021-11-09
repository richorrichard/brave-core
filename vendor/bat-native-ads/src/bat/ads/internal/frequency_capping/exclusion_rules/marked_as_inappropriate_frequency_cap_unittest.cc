/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ads/internal/frequency_capping/exclusion_rules/marked_as_inappropriate_frequency_cap.h"

#include "bat/ads/ad_content_info.h"
#include "bat/ads/internal/client/client.h"
#include "bat/ads/internal/unittest_base.h"
#include "bat/ads/internal/unittest_util.h"

// npm run test -- brave_unit_tests --filter=BatAds*

namespace ads {

namespace {
const char kCreativeInstanceId[] = "9cf19f6e-25b8-44f1-9050-2a7247185489";
const char kCreativeSetId[] = "654f10df-fbc4-4a92-8d43-2edf73734a60";
}  // namespace

class BatAdsMarkedAsInappropriateFrequencyCapTest : public UnitTestBase {
 protected:
  BatAdsMarkedAsInappropriateFrequencyCapTest() = default;

  ~BatAdsMarkedAsInappropriateFrequencyCapTest() override = default;
};

TEST_F(BatAdsMarkedAsInappropriateFrequencyCapTest, AllowAd) {
  // Arrange
  CreativeAdInfo creative_ad;
  creative_ad.creative_instance_id = kCreativeInstanceId;
  creative_ad.creative_set_id = kCreativeSetId;

  // Act
  MarkedAsInappropriateFrequencyCap frequency_cap;
  const bool should_exclude = frequency_cap.ShouldExclude(creative_ad);

  // Assert
  EXPECT_FALSE(should_exclude);
}

TEST_F(BatAdsMarkedAsInappropriateFrequencyCapTest, DoNotAllowAd) {
  // Arrange
  CreativeAdInfo creative_ad;
  creative_ad.creative_instance_id = kCreativeInstanceId;
  creative_ad.creative_set_id = kCreativeSetId;

  AdContentInfo ad_content;
  ad_content.creative_set_id = kCreativeSetId;
  ad_content.is_flagged = false;

  Client::Get()->ToggleFlaggedAd(ad_content);

  // Act
  MarkedAsInappropriateFrequencyCap frequency_cap;
  const bool should_exclude = frequency_cap.ShouldExclude(creative_ad);

  // Assert
  EXPECT_TRUE(should_exclude);
}

}  // namespace ads
