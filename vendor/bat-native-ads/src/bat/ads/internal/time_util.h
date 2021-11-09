/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_VENDOR_BAT_NATIVE_ADS_SRC_BAT_ADS_INTERNAL_TIME_UTIL_H_
#define BRAVE_VENDOR_BAT_NATIVE_ADS_SRC_BAT_ADS_INTERNAL_TIME_UTIL_H_

namespace base {
class Time;
}  // namespace base

namespace ads {

int GetTimeAsMinutes(const base::Time& time);

base::Time AdjustTimeToBeginningOfPreviousMonth(const base::Time& time);
base::Time AdjustTimeToEndOfPreviousMonth(const base::Time& time);
base::Time AdjustTimeToBeginningOfMonth(const base::Time& time);
base::Time AdjustTimeToEndOfMonth(const base::Time& time);

base::Time GetTimeAtBeginningOfLastMonth();
base::Time GetTimeAtEndOfLastMonth();
base::Time GetTimeAtBeginningOfThisMonth();
base::Time GetTimeAtEndOfThisMonth();

}  // namespace ads

#endif  // BRAVE_VENDOR_BAT_NATIVE_ADS_SRC_BAT_ADS_INTERNAL_TIME_UTIL_H_
