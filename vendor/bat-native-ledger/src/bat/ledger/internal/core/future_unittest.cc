/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ledger/internal/core/future.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ledger {

class FutureTest : public testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(FutureTest, CompleteResultSentInFutureTurn) {
  auto future =
      Future<int>::Create([](auto resolver) { resolver.Complete(10); });
  int value = 0;
  future.Then(base::BindLambdaForTesting([&value](int v) { value = v; }));
  ASSERT_EQ(value, 0);
  task_environment_.RunUntilIdle();
  ASSERT_EQ(value, 10);
}

TEST_F(FutureTest, CompleteCallbacksExecutedInFutureTurn) {
  FuturePair<int> pair;
  int value = 0;
  pair.future.Then(base::BindLambdaForTesting([&value](int v) { value = v; }));
  pair.resolver.Complete(1);
  ASSERT_EQ(value, 0);
  task_environment_.RunUntilIdle();
  ASSERT_EQ(value, 1);
}

TEST_F(FutureTest, ThenMapping) {
  auto future =
      Future<int>::Create([](auto resolver) { resolver.Complete(1); });

  double value = 0;

  future.Then(base::BindOnce([](int v) { return static_cast<double>(v) / 2; }))
      .Then(base::BindLambdaForTesting([&value](double v) { value = v; }));

  ASSERT_EQ(value, 0);
  task_environment_.RunUntilIdle();
  ASSERT_EQ(value, 0.5);
}

TEST_F(FutureTest, Completed) {
  int value = 0;
  Future<int>::Completed(1).Then(
      base::BindLambdaForTesting([&value](int v) { value = v; }));
  ASSERT_EQ(value, 0);
  task_environment_.RunUntilIdle();
  ASSERT_EQ(value, 1);
}

}  // namespace ledger
