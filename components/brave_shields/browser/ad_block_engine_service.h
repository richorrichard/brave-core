/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_SHIELDS_BROWSER_AD_BLOCK_ENGINE_SERVICE_H_
#define BRAVE_COMPONENTS_BRAVE_SHIELDS_BROWSER_AD_BLOCK_ENGINE_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "brave/components/brave_shields/browser/ad_block_base_service.h"
#include "brave/components/brave_shields/browser/ad_block_resource_provider.h"
#include "brave/components/brave_shields/browser/ad_block_source_provider.h"

class AdBlockServiceTest;

namespace brave_shields {

using brave_component_updater::DATFileDataBuffer;

// Manages an instance of an adblock-rust engine.
class AdBlockEngineService : public AdBlockBaseService, public ResourceProvider::Observer, public SourceProvider::Observer {
 public:
  explicit AdBlockEngineService(scoped_refptr<base::SequencedTaskRunner> task_runner);
  ~AdBlockEngineService() override;

  void OnNewListSourceAvailable(const DATFileDataBuffer& list_source) override;
  void OnNewDATAvailable(const DATFileDataBuffer& list_source) override;

  void OnNewResourcesAvailable(const std::string& resources_json) override;

  void OnInitialLoad(bool deserialize, const DATFileDataBuffer& dat_buf);

  bool Init(SourceProvider* source_provider, ResourceProvider* resource_provider);

 protected:
  bool Init() override;

 private:
  friend class ::AdBlockServiceTest;
  void UpdateFiltersOnFileTaskRunner(const DATFileDataBuffer& filters);
  void UpdateDATOnFileTaskRunner(const DATFileDataBuffer& dat_buf);

  base::WeakPtrFactory<AdBlockEngineService> weak_factory_{this};

  AdBlockEngineService(const AdBlockEngineService&) = delete;
  AdBlockEngineService& operator=(const AdBlockEngineService&) = delete;
};

// Creates the AdBlockEngineService
std::unique_ptr<AdBlockEngineService>
AdBlockEngineServiceFactory(scoped_refptr<base::SequencedTaskRunner> task_runner);

}  // namespace brave_shields

#endif  // BRAVE_COMPONENTS_BRAVE_SHIELDS_BROWSER_AD_BLOCK_ENGINE_SERVICE_H_
