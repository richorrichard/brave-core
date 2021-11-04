/* Copyright 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_FEDERATED_LEARNING_BRAVE_FEDERATED_LEARNING_SERVICE_H_
#define BRAVE_COMPONENTS_BRAVE_FEDERATED_LEARNING_BRAVE_FEDERATED_LEARNING_SERVICE_H_

#include <memory>
#include <string>

#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"

namespace network {
class SharedURLLoaderFactory;
}

class PrefRegistrySimple;

namespace brave {

class BraveOperationalPatterns;

class BraveFederatedLearningService : public KeyedService {
 public:
  BraveFederatedLearningService();
  ~BraveFederatedLearningService() override;

  BraveFederatedLearningService(const BraveFederatedLearningService&) = delete;
  BraveFederatedLearningService& operator=(
      const BraveFederatedLearningService&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  virtual void Start() = 0;
};

}  // namespace brave

#endif  // BRAVE_COMPONENTS_BRAVE_FEDERATED_LEARNING_BRAVE_FEDERATED_LEARNING_SERVICE_H_
