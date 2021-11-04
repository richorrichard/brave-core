/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/browser/brave_federated_learning/brave_federated_learning_service_factory.h"

#include <memory>
#include <string>
#include <utility>

#include "brave/browser/brave_browser_process.h"
#include "brave/browser/profiles/profile_util.h"
#include "brave/components/brave_federated_learning/brave_federated_learning_service_impl.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace brave {

// static
BraveFederatedLearningService*
BraveFederatedLearningServiceFactory::GetForProfile(Profile* profile) {
  if (!brave::IsRegularProfile(profile)) {
    return nullptr;
  }

  return static_cast<BraveFederatedLearningService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
BraveFederatedLearningServiceFactory*
BraveFederatedLearningServiceFactory::GetInstance() {
  return base::Singleton<BraveFederatedLearningServiceFactory>::get();
}

BraveFederatedLearningServiceFactory::BraveFederatedLearningServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "BraveFederatedLearningService",
          BrowserContextDependencyManager::GetInstance()) {}

BraveFederatedLearningServiceFactory::~BraveFederatedLearningServiceFactory() {}

KeyedService* BraveFederatedLearningServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  auto* profile = Profile::FromBrowserContext(context);
  auto url_loader_factory = context->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess();
  std::unique_ptr<BraveFederatedLearningServiceImpl>
      brave_federated_learning_service(new BraveFederatedLearningServiceImpl(
          profile->GetPrefs(), g_browser_process->local_state(),
          url_loader_factory));
  return brave_federated_learning_service.release();
}

bool BraveFederatedLearningServiceFactory::ServiceIsNULLWhileTesting() const {
  return false;
}

}  // namespace brave
