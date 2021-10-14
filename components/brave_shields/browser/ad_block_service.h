/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_SHIELDS_BROWSER_AD_BLOCK_SERVICE_H_
#define BRAVE_COMPONENTS_BRAVE_SHIELDS_BROWSER_AD_BLOCK_SERVICE_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/values.h"
#include "brave/components/brave_shields/browser/ad_block_base_service.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_registry_simple.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

class AdBlockServiceTest;
class DomainBlockTest;
class PrefChangeRegistrar;
class PrefService;

using brave_component_updater::BraveComponent;

namespace component_updater {
class ComponentUpdateService;
}  // namespace component_updater

namespace brave_shields {

class AdBlockEngineService;
class AdBlockDefaultSourceProvider;
class AdBlockRegionalServiceManager;
class AdBlockCustomFiltersSourceProvider;
class AdBlockSubscriptionServiceManager;
class ResourceProvider;

//const char kAdBlockResourcesFilename[] = "resources.json";
/*const char kAdBlockComponentName[] = "Brave Ad Block Updater";
const char kAdBlockComponentId[] = "cffkpbalmllkdoenhmdmpbkajipdjfam";
const char kAdBlockComponentBase64PublicKey[] =
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAs0qzJmHSgIiw7IGFCxij"
    "1NnB5hJ5ZQ1LKW9htL4EBOaMJvmqaDs/wfq0nw/goBHWsqqkMBynRTu2Hxxirvdb"
    "cugn1Goys5QKPgAvKwDHJp9jlnADWm5xQvPQ4GE1mK1/I3ka9cEOCzPW6GI+wGLi"
    "VPx9VZrxHHsSBIJRaEB5Tyi5bj0CZ+kcfMnRTsXIBw3C6xJgCVKISQUkd8mawVvG"
    "vqOhBOogCdb9qza5eJ1Cgx8RWKucFfaWWxKLOelCiBMT1Hm1znAoVBHG/blhJJOD"
    "5HcH/heRrB4MvrE1J76WF3fvZ03aHVcnlLtQeiNNOZ7VbBDXdie8Nomf/QswbBGa"
    "VwIDAQAB";*/

// The brave shields service in charge of ad-block checking and init.
class AdBlockService {
 public:
  explicit AdBlockService(
      PrefService* local_state,
      std::string locale,
      component_updater::ComponentUpdateService* cus,
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      std::unique_ptr<AdBlockSubscriptionServiceManager> manager);
  ~AdBlockService();

  void ShouldStartRequest(const GURL& url,
                          blink::mojom::ResourceType resource_type,
                          const std::string& tab_host,
                          bool aggressive_blocking,
                          bool* did_match_rule,
                          bool* did_match_exception,
                          bool* did_match_important,
                          std::string* mock_data_url);
  absl::optional<std::string> GetCspDirectives(
      const GURL& url,
      blink::mojom::ResourceType resource_type,
      const std::string& tab_host);
  absl::optional<base::Value> UrlCosmeticResources(
      const std::string& url);
  absl::optional<base::Value> HiddenClassIdSelectors(
      const std::vector<std::string>& classes,
      const std::vector<std::string>& ids,
      const std::vector<std::string>& exceptions);

  AdBlockRegionalServiceManager* regional_service_manager();
  AdBlockEngineService* custom_filters_service();
  AdBlockEngineService* default_service();
  AdBlockSubscriptionServiceManager* subscription_service_manager();

  AdBlockCustomFiltersSourceProvider* custom_filters_source_provider();

  void EnableTag(const std::string& tag, bool enabled);

  base::SequencedTaskRunner* GetTaskRunner() { return task_runner_.get(); }

  bool Start();
 protected:
  /*void OnComponentReady(const std::string& component_id,
                        const base::FilePath& install_dir,
                        const std::string& manifest) override;
  void OnResourcesFileDataReady(const std::string& resources);*/
  void OnRegionalCatalogFileDataReady(const std::string& catalog_json);

 private:
  friend class ::AdBlockServiceTest;
  friend class ::DomainBlockTest;
  /*static std::string g_ad_block_component_id_;
  static std::string g_ad_block_component_base64_public_key_;*/
  static std::string g_ad_block_dat_file_version_;
  /*static void SetComponentIdAndBase64PublicKeyForTest(
      const std::string& component_id,
      const std::string& component_base64_public_key);*/

  void InitCustomFilters();
  ResourceProvider* resource_provider();

  //BraveComponent::Delegate* component_delegate_;
  PrefService* local_state_;
  std::string locale_;

  component_updater::ComponentUpdateService* component_update_service_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  std::unique_ptr<brave_shields::AdBlockRegionalServiceManager>
      regional_service_manager_;
  std::unique_ptr<brave_shields::AdBlockEngineService>
      custom_filters_service_;
  std::unique_ptr<brave_shields::AdBlockEngineService>
      default_service_;
  std::unique_ptr<brave_shields::AdBlockSubscriptionServiceManager>
      subscription_service_manager_;

  std::unique_ptr<brave_shields::AdBlockCustomFiltersSourceProvider>
      custom_filters_source_provider_;
  std::unique_ptr<brave_shields::AdBlockDefaultSourceProvider>
      default_source_provider_;

  base::WeakPtrFactory<AdBlockService> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(AdBlockService);
};

// Registers the local_state preferences used by Adblock
void RegisterPrefsForAdBlockService(PrefRegistrySimple* registry);

}  // namespace brave_shields

#endif  // BRAVE_COMPONENTS_BRAVE_SHIELDS_BROWSER_AD_BLOCK_SERVICE_H_
