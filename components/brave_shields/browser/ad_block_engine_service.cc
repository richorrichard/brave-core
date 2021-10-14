/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_shields/browser/ad_block_engine_service.h"

#include "base/logging.h"
#include "brave/components/adblock_rust_ffi/src/wrapper.h"

using brave_component_updater::DATFileDataBuffer;

namespace brave_shields {

AdBlockEngineService::AdBlockEngineService(
    scoped_refptr<base::SequencedTaskRunner> task_runner)
    : AdBlockBaseService(task_runner) {}

AdBlockEngineService::~AdBlockEngineService() {}

void AdBlockEngineService::OnInitialLoad(bool deserialize, const DATFileDataBuffer& dat_buf) {
  if (deserialize) {
    OnNewDATAvailable(dat_buf);
  } else {
    OnNewListSourceAvailable(dat_buf);
  }
}

bool AdBlockEngineService::Init(SourceProvider* source_provider, ResourceProvider* resource_provider) {
  source_provider->Load(base::BindOnce(&AdBlockEngineService::OnInitialLoad, base::Unretained(this)));
  source_provider->AddObserver(this);
  return true;
}

bool AdBlockEngineService::Init() {
  //AdBlockBaseService::Init();
  return true;
}

void AdBlockEngineService::OnNewListSourceAvailable(const DATFileDataBuffer& list_source) {
  GetTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &AdBlockEngineService::UpdateFiltersOnFileTaskRunner,
          base::Unretained(this), list_source));
}

void AdBlockEngineService::UpdateFiltersOnFileTaskRunner(
    const DATFileDataBuffer& filters) {
  DCHECK(GetTaskRunner()->RunsTasksInCurrentSequence());
  ad_block_client_.reset(new adblock::Engine(reinterpret_cast<const char*>(filters.data()), filters.size()));
}

void AdBlockEngineService::OnNewDATAvailable(const DATFileDataBuffer& dat_buf) {
  // An empty buffer will not load successfully.
  if (dat_buf.empty()) {
    return;
  }
  GetTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &AdBlockEngineService::UpdateDATOnFileTaskRunner,
          base::Unretained(this), dat_buf));
}

void AdBlockEngineService::UpdateDATOnFileTaskRunner(const DATFileDataBuffer& dat_buf) {
  DCHECK(GetTaskRunner()->RunsTasksInCurrentSequence());
  adblock::Engine* e = new adblock::Engine();
  e->deserialize(reinterpret_cast<const char*>(&dat_buf.front()), dat_buf.size());
  ad_block_client_.reset(e);
}

void AdBlockEngineService::OnNewResourcesAvailable(const std::string& resources_json) {
  ad_block_client_->addResources(resources_json);
}

///////////////////////////////////////////////////////////////////////////////

std::unique_ptr<AdBlockEngineService> AdBlockEngineServiceFactory(scoped_refptr<base::SequencedTaskRunner> task_runner) {
  return std::make_unique<AdBlockEngineService>(task_runner);
}

}  // namespace brave_shields
