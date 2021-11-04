/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_FEDERATED_LEARNING_BRAVE_FEDERATED_LEARNING_SERVICE_IMPL_H_
#define BRAVE_COMPONENTS_BRAVE_FEDERATED_LEARNING_BRAVE_FEDERATED_LEARNING_SERVICE_IMPL_H_

#include <memory>
#include <string>

#include "brave/components/brave_federated_learning/brave_federated_learning_service.h"
#include "components/prefs/pref_change_registrar.h"

class Profile;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace brave {

class BraveOperationalPatterns;

class BraveFederatedLearningServiceImpl
    : public brave::BraveFederatedLearningService,
      public base::SupportsWeakPtr<BraveFederatedLearningServiceImpl> {
 public:
  explicit BraveFederatedLearningServiceImpl(
      PrefService* prefs,
      PrefService* local_state,
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory);
  ~BraveFederatedLearningServiceImpl() override;

  BraveFederatedLearningServiceImpl(const BraveFederatedLearningServiceImpl&) =
      delete;
  BraveFederatedLearningServiceImpl& operator=(
      const BraveFederatedLearningServiceImpl&) = delete;

  void Start() override;

 private:
  void InitPrefChangeRegistrar();
  void OnPreferenceChanged(const std::string& key);

  bool IsP3AEnabled();
  bool IsOperationalPatternsEnabled();

  PrefService* prefs_;
  PrefService* local_state_;
  PrefChangeRegistrar pref_change_registrar_;
  std::unique_ptr<BraveOperationalPatterns> operational_patterns_;
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
};

}  // namespace brave

#endif  // BRAVE_COMPONENTS_BRAVE_FEDERATED_LEARNING_BRAVE_FEDERATED_LEARNING_SERVICE_IMPL_H_
