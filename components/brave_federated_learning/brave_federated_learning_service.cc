/* Copyright 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#include "brave/components/brave_federated_learning/brave_federated_learning_service.h"

#include "brave/components/brave_federated_learning/brave_operational_patterns.h"
#include "components/prefs/pref_registry_simple.h"

namespace brave {

BraveFederatedLearningService::BraveFederatedLearningService() {}

BraveFederatedLearningService::~BraveFederatedLearningService() {}

// static
void BraveFederatedLearningService::RegisterProfilePrefs(
    PrefRegistrySimple* registry) {
  BraveOperationalPatterns::RegisterLocalStatePrefs(registry);
}

}  // namespace brave
