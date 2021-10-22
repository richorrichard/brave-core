/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <utility>

#include "base/task/thread_pool/thread_pool_instance.h"
#include "bat/ledger/global_constants.h"
#include "bat/ledger/internal/common/security_util.h"
#include "bat/ledger/internal/common/time_util.h"
#include "bat/ledger/internal/constants.h"
#include "bat/ledger/internal/core/bat_ledger_context.h"
#include "bat/ledger/internal/ledger_impl.h"
#include "bat/ledger/internal/legacy/media/helper.h"
#include "bat/ledger/internal/legacy/static_values.h"
#include "bat/ledger/internal/publisher/publisher_status_helper.h"
#include "bat/ledger/internal/sku/sku_factory.h"
#include "bat/ledger/internal/sku/sku_merchant.h"

using std::placeholders::_1;

namespace ledger {

LedgerImpl::LedgerImpl(LedgerClient* client)
    : ledger_client_(client),
      context_(std::make_unique<BATLedgerContext>(this)),
      promotion_(std::make_unique<promotion::Promotion>(this)),
      publisher_(std::make_unique<publisher::Publisher>(this)),
      media_(std::make_unique<braveledger_media::Media>(this)),
      contribution_(std::make_unique<contribution::Contribution>(this)),
      wallet_(std::make_unique<wallet::Wallet>(this)),
      database_(std::make_unique<database::Database>(this)),
      report_(std::make_unique<report::Report>(this)),
      sku_(sku::SKUFactory::Create(this, sku::SKUType::kMerchant)),
      state_(std::make_unique<state::State>(this)),
      api_(std::make_unique<api::API>(this)),
      recovery_(std::make_unique<recovery::Recovery>(this)),
      bitflyer_(std::make_unique<bitflyer::Bitflyer>(this)),
      gemini_(std::make_unique<gemini::Gemini>(this)),
      uphold_(std::make_unique<uphold::Uphold>(this)),
      backup_restore_(std::make_unique<vg::BackupRestore>(this)) {
  DCHECK(base::ThreadPoolInstance::Get());
  set_ledger_client_for_logging(ledger_client_);
}

LedgerImpl::~LedgerImpl() = default;

BATLedgerContext* LedgerImpl::context() const {
  return context_.get();
}

LedgerClient* LedgerImpl::ledger_client() const {
  return ledger_client_;
}

state::State* LedgerImpl::state() const {
  return state_.get();
}

promotion::Promotion* LedgerImpl::promotion() const {
  return promotion_.get();
}

publisher::Publisher* LedgerImpl::publisher() const {
  return publisher_.get();
}

braveledger_media::Media* LedgerImpl::media() const {
  return media_.get();
}

contribution::Contribution* LedgerImpl::contribution() const {
  return contribution_.get();
}

wallet::Wallet* LedgerImpl::wallet() const {
  return wallet_.get();
}

report::Report* LedgerImpl::report() const {
  return report_.get();
}

sku::SKU* LedgerImpl::sku() const {
  return sku_.get();
}

api::API* LedgerImpl::api() const {
  return api_.get();
}

database::Database* LedgerImpl::database() const {
  return database_.get();
}

bitflyer::Bitflyer* LedgerImpl::bitflyer() const {
  return bitflyer_.get();
}

gemini::Gemini* LedgerImpl::gemini() const {
  return gemini_.get();
}

uphold::Uphold* LedgerImpl::uphold() const {
  return uphold_.get();
}

vg::BackupRestore* LedgerImpl::backup_restore() const {
  return backup_restore_.get();
}

void LedgerImpl::LoadURL(
    type::UrlRequestPtr request,
    client::LoadURLCallback callback) {
  DCHECK(request);
  if (IsShuttingDown()) {
    BLOG(1, request->url + " will not be executed as we are shutting down");
    return;
  }

  if (!request->skip_log) {
    BLOG(5, UrlRequestToString(request->url, request->headers, request->content,
                               request->content_type, request->method));
  }

  ledger_client_->LoadURL(std::move(request), callback);
}

void LedgerImpl::StartServices() {
  DCHECK(ready_state_ == ReadyState::kInitializing);

  publisher()->SetPublisherServerListTimer();
  contribution()->SetReconcileTimer();
  promotion()->Refresh(false);
  contribution()->Initialize();
  promotion()->Initialize();
  api()->Initialize();
  recovery_->Check();
  backup_restore()->Start();
}

void LedgerImpl::Initialize(bool execute_create_script,
                            ResultCallback callback) {
  if (ready_state_ != ReadyState::kUninitialized) {
    BLOG(0, "Ledger already initializing");
    callback(type::Result::LEDGER_ERROR);
    return;
  }

  ready_state_ = ReadyState::kInitializing;
  InitializeDatabase(execute_create_script, callback);
}

void LedgerImpl::InitializeDatabase(bool execute_create_script,
                                    ResultCallback callback) {
  DCHECK(ready_state_ == ReadyState::kInitializing);

  ResultCallback finish_callback =
      std::bind(&LedgerImpl::OnInitialized, this, _1, std::move(callback));

  auto database_callback = std::bind(&LedgerImpl::OnDatabaseInitialized,
      this,
      _1,
      finish_callback);
  database()->Initialize(execute_create_script, database_callback);
}

void LedgerImpl::OnInitialized(type::Result result, ResultCallback callback) {
  DCHECK(ready_state_ == ReadyState::kInitializing);

  if (result == type::Result::LEDGER_OK) {
    StartServices();
  } else {
    BLOG(0, "Failed to initialize wallet " << result);
  }

  while (!ready_callbacks_.empty()) {
    auto callback = std::move(ready_callbacks_.front());
    ready_callbacks_.pop();
    callback();
  }

  ready_state_ = ReadyState::kReady;

  callback(result);
}

void LedgerImpl::OnDatabaseInitialized(type::Result result,
                                       ResultCallback callback) {
  DCHECK(ready_state_ == ReadyState::kInitializing);

  if (result != type::Result::LEDGER_OK) {
    BLOG(0, "Database could not be initialized. Error: " << result);
    callback(result);
    return;
  }

  auto state_callback = std::bind(&LedgerImpl::OnStateInitialized,
      this,
      _1,
      callback);

  state()->Initialize(state_callback);
}

void LedgerImpl::OnStateInitialized(type::Result result,
                                    ResultCallback callback) {
  DCHECK(ready_state_ == ReadyState::kInitializing);

  if (result != type::Result::LEDGER_OK) {
    BLOG(0, "Failed to initialize state");
    return;
  }

  callback(type::Result::LEDGER_OK);
}

void LedgerImpl::CreateWallet(ResultCallback callback) {
  WhenReady(
      [this, callback]() { wallet()->CreateWalletIfNecessary(callback); });
}

void LedgerImpl::OneTimeTip(const std::string& publisher_key,
                            double amount,
                            ResultCallback callback) {
  WhenReady([this, publisher_key, amount, callback]() {
    contribution()->OneTimeTip(publisher_key, amount, callback);
  });
}

void LedgerImpl::OnLoad(type::VisitDataPtr visit_data, uint64_t current_time) {
  if (!IsReady() || !visit_data || visit_data->domain.empty()) {
    return;
  }

  auto iter = current_pages_.find(visit_data->tab_id);
  if (iter != current_pages_.end() &&
      iter->second.domain == visit_data->domain) {
    return;
  }

  if (last_shown_tab_id_ == visit_data->tab_id) {
    last_tab_active_time_ = current_time;
  }

  current_pages_[visit_data->tab_id] = *visit_data;
}

void LedgerImpl::OnUnload(uint32_t tab_id, uint64_t current_time) {
  if (!IsReady())
    return;

  OnHide(tab_id, current_time);
  auto iter = current_pages_.find(tab_id);
  if (iter != current_pages_.end()) {
    current_pages_.erase(iter);
  }
}

void LedgerImpl::OnShow(uint32_t tab_id, uint64_t current_time) {
  if (!IsReady())
    return;

  last_tab_active_time_ = current_time;
  last_shown_tab_id_ = tab_id;
}

void LedgerImpl::OnHide(uint32_t tab_id, uint64_t current_time) {
  if (!IsReady())
    return;

  if (!state()->GetAutoContributeEnabled()) {
    return;
  }

  if (tab_id != last_shown_tab_id_ || last_tab_active_time_ == 0) {
    return;
  }

  auto iter = current_pages_.find(tab_id);
  if (iter == current_pages_.end()) {
    return;
  }

  const std::string type = media()->GetLinkType(iter->second.tld, "", "");
  uint64_t duration = current_time - last_tab_active_time_;
  last_tab_active_time_ = 0;

  if (type == GITHUB_MEDIA_TYPE) {
    base::flat_map<std::string, std::string> parts;
    parts["duration"] = std::to_string(duration);
    media()->ProcessMedia(parts, type, iter->second.Clone());
    return;
  }

  publisher()->SaveVisit(iter->second.tld, iter->second, duration, true, 0,
                         [](type::Result, type::PublisherInfoPtr) {});
}

void LedgerImpl::OnForeground(uint32_t tab_id, uint64_t current_time) {
  if (!IsReady())
    return;

  if (last_shown_tab_id_ != tab_id) {
    return;
  }

  OnShow(tab_id, current_time);
}

void LedgerImpl::OnBackground(uint32_t tab_id, uint64_t current_time) {
  if (!IsReady())
    return;

  OnHide(tab_id, current_time);
}

void LedgerImpl::OnXHRLoad(
    uint32_t tab_id,
    const std::string& url,
    const base::flat_map<std::string, std::string>& parts,
    const std::string& first_party_url,
    const std::string& referrer,
    type::VisitDataPtr visit_data) {
  if (!IsReady())
    return;

  std::string type = media()->GetLinkType(url, first_party_url, referrer);
  if (type.empty()) {
    return;
  }
  media()->ProcessMedia(parts, type, std::move(visit_data));
}

void LedgerImpl::OnPostData(
    const std::string& url,
    const std::string& first_party_url,
    const std::string& referrer,
    const std::string& post_data,
    type::VisitDataPtr visit_data) {
  if (!IsReady())
    return;

  std::string type = media()->GetLinkType(url, first_party_url, referrer);

  if (type.empty()) {
    return;
  }

  if (type == TWITCH_MEDIA_TYPE) {
    std::vector<base::flat_map<std::string, std::string>> twitchParts;
    braveledger_media::GetTwitchParts(post_data, &twitchParts);
    for (size_t i = 0; i < twitchParts.size(); i++) {
      media()->ProcessMedia(twitchParts[i], type, std::move(visit_data));
    }
    return;
  }

  if (type == VIMEO_MEDIA_TYPE) {
    std::vector<base::flat_map<std::string, std::string>> parts;
    braveledger_media::GetVimeoParts(post_data, &parts);

    for (auto part = parts.begin(); part != parts.end(); part++) {
      media()->ProcessMedia(*part, type, std::move(visit_data));
    }
    return;
  }
}

void LedgerImpl::GetActivityInfoList(uint32_t start,
                                     uint32_t limit,
                                     type::ActivityInfoFilterPtr filter,
                                     PublisherInfoListCallback callback) {
  WhenReady([this, start, limit, filter = std::move(filter),
             callback]() mutable {
    database()->GetActivityInfoList(start, limit, std::move(filter), callback);
  });
}

void LedgerImpl::GetExcludedList(PublisherInfoListCallback callback) {
  WhenReady([this, callback]() { database()->GetExcludedList(callback); });
}

void LedgerImpl::SetPublisherMinVisitTime(int duration) {
  WhenReady(
      [this, duration]() { state()->SetPublisherMinVisitTime(duration); });
}

void LedgerImpl::SetPublisherMinVisits(int visits) {
  WhenReady([this, visits]() { state()->SetPublisherMinVisits(visits); });
}

void LedgerImpl::SetPublisherAllowNonVerified(bool allow) {
  WhenReady([this, allow]() { state()->SetPublisherAllowNonVerified(allow); });
}

void LedgerImpl::SetPublisherAllowVideos(bool allow) {
  WhenReady([this, allow]() { state()->SetPublisherAllowVideos(allow); });
}

void LedgerImpl::SetAutoContributionAmount(double amount) {
  WhenReady([this, amount]() { state()->SetAutoContributionAmount(amount); });
}

void LedgerImpl::SetAutoContributeEnabled(bool enabled) {
  WhenReady([this, enabled]() { state()->SetAutoContributeEnabled(enabled); });
}

uint64_t LedgerImpl::GetReconcileStamp() {
  if (!IsReady())
    return 0;

  return state()->GetReconcileStamp();
}

int LedgerImpl::GetPublisherMinVisitTime() {
  if (!IsReady())
    return 0;

  return state()->GetPublisherMinVisitTime();
}

int LedgerImpl::GetPublisherMinVisits() {
  if (!IsReady())
    return 0;

  return state()->GetPublisherMinVisits();
}

bool LedgerImpl::GetPublisherAllowNonVerified() {
  if (!IsReady())
    return false;

  return state()->GetPublisherAllowNonVerified();
}

bool LedgerImpl::GetPublisherAllowVideos() {
  if (!IsReady())
    return false;

  return state()->GetPublisherAllowVideos();
}

double LedgerImpl::GetAutoContributionAmount() {
  if (!IsReady())
    return 0;

  return state()->GetAutoContributionAmount();
}

bool LedgerImpl::GetAutoContributeEnabled() {
  if (!IsReady())
    return false;

  return state()->GetAutoContributeEnabled();
}

void LedgerImpl::GetRewardsParameters(GetRewardsParametersCallback callback) {
  WhenReady([this, callback]() {
    auto params = state()->GetRewardsParameters();
    if (params->rate == 0.0) {
      // A rate of zero indicates that the rewards parameters have
      // not yet been successfully initialized from the server.
      BLOG(1, "Rewards parameters not set - fetching from server");
      api()->FetchParameters(callback);
      return;
    }

    callback(std::move(params));
  });
}

void LedgerImpl::FetchPromotions(FetchPromotionCallback callback) {
  WhenReady([this, callback]() { promotion()->Fetch(callback); });
}

void LedgerImpl::ClaimPromotion(const std::string& promotion_id,
                                const std::string& payload,
                                ClaimPromotionCallback callback) {
  WhenReady([this, promotion_id, payload, callback]() {
    promotion()->Claim(promotion_id, payload, callback);
  });
}

void LedgerImpl::AttestPromotion(const std::string& promotion_id,
                                 const std::string& solution,
                                 AttestPromotionCallback callback) {
  WhenReady([this, promotion_id, solution, callback]() {
    promotion()->Attest(promotion_id, solution, callback);
  });
}

void LedgerImpl::GetBalanceReport(type::ActivityMonth month,
                                  int year,
                                  GetBalanceReportCallback callback) {
  WhenReady([this, month, year, callback]() {
    database()->GetBalanceReportInfo(month, year, callback);
  });
}

void LedgerImpl::GetAllBalanceReports(GetBalanceReportListCallback callback) {
  WhenReady([this, callback]() { database()->GetAllBalanceReports(callback); });
}

type::AutoContributePropertiesPtr LedgerImpl::GetAutoContributeProperties() {
  if (!IsReady())
    return nullptr;

  auto props = type::AutoContributeProperties::New();
  props->enabled_contribute = state()->GetAutoContributeEnabled();
  props->amount = state()->GetAutoContributionAmount();
  props->contribution_min_time = state()->GetPublisherMinVisitTime();
  props->contribution_min_visits = state()->GetPublisherMinVisits();
  props->contribution_non_verified = state()->GetPublisherAllowNonVerified();
  props->contribution_videos = state()->GetPublisherAllowVideos();
  props->reconcile_stamp = state()->GetReconcileStamp();
  return props;
}

void LedgerImpl::RecoverWallet(const std::string& pass_phrase,
                               ResultCallback callback) {
  WhenReady([this, pass_phrase, callback]() {
    wallet()->RecoverWallet(pass_phrase, callback);
  });
}

void LedgerImpl::SetPublisherExclude(const std::string& publisher_id,
                                     type::PublisherExclude exclude,
                                     ResultCallback callback) {
  WhenReady([this, publisher_id, exclude, callback]() {
    publisher()->SetPublisherExclude(publisher_id, exclude, callback);
  });
}

void LedgerImpl::RestorePublishers(ResultCallback callback) {
  WhenReady([this, callback]() { database()->RestorePublishers(callback); });
}

void LedgerImpl::GetPublisherActivityFromUrl(
    uint64_t window_id,
    type::VisitDataPtr visit_data,
    const std::string& publisher_blob) {
  WhenReady([this, window_id, visit_data = std::move(visit_data),
             publisher_blob]() mutable {
    publisher()->GetPublisherActivityFromUrl(window_id, std::move(visit_data),
                                             publisher_blob);
  });
}

void LedgerImpl::GetPublisherBanner(const std::string& publisher_id,
                                    PublisherBannerCallback callback) {
  WhenReady([this, publisher_id, callback]() {
    publisher()->GetPublisherBanner(publisher_id, callback);
  });
}

void LedgerImpl::RemoveRecurringTip(const std::string& publisher_key,
                                    ResultCallback callback) {
  WhenReady([this, publisher_key, callback]() {
    database()->RemoveRecurringTip(publisher_key, callback);
  });
}

uint64_t LedgerImpl::GetCreationStamp() {
  if (!IsReady())
    return 0;

  return state()->GetCreationStamp();
}

void LedgerImpl::HasSufficientBalanceToReconcile(
    HasSufficientBalanceToReconcileCallback callback) {
  WhenReady(
      [this, callback]() { contribution()->HasSufficientBalance(callback); });
}

void LedgerImpl::GetRewardsInternalsInfo(
    RewardsInternalsInfoCallback callback) {
  WhenReady([this, callback]() {
    auto info = type::RewardsInternalsInfo::New();

    type::BraveWalletPtr wallet = wallet_->GetWallet();
    if (!wallet) {
      BLOG(0, "Wallet is null");
      callback(std::move(info));
      return;
    }

    // Retrieve the payment id.
    info->payment_id = wallet->payment_id;

    // Retrieve the boot stamp.
    info->boot_stamp = state()->GetCreationStamp();

    // Retrieve the key info seed and validate it.
    if (!util::Security::IsSeedValid(wallet->recovery_seed)) {
      info->is_key_info_seed_valid = false;
    } else {
      std::vector<uint8_t> secret_key =
          util::Security::GetHKDF(wallet->recovery_seed);
      std::vector<uint8_t> public_key;
      std::vector<uint8_t> new_secret_key;
      info->is_key_info_seed_valid = util::Security::GetPublicKeyFromSeed(
          secret_key, &public_key, &new_secret_key);
    }

    callback(std::move(info));
  });
}

void LedgerImpl::SaveRecurringTip(type::RecurringTipPtr info,
                                  ResultCallback callback) {
  WhenReady([this, info = std::move(info), callback]() mutable {
    database()->SaveRecurringTip(std::move(info), callback);
  });
}

void LedgerImpl::GetRecurringTips(PublisherInfoListCallback callback) {
  WhenReady([this, callback]() { contribution()->GetRecurringTips(callback); });
}

void LedgerImpl::GetOneTimeTips(PublisherInfoListCallback callback) {
  WhenReady([this, callback]() {
    database()->GetOneTimeTips(util::GetCurrentMonth(), util::GetCurrentYear(),
                               callback);
  });
}

void LedgerImpl::RefreshPublisher(const std::string& publisher_key,
                                  OnRefreshPublisherCallback callback) {
  WhenReady([this, publisher_key, callback]() {
    publisher()->RefreshPublisher(publisher_key, callback);
  });
}

void LedgerImpl::StartMonthlyContribution() {
  WhenReady([this]() { contribution()->StartMonthlyContribution(); });
}

void LedgerImpl::SaveMediaInfo(
    const std::string& type,
    const base::flat_map<std::string, std::string>& data,
    PublisherInfoCallback callback) {
  WhenReady([this, type, data, callback]() {
    media()->SaveMediaInfo(type, data, callback);
  });
}

void LedgerImpl::UpdateMediaDuration(uint64_t window_id,
                                     const std::string& publisher_key,
                                     uint64_t duration,
                                     bool first_visit) {
  WhenReady([this, window_id, publisher_key, duration, first_visit]() {
    publisher()->UpdateMediaDuration(window_id, publisher_key, duration,
                                     first_visit);
  });
}

void LedgerImpl::GetPublisherInfo(const std::string& publisher_key,
                                  PublisherInfoCallback callback) {
  WhenReady([this, publisher_key, callback]() {
    database()->GetPublisherInfo(publisher_key, callback);
  });
}

void LedgerImpl::GetPublisherPanelInfo(const std::string& publisher_key,
                                       PublisherInfoCallback callback) {
  WhenReady([this, publisher_key, callback]() {
    publisher()->GetPublisherPanelInfo(publisher_key, callback);
  });
}

void LedgerImpl::SavePublisherInfo(uint64_t window_id,
                                   type::PublisherInfoPtr publisher_info,
                                   ResultCallback callback) {
  WhenReady(
      [this, window_id, info = std::move(publisher_info), callback]() mutable {
        publisher()->SavePublisherInfo(window_id, std::move(info), callback);
      });
}

void LedgerImpl::SetInlineTippingPlatformEnabled(
    type::InlineTipsPlatforms platform,
    bool enabled) {
  WhenReady([this, platform, enabled]() {
    state()->SetInlineTippingPlatformEnabled(platform, enabled);
  });
}

bool LedgerImpl::GetInlineTippingPlatformEnabled(
    type::InlineTipsPlatforms platform) {
  if (!IsReady())
    return false;

  return state()->GetInlineTippingPlatformEnabled(platform);
}

std::string LedgerImpl::GetShareURL(
    const base::flat_map<std::string, std::string>& args) {
  if (!IsReady())
    return "";

  return publisher()->GetShareURL(args);
}

void LedgerImpl::GetPendingContributions(
    PendingContributionInfoListCallback callback) {
  WhenReady([this, callback]() {
    database()->GetPendingContributions(
        [this, callback](type::PendingContributionInfoList list) {
          // The publisher status field may be expired. Attempt to refresh
          // expired publisher status values before executing callback.
          publisher::RefreshPublisherStatus(this, std::move(list), callback);
        });
  });
}

void LedgerImpl::RemovePendingContribution(uint64_t id,
                                           ResultCallback callback) {
  WhenReady([this, id, callback]() {
    database()->RemovePendingContribution(id, callback);
  });
}

void LedgerImpl::RemoveAllPendingContributions(ResultCallback callback) {
  WhenReady([this, callback]() {
    database()->RemoveAllPendingContributions(callback);
  });
}

void LedgerImpl::GetPendingContributionsTotal(
    PendingContributionsTotalCallback callback) {
  WhenReady([this, callback]() {
    database()->GetPendingContributionsTotal(callback);
  });
}

void LedgerImpl::FetchBalance(FetchBalanceCallback callback) {
  WhenReady([this, callback]() { wallet()->FetchBalance(callback); });
}

void LedgerImpl::GetExternalWallet(const std::string& wallet_type,
                                   ExternalWalletCallback callback) {
  WhenReady([this, wallet_type, callback]() {
    if (wallet_type == "") {
      callback(type::Result::LEDGER_OK, nullptr);
      return;
    }

    if (wallet_type == constant::kWalletUphold) {
      uphold()->GenerateWallet([this, callback](type::Result result) {
        if (result != type::Result::LEDGER_OK &&
            result != type::Result::CONTINUE) {
          callback(result, nullptr);
          return;
        }

        auto wallet = uphold()->GetWallet();
        callback(type::Result::LEDGER_OK, std::move(wallet));
      });
      return;
    }

    if (wallet_type == constant::kWalletBitflyer) {
      bitflyer()->GenerateWallet([this, callback](type::Result result) {
        if (result != type::Result::LEDGER_OK &&
            result != type::Result::CONTINUE) {
          callback(result, nullptr);
          return;
        }

        auto wallet = bitflyer()->GetWallet();
        callback(type::Result::LEDGER_OK, std::move(wallet));
      });
      return;
    }

    if (wallet_type == constant::kWalletGemini) {
      gemini()->GenerateWallet([this, callback](type::Result result) {
        if (result != type::Result::LEDGER_OK &&
            result != type::Result::CONTINUE) {
          callback(result, nullptr);
          return;
        }

        auto wallet = gemini()->GetWallet();
        callback(type::Result::LEDGER_OK, std::move(wallet));
      });
      return;
    }

    NOTREACHED();
    callback(type::Result::LEDGER_OK, nullptr);
  });
}

void LedgerImpl::ExternalWalletAuthorization(
    const std::string& wallet_type,
    const base::flat_map<std::string, std::string>& args,
    ExternalWalletAuthorizationCallback callback) {
  WhenReady([this, wallet_type, args, callback]() {
    wallet()->ExternalWalletAuthorization(wallet_type, args, callback);
  });
}

void LedgerImpl::DisconnectWallet(const std::string& wallet_type,
                                  ResultCallback callback) {
  WhenReady([this, wallet_type, callback]() {
    wallet()->DisconnectWallet(wallet_type, callback);
  });
}

void LedgerImpl::GetAllPromotions(GetAllPromotionsCallback callback) {
  WhenReady([this, callback]() { database()->GetAllPromotions(callback); });
}

void LedgerImpl::GetAnonWalletStatus(ResultCallback callback) {
  WhenReady([this, callback]() { wallet()->GetAnonWalletStatus(callback); });
}

void LedgerImpl::GetTransactionReport(type::ActivityMonth month,
                                      int year,
                                      GetTransactionReportCallback callback) {
  WhenReady([this, month, year, callback]() {
    database()->GetTransactionReport(month, year, callback);
  });
}

void LedgerImpl::GetContributionReport(type::ActivityMonth month,
                                       int year,
                                       GetContributionReportCallback callback) {
  WhenReady([this, month, year, callback]() {
    database()->GetContributionReport(month, year, callback);
  });
}

void LedgerImpl::GetAllContributions(ContributionInfoListCallback callback) {
  WhenReady([this, callback]() { database()->GetAllContributions(callback); });
}

void LedgerImpl::SavePublisherInfoForTip(type::PublisherInfoPtr info,
                                         ResultCallback callback) {
  WhenReady([this, info = std::move(info), callback]() mutable {
    database()->SavePublisherInfo(std::move(info), callback);
  });
}

void LedgerImpl::GetMonthlyReport(type::ActivityMonth month,
                                  int year,
                                  GetMonthlyReportCallback callback) {
  WhenReady([this, month, year, callback]() {
    report()->GetMonthly(month, year, callback);
  });
}

void LedgerImpl::GetAllMonthlyReportIds(
    GetAllMonthlyReportIdsCallback callback) {
  WhenReady([this, callback]() { report()->GetAllMonthlyIds(callback); });
}

void LedgerImpl::ProcessSKU(const std::vector<type::SKUOrderItem>& items,
                            const std::string& wallet_type,
                            SKUOrderCallback callback) {
  WhenReady([this, items, wallet_type, callback]() {
    sku()->Process(items, wallet_type, callback);
  });
}

void LedgerImpl::Shutdown(ResultCallback callback) {
  if (!IsReady()) {
    callback(type::Result::LEDGER_ERROR);
    return;
  }

  ready_state_ = ReadyState::kShuttingDown;
  ledger_client_->ClearAllNotifications();

  wallet()->DisconnectAllWallets([this, callback](type::Result result) {
    BLOG_IF(
      1,
      result != type::Result::LEDGER_OK,
      "Not all wallets were disconnected");
    auto finish_callback = std::bind(&LedgerImpl::OnAllDone,
        this,
        _1,
        callback);
    database()->FinishAllInProgressContributions(finish_callback);
  });
}

void LedgerImpl::OnAllDone(type::Result result, ResultCallback callback) {
  database()->Close(callback);
}

void LedgerImpl::GetEventLogs(GetEventLogsCallback callback) {
  WhenReady([this, callback]() { database()->GetLastEventLogs(callback); });
}

void LedgerImpl::RestoreVirtualGrants(RestoreVirtualGrantsCallback callback) {
  WhenReady([this, callback = std::move(callback)]() mutable {
    backup_restore()->Restore(
        R"(
{
    "backed_up_at": 1635415527,
    "vg_bodies": [
        {
            "batch_proof": "ovU8fk7Ikp9TRe+yuDdLqPCyXb5m7CybSo9/mGTGpwwwTofnZI7H9wFj7CE856KyTlNqBsAK4KRkJXfgv0tJDQ==",
            "blinded_creds": "[\"EmmPv7RQ3Hs62aU5mAjpYbVyIgEPjZfIU1OgCM3CxAw=\",\"upPzUFLciSXesE+IvZsgaT9HVkT+n/5GZRoEv0e6ZDM=\",\"iK0nwJvm/GFOUOM5/4mwoi+SQ0HLLo5VpkwNHMfOIig=\",\"uvL2ei9zwF37s2gE76K3WJuFO9b4RVQZMpE15bq3fjQ=\",\"BrF1nMorw3rhJkJzboKtr1hscYdAahPgZEJar2heYjs=\",\"4HD0igFXuXJwDVwCsnXDx/wHdGlCaHALF3KBuoUgYHY=\",\"2F8kHHrrJrUdH1Gl1yzmqI0owvKmR7crIY6HEi5qSQE=\",\"FkmMPdsHtyioJzda3Y+zSMTv55VfQuDbO3YfxKo1XQA=\",\"SgcMemOTU/zMBJ0zHsw7S79VrzQGwX8DaOp+W6HKH0I=\",\"BAoJuA+Une+FFe1LrvHEXWUgPAyypBHEINm6HCFUNFg=\",\"6ODatjXF8xkIBK7+RNgXJMdhT9JIOSyCMzu2c6thMwg=\",\"JP9zfH0zgofzhGhmXpeye9s7ac6oooXGDsnKcmxum1U=\",\"qrNTZtVtqAMmVNPjYRf/DuCood3Ig1O+cgGSec62xgE=\",\"4LvfWO4eLx23UNkZSfwdMQBTFX/lH+uPDDgIdkmzW30=\",\"yuN4uAaxl061C7XIGVGhQfuOSoeyCX9vX1TyLH9sXRQ=\",\"HrzB9R3Ys4DVBM4qtVsU6ndtPzgq8hjlULSc0TbO8i4=\",\"iOuSg5L3AFXjkhSx+RuJmfAgy1v0MDXO9NlkdIORaBE=\",\"PBKHbfIrXLO6luaB1uXn3PNSmoF8DaUma9BNdHUU6iE=\",\"vE2Hr15s2g+tp2uIFXhzFRLkGt+rfmbLjOMxO4ohhAQ=\",\"ULtH5nmMAArPU3d2LMo2Ir6vMrhZSGtT/MDTegsdTyU=\",\"iqc+ZsGxZrA7sCHX283tgNC+zRYUNGp5pFrIl9ygxCs=\",\"Nk1MT47IacbPklA00h42gfQdxlALpqYPNOedwhv3KDg=\",\"wv70r8DXrENZVcJ6+HMPo88vyvHueC4B3f7tC+bV/Cc=\",\"oPvrBpClL0xuo9VV2LPHLiTQSlP8p+x5XPqFqmfIhGo=\",\"SFtUM7+QKz+IxbibdOMZgopYxDukC4Z8sF0CbK47Iwg=\",\"qJbv3U3EN5arBGzfFygQ8IY0KTShq24rFF9j1x3/7UA=\",\"TIRDxQ0bJupU4Jiel1qx2p8ICddpG09yB7C0Egld83s=\",\"3ntOaqoTdhGNUYwFU7Rm0QKLw9+9stUE9fpuGmC6amg=\",\"FuRNPleqhyGir2jYNBX2iagzu9X//I7wJOF27jhxym0=\",\"vBhc3nPUvnidYCaC/T42RZsnNvXjT8dNi6iLvK8e8FQ=\",\"DCU6tyK9LKwCYUHOFhtejoorKok773wdQMVmLl5LQwM=\",\"oAtPMBZ3BAqc/qsLTZaweoIATju3AE+hox9oYiMgDk4=\",\"yGXXAMZQ+wgMnr4BaXpTYVhBc+Kv5uRs1vtBVNZ3oxg=\",\"RAS6hSqrvl0nWiFYQyYiyCaTMygRr/xVWiXYF4Qjbj4=\",\"tj4aF35lZ2QTl2Ej7EQciVD8jPStxXM3IR3FQRRXeEw=\",\"IHA9Sp4j0lZL3FBFT29vKsPtHWhGDUk0jooUiXBPOjs=\",\"cC+k2XvdJD6naaCqaRTo+Z4vvzzSRB0tauFQsQrLcEQ=\",\"EqMr3JsSAdQem3EKokcJrxokWQBDEg5wZQw9+TZ92Q0=\",\"umeVbGSGpfnPdELCvbbeOyFCwt6vojRLSfmO31jDalU=\",\"7KjshTIkf+290FPjRDkgi8XT/PVTqGZPHfXwfYkJzS8=\",\"hE/57IIC+wp5uJEecyuy80kU1IlBN69Q9MUaNIMEH2w=\",\"vHT1mpZzNtx2yWY8+KIUI7vsv36kO/SHy6XAJ4/zXRI=\",\"rrX+L5tcwhuacVFDkArxAIjl1VCnWHKaC6OeXgqMDVk=\",\"kvLPy6g7iiyxgKg5NYwNJ10LLMZK+DuzhhnY9u6q+hA=\",\"CLa9CIDkIEoD08a32ebjDGv3RE7rqlJMshAhP4zJbzY=\",\"zN4+4qZEbnR0UfYEU4OYP63B/xpwSkXZMmLvLYoB2z0=\",\"pleLXDjCr+dhz/Zi1A2bcsJ7GxusVBD/I/oPo02umQE=\",\"MMtu+aidFj1tZCnAtlTGBUGII8CIpSEEeu5ffcqBrRw=\",\"IpNCtqid5WWzRmFCuY8xGGuXAbiC5vo3nruiLlq3sTo=\",\"2O3+HqaRe+TDyX1dU3LN/X/CoemkEFCKQB0nyL+8kWs=\",\"/IlwH8/6yez/PnoR1X+cJMV7ABrVpoVPkXYGTqav5Bs=\",\"oHR4zDoX0HfJDIXWSNPHi5+HhhKdsFNbVTm5NE9aazI=\",\"Qkb2NR2NeO07elgaJgA4LMmMN2xDkycuiexS60Mttng=\",\"4AqeNfolctuqn80AUg9ozU+qn5vqIQHsdHfGd1vNsWU=\",\"Xjg/FXF3oJ6nPpTAPkY24W9CCbusK0ir5uQnvYdwhn0=\",\"6gpVjgswH2e/qR7WuJsmw+wEJVTXby4kdIhwfSBvXGs=\",\"SH+afQtefcH7TeZgyjOMxh0OKi9EEb1T8/Xi7R7OW0U=\",\"cgrfViq7bGRobcH+OmTMBnH24jB5ajk+XbL8Hd+Y/Uw=\",\"2ALryt2MWEtVU6/0oWq1/S87YChVPhA+/ELs1zRpRyg=\",\"nuabApKTmzGriUSSCiFDOSamQbSGxj7kfKNNDQXWr2Y=\",\"BmsuhvYH4pjQkNPbqbvdl8CMgXIszwAC8czaHPZVdls=\",\"MuTDnYoyyu34tTh1vNcnAXXHeeaq28zP71BEksX2oEI=\",\"rPy8OqFLIbRXIZxs5uonX197XK3MDasnfeA9MGb3pTQ=\",\"8jl7xerlifW/8aBgNFrjYNFdSbleN7XcC28P6nN83Cw=\",\"dpzT7NFBhpoN6AuJOZDGKWqC6gdGy+da9rZTPM6LkyI=\",\"av+2BXnPcV5m2sV/KyqIfSNZFvBiJ9ASgy7zO30bbEY=\",\"HMeHscn39MLe/IptL+x8DGrYZPu/WEvTK2ivyIY0cjA=\",\"4k/AYx5CDj0HSLiggmpS55cJlypx+Asuin8nQsYi6Qg=\",\"AvEqq7lXYDoFOOjOF9+KQcqKJUXw8y8GHhzpCAKQHFc=\",\"yhxSh787VWVnjdMRz99c61cC80d5N9Yryg5m42+tjmo=\",\"zBpNzKc9vODx1rDmmj0hvFEIHYkNW7Kye8IxfWyw4A0=\",\"YIL5S/vjVx7GTPMd+TQHB0Y4c7F5NXvzCLxaSf5BEWs=\",\"bBIjAhX3S+KWMZqwWDs10nmlqt0DELqWQoYPv3NF/Wc=\",\"FuoBQA053rZJJBNmRmjuh132q+ZDXbPjvZSFH/iq4TU=\",\"aMEwwnCcuzhdTIaYuRtr3IQ8sanSKfxswsAgiiLE91M=\",\"mCSPmDVU9jwUvbMZMfHG+jTjNFLvtOJvYsKK1Yde/QY=\",\"iHoa2kR89f/h28nf1kC2o0ojUdEiabIiJ0zTqlFSqDs=\",\"fm3S0PTujM+2cGwDZMxp/nL/8LtWGM+vAG1+DOLimHk=\",\"qm3js7XzImfwhmw1uyBwujxPV+wUMeoVDvvt3k5/LwM=\",\"qE5QyShY0GpIcGOm/SBCfwervEO/cUx3EZi/2UpmNGg=\",\"9DFHoE/3o7sIZL56IHFwfhqiidUI2BMsSOcyYtSs4H8=\",\"UojVRwjc+rRK1PbW5xY26r58T88MnogQCzBNRW1KzVI=\",\"zGiaBJ3fGH8iEyIP8JaFSN2DENELhfomnjVySeBMkVs=\",\"mNUfl3BE4awywPUkUMx9QV141mue+T1oEt6kMHo9i3I=\",\"gmgIubze+e0N8l+jrRA0Fh8z8+mt6kPVsd/V0jnKMHQ=\",\"hrqGisCVXk9d1MYvblFYwsoHelMSDqOzkzVUbO6YSBI=\",\"qB6/hJt3qeg1yyfs7kKFxbho7XqTcNsxcelz9knCghw=\",\"CBzA7K2X2dbN1k60LUWV5N05Wwjvjw5mwxCCPAGbgEk=\",\"5NQyGVBmC8w2EXMuJlFH/UZwOWPNuZmRdKafseYgiw8=\",\"WjcvvcDi6Y7x870QWTWZK1BFezwiLCri8HlYRY5wpzg=\",\"wBCB5jOORTJAefzz+VbEIWzYWLPp97Rcvcy5hUPf8wg=\",\"tuQEe24DLz04zsYqEUVVT3j6+6CvwAXpgu2/0OMVBlU=\",\"vvExjU4+7GLxDzhhUupgrKilavSas3ZWI5qeRyChdQY=\",\"PLOWlamnic1Pu6GtR/cV4E/dg+iDLhGgYOHazjEuiwo=\",\"jA10K5OVk8f+S2q1BYIBMetqlCrYsf+dkgtldCiCSwI=\",\"TFxTd8+2UwCZl2lEl1gCQLD6hcacjykShs6pZ2S5Sk0=\",\"To1cYl2X/wxdxQAb//X83qKjutNiZUmFdMLfByKa+Rk=\",\"mh876uxOLCL8Q+8FcEbxk2wz0Q60SnVECFrRZ/ZWJwM=\",\"Hu3CSVcibnowIpVOnns96ikB0pesXUIdIZoDUJ9trzg=\",\"KMR+ykvoy9XjBLPP94CTpud/Ksq5dv/8j6LTVN8kZg4=\",\"FrPSF1rUV0xgmEut94RHaSJkKzA+NsV7ES/zicp9BXk=\",\"tih0/5mUXskOwSwNlfVbv5Eg6t6JAhPYh9ARt4bJHjs=\",\"bAsVRl8xCllTH1bk4BMhN1oXwaoD79bA2UUyuXIzwn4=\",\"eNsSKciHtPRWEiQ/slItj3OH4xLgYjK3Tl5XwE5lGmo=\",\"YApoD2qQAxTyKKMGn4bkbGJ3yPjwb9pEK/XFtOSGqDc=\",\"5F4v0+ItoM7LNGn1VGi6vNT1pumeJPqrsyMEWnPde34=\",\"GjgGe97ZziRgnu1sfn9dDR2pt7WbX5Uhr79gyAwo1AQ=\",\"sgZlVlUmoEVBgU95EZgXmjjUVSa9F5dT7YVgmnr2KA8=\",\"Gg/sG9sY2LbMlJXNAOIVE30zPOi0ew3Sq8pBvc4noAY=\",\"ptSvroaKJnL1k9nnieKzTxfIP36z2kLGn9uNQMdzkHg=\",\"+AWHA5J/SnXEOMNflLIqEOzCgoPRCRz+FH3uJldzdSc=\",\"WBE57/QWdhEHGC7wuuFjCYPpkzxLQghiMaqmb4YFsU0=\",\"sFPKh6TUx9JwBMvK8H7KiKNtNW+8jyXo6mDUOF1VLh0=\",\"6qmjJ+r/0QjLqE+MrE4/vaeesUiLB49ewZ1nr5lwqyk=\",\"9AcUNNcdRsbenSmSNuGXFTJStWqJ7yFB3hwBvNDrcX8=\",\"Wkul35akMwTIt4tKXmfMQ9H/rxYRn8hPRolvATSh2z8=\",\"xPLFAVmAsSGqJl+3AfZjpqYYWlIAMBt760hHiPsloB0=\",\"eO2E1a7aVbziErIu3uUrssBE7i6+YvyX9kAlxy4SqWY=\",\"+AM1R9ZU4xdUlVogU2mHD2KvUlkTijNF4vtvJUbZABM=\",\"rre0mEd4fady3+GiEbFTIAw8tBPa1+o/l0lHDR3xUhs=\"]",
            "creds": "[\"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\"]",
            "creds_id": "95968f20-2bb9-4702-87bd-24170b4a0266",
            "public_key": "6AphTvx13IgxVRG1nljV2ql1Y7yGUol6yrVMhEP85wI=",
            "signed_creds": "[\"uIp6fJkYbAE8ZuqBITC2IwrCUVk1vBOhjsWFXDOXXRg=\",\"6HQvo6hngaghjGMUgwW/P9exvfU41qspw4HYaPp3bU8=\",\"gvHwlgLyFegQmzyWkaHX5LbsEA3D+Pk1bs4d23KxCno=\",\"tJJwdGQHCnoAmQw+xI6SF9vyAQFfp5vuIcKZ024rzxM=\",\"SC6RfBwloISdmDWu0qleI4lgfcOE4usP9NUoGdpo6XY=\",\"aOxuU860ofZ5aicrzqVy3++YNMmx8y45PlBnPi7r/Xo=\",\"qrXHq7yIWECeXWeBpjZ3NI+UxlUU5PJeZwr/TKh1ah8=\",\"Pg2l1mK8T0csBgVLcGzyQFGzTPkjZHq/EE3KLBOcfSU=\",\"xg7llkLDslcUN2qCEW04FmpR96FhBWDVEMUeO1BtLhM=\",\"8i11uD6cWtrQozda8nMxACHP4YpzrHube+lM/s5gUD0=\",\"Dvw+PXaTURCFbWVGIDjNJ3RjY0ky9nn7hU7ToUaHLGI=\",\"dHk5O0+ms2HZ2TZUSzwKVmmPg3xITwOUrPk2iHm5IUg=\",\"slGfkBz9+YKoHW1B1FZmW1NUFOgv3P0WE4CTTTKlE2s=\",\"psd1bhYTqSmTmiC4Lxi19APLp8gQ7/AHOvHAdJBeyF0=\",\"VhRv58RpsXQgYdQ0fKNh3fT8Scz4f9peAa0s+I+vGUM=\",\"fvPeEjmq6hTVJL8UsoTUQiQZ5tkXQnbRbHG5isHzxUM=\",\"astZJWduQD4H5lcJOmEAoDKnYBAZsgl/pegjQZnTfhI=\",\"djV4ypsCQ9S7MrABE8t5YMx8rQ2GV3fEMhlAYPYdpEU=\",\"5ro0vca1cpbnK41QAElrJKM/KNlXmPQvxK9fAGoXDx0=\",\"bgm+CFyVcgs3aGOQ7bpEZ3QPTANFIunzWV5x8qgYsAc=\",\"5tW0q1rGVPxofhFzplxev+FIH//mfiJpohvZ8T+32R4=\",\"nog4XrMdamJBAatiWyvavSn15BU3nHqKtcscMK/UgzA=\",\"WtgyGrISHW6VlNhje+qIIV4g9y5RiuHZursUapqRLV0=\",\"elaL2egphs7NUr5zdXTbzZyzYZvTO5nEbY7DrZ4H9B8=\",\"+HQffq/NK+jMPgSZhbXfd4PyLcBC3PuqfRRmppTDHzY=\",\"YMnAeSpH8b5c/CdAvBmKEhMoZSclW3DbxhU+bm7S0WY=\",\"VCrrvEKdiwXMRlCpXHPBKXBmOq/ynmptHy5MwVOJXE8=\",\"huItBeiFqOGsT8MIqWnk9xW8fEunTvrW+swQwHwh8HQ=\",\"0ktWIKv9pBAte6x+TFyCRUvBiob7/PAXqGzwAvr3CFI=\",\"DJUcD4sEmpJ6EtD7iaQlM/UFtaKNJm5CrhnOxiKaVV0=\",\"HINFzjxKpSHyn0qGQgmmUz3l6bgDVQyKB2D1JtJ73Go=\",\"aAvB4fXRLilTAw2X/CWcfls8L2JUWzTvmnVN7S6e3Fw=\",\"Dlc82SPkJsQGJrRpHctpJEN8xbIzLE31Od6bU+Lgh1o=\",\"ci0/PvQwfIfrEvghSnn2TX6q+HAp77j3Fj+CuaZq4Ao=\",\"BKPwFjsISOlsCF/xcaRugBgrrZV6MqfosTnfoYNZrDg=\",\"UtVAHXhnH7uAhsOwSuHDXUxIAYyMrV1fHEfcT7olIh4=\",\"Bk4E0TZGrY3V2KOZ09pkBZKCWwqJTdxLz7YtkvTQTxA=\",\"ng/Lzsfs03TisQCk27foYJJyX5KVx1rv1HNtC+U2cEc=\",\"2PBedKq7aGJ8Qae2prz2H3mCi6K72P2vs9UZFwPkSQU=\",\"WEzXauHhLqQcxBq16F10mdbpRBofZXAPhXHdTKvo0hY=\",\"6hoMAUoLJjhaqPG+N5HrX3ZUaImd74suEvJwbqk0sng=\",\"HMPTwUOxamO4V6l/xDmYY2djSmVtN3MdL/DygqpkAFc=\",\"eBmqYHtKKtXYUzh1yudj9kiypN+Y1NspYKmOwDNvkDc=\",\"8owpOPgzFhXiJDcmXJZae4eZ4WkjZ8jOSu4wDbEbuT0=\",\"4HOfhPh+dw2RftB3x3Vz8BjGXFfXIJzCWE41o7bf9Ro=\",\"PDyL+23isigltTPTJZ2qPufTDHnpv+wkUW66jX46lg4=\",\"+O+/Uqj/tPo1OVzgmqa9mQ/ABPxVkUqujKRkwThvP14=\",\"MgcJwYfGtbc5kRF8OfDPdIkac9U6OyBpvLdewatMFiw=\",\"xhCizZUgVo801TPRm2VscjzxgKo/PaomMlk5j3GHVzo=\",\"tCLAltR4qUQDTLLInVM91neIns97FwXxVpxNHmAOalM=\",\"4L9aFXTSvyRtSlI33d7xpxsQDF9X2xpuwgbdqRj8VlU=\",\"Qq76yjbRkU5ptVY5pm8q40jyKS4Xz+wCh47ZvGZUXW0=\",\"2DyoHsQQ22zFyfiW+4zuvKt/aTFGIgjcSCtx22OQ6T0=\",\"1I+/FKr4cXxmlkWYf1hOyRECHjp20+tJTIZjzHJ5yWw=\",\"oiHHlNSiV2JEIlO42MsrC6CBeQBBwOjPwQuKqOYNb0g=\",\"EKN2jkKLI39csAq382R2bAiV0ORlPFVHQK/M+F5W8jw=\",\"YH553NSW+MjdgDmchxRP5KFQeFQdoJT6w6MJOY4cPWM=\",\"9BQxdmuZ6KG4mx24i/vK1zbqTwpWHC8F8R10K9VPbgQ=\",\"zljTE7rcFXkuycjaXDY9s2B1VuEcSW5eQEl0DB1CVWY=\",\"vjbB+JMZM/ORqTxuVMIeW23OA69ovlZHzT6c0Nr4k1E=\",\"1HBxdeOV5vSD2s0DAIJcGp9dzvY4Q1i0Z92BYHWcvyw=\",\"ykfDD4VzPw3ofRMDVES51n7KMo8MruntLCygNJCUmhw=\",\"Uu5z5oLGYlXDPNEoAYid7x3QPNqkEZWtDUa+oJUBuho=\",\"6gzLPOnStF0Qv4c1nfLpXk/lZQEjjPGpnen3PlpYWG4=\",\"VEn0lSeOdADP7ulHtlve5PNq7kSdPm+O5wwtuW5e80U=\",\"mKL2hfa8/J8o9nQ2n4MEzA3hAxGp1RKOyDbavF8cu3A=\",\"Ho16jW8fGfTcNOyDf9TdxjjPscPBX+eL0dRpke0HTlk=\",\"7Gy3k7bR0adDPyqpxspHkapoZmtRJzAfIjgPKdCeFEY=\",\"QiFpMgCTqDH+4hLRCdGULMhJ2Sg3F5WYdTI55GHFnAE=\",\"vJZWuOMZqvH0Ypu9cIUWqqgfTzrHfFLPMhKEPuy8TjQ=\",\"kiRKsFwScKAOWs83zR0gNm16Mru97O4jUv7O+RBNuAM=\",\"ehIGR29zWCWXiVHcps/UpOfZQ0NndIXLb1l7FmxbZEc=\",\"tGPAW2GdbdURDsJh+z9uA8MQchzXE6vhzvt82OatxUU=\",\"ehwU+7BzrH4v8w72feGmFfD0GMW/d3Q3A7c1uhxkaHo=\",\"VqQeRgJ/WZRYmvqwBv9NmZfGhmp1n14G/FGQhl4EqSs=\",\"3vUB9Q4HtwSYjWJ94rMgCdn27kYjVNoCr+jgnstSZCo=\",\"cv1h2uynmfQFatxPS3/2zIkWmikWeYyfCz8BVtcZlXA=\",\"xEmMHwMOVnUlgIJtguV7vG98sY8FUuhUNWnXFUbJuxA=\",\"oiNbOo76QMdaV948yQNHJ/iY9CaHD7p/zX7W1lrzj3s=\",\"evocDX+I2Ea7HI7CX9A3iIGctHJXAIj+akrRqxgPTzg=\",\"EIECW8VUckn8zHNifaL9VVjIF+L7vxbf9AX+sJYiwlM=\",\"tjroBJJkdnhjscXXLAy+O/Df7wkEN5F5OaOnVIYh82Q=\",\"1uZkM1TfNZr8BjWy0Zte87gUrMeP5x4RKWrrI6qpHn8=\",\"HtrbKjPBThSbxZL6jTcKfuQ/C0zTyQ/VJsMi0jG0p2I=\",\"At503+yoe1h36dAvnr/4Pop0v2lVJhzSRh3PtC6IjE8=\",\"QhLM7wyWHDINi/xiAa95HayrrlhCdkiMbr5z8RFv/AU=\",\"2nDoU9SA8Rpwj7I4BNwsq3CDslCE09oFD0ge6/nU6T4=\",\"ZPbdXuVib17kgbeHK4KhcrXcteUbRvQlPE0Su5eiIhA=\",\"INNpth0oS4o02MktyjbUOacV1Ega4wq/IId0EpUpoX8=\",\"2AD225SHaHGLEHinA1kW6OetC/+QT0IMR+2i6FN3ty4=\",\"eNuA4xDSQ0vTMxo5SgmWKCJt3EI/kPROVI4fITC6LCw=\",\"TjkabGx21C0r8OPOFS1THZCI2Qivy3PUOMgwPwP/NDQ=\",\"Qud6cnKOwsI11TEfN98RCmVjHEo1SlIhO8Nfcw2oTlQ=\",\"WGCRHWCUAUk2law5347SDz0FgKUp1xBHoNDHn+Rd9Fw=\",\"2rghjvndTAPik7adTkiFYXxuSWqsBnXlnj6ZnChlOXU=\",\"PuvXuDa4+S4nIYW3ELXPKX+qtpzj3xzz45/xya46m1w=\",\"UGC9I1A8EpBisliZqxiXuLgX/FIMqIsq1ESQiLSi6SU=\",\"av85cLFIHqVe/j84Cfygk5mSnSHLCfJqwYRYs1DziG4=\",\"RnR5IXUZTvadeG8HT7zNIEUP/1iTb/I2iL6vKp1vjDE=\",\"JsEYnAnarZMTejZjGHJE5XRSLZDC9M94CnOWx3/mFEQ=\",\"kPaU4cPubl3xsrKwft3+O7AU8aXGAFYq+MwNol5YfQ0=\",\"DsrleOuPgEkig2IvU+oWx5kmLjT1nVSDEfj0ln3AHFI=\",\"wlLXQEeo1CiqDcYSufxBb7ZVejwPuewwACvvHFwynE0=\",\"LskgJxkoHjsJWNVOYR6FoM03+oo9NPTEPAK3tUdzJSU=\",\"zhAfLNsZ1OtoZh9VpnqptvoGYqk3bJi2uPqD1SKR/gg=\",\"3K5Eqa/jTTfK7FTqAOy8yiMiawaNL2U+vyw3Kw8/fQA=\",\"DtIQ4yjzTNDine/W8aqeJqRd6UunW74hYM7p8oGg43w=\",\"ZgHFcitXULCkOsZz7TIXSXscimRwNYHGD44MxtinjFI=\",\"uoHdqwVhQccpnomHhpurPc7SSUa0VUJldE4SYNcfsVY=\",\"moqn/54ZplB1rqCClDnbHGE8GuTZGwvyHcb0hk0WZmc=\",\"AjVnzmVjOyxM4rBfoGl3u9imOXBlY51i4m84fP8yXBM=\",\"toY/vNbXY/1DAg1Acp4EKAa9D0PMtq6iAp97yNJlPkc=\",\"TmrBI14ZHAvN4hB/2TEFGprGjlsIkUpTNiQtMsL1DV0=\",\"opQH5pc9bMXgEe2Ktk+7WmrD398rA7kp5Gy8T/kH7zk=\",\"tN86oTIcEIb9/3sfX3a4Ulip4Oc7lcAVvBuGz4b1YnY=\",\"YprAltBFjxbgya7vosOXX4Kd9Pn6k3GugDSVXL2v83I=\",\"fCrTZDpbm47HW44Jseg0fB66vHTYjMovn7tNzpyOIF0=\",\"IM1LHDGMndGwaVugjL+3m/d95uPuCAn+NctbitGQtBU=\",\"PPTdRlzz/T0DhBSI0Zt/Dowb/QBf4UcG94BNlT+KsDA=\",\"zHi6RkDANwAqZwM1uWD20axX1iP+srSilAw3RygZ8yE=\"]",
            "status": 4,
            "tokens": [
                {
                    "expires_at": 1639220745,
                    "token_id": 1,
                    "token_value": "Zq8IgE+mWxb5ZIqw2Wrj0gjFXFhNI43hUIukr6lsP/FB+H6kWEYr8t3FhP5FQXFTvrBCb232lZXKANwDE5cyliK8qAmtFpmfaoZ5Zksl0CKEgdv0hCxOiV0zJPyYwlhn",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 2,
                    "token_value": "zGE40lYYZnnylhpR7LxNt2cArSf22VcezNOpwIMFA22V7/zca2bsGm2s/cRvZlwgLHETRqKZzP6Z1TG2aPlGeADS5HVLcc9yrlldt5l1GcT1E7Dr94m2n55JAtKyCTd/",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 3,
                    "token_value": "57znKFTmzxb7bBudutatBGV9SzoL21alM7KKbI7b7WolZfs2+EJaSJdksGW6h41AYyQqMNDGqF3xxWkHuCEGH4S+NUtCB4ystEKoDIx2P2nnqnpFr6NXBC/nRIeOTHxG",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 4,
                    "token_value": "+CGBV8zl8BqNQNzlnqS8Npm9OzV2mA1vnOEXcaMrqvJ/9n67pnpDJ7+LVB/09kEnyUEsQQj8dlpcJF+wuKbhoabPivYTgBEKH27FCfcS+Ytk14KPVX9uc4zN5tBBg0wD",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 5,
                    "token_value": "Q79vS97CRCbsDCOu6n1XCjiwtY55JhMnmA08Uy7ZS/06VMXL+ufQJQaE6fALc8x0I5OpRLxHfTTrqnExxyKZdezNlOexK97G+/XT/FKNOIqBQs/Oc4YvICXm1Jo/HYBU",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 6,
                    "token_value": "Xqrhn5XaEHFNnwhy3YVALzg/YYLWe4t3dwUqh2kDY3HiXaJQiGNv59H8rL4vVxMNhi+DdhMn3QL6oqmfrXfIYqq7NLpnyGV801BxHPSBQ9bOtB2k5j8dvWotY1sN0fR6",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 7,
                    "token_value": "3BTpOGADZO64d2dyxLjAeAxnYmbDBNk0PklKaL9hV6BN45BFWk2/Ju8fLU+GlFhef3uY8EZMp5AS6Wl2bTTdUqr5W6iMoJd2mkA6UsH9zdoxItPT2sbMGAyarIdK3Rgy",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 8,
                    "token_value": "3UiRRdNidvwUacaKtjgtbmPX9nrLpiZ0RAys52jr5N0FRD9n7SH6wWK/RTk01WIXbdAIHCRinlzwDNH4QRRS96Kybecg+BfIk9V7C1AGKleIbnSQXc4BFBxjDwSSegJe",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 9,
                    "token_value": "B0ygqEC2DTGM4oCIxE/0t+2ebwOhfMZ4WXHJ+h/cFOEc687r002PmB4agg1yufa/DJQ5Cvn0YAwiXQwjtqGVkZBgG0khd2fI8aMpVK+E/SuwtcJQJhZ1ZGJJwVzw7UI3",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 10,
                    "token_value": "Eua/LPplfE89yqoI11ZHBEsAockBH8dvLpblFGTuCkw7BMY5DRVfAofctqGlFB8kpvBKxY14LDAVifxG+kTuOlTuuhf+k0aq9Gjg3AYy5R5MiNoP1jl1JsZQmdStCx8c",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 11,
                    "token_value": "Ph/yWFN3jss09Wi3o9Rw5XExf4gvyXxXtsugCGi92Hm5qjCYGiyUaFb5YZxhUIqOpzkpnMtfP2HzMhFZ6WAPX06HyaRHhtIbLexMaFjyCjvEJc1WMUiDrIXtuiRUb5wy",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 12,
                    "token_value": "f+ca6l2seKeKbUK1wA5B/5XWzLNdw1Mg/6qxIS84JFU53ZfMY94F2qq1Ygz55cqmvFo+AntKDGAMbdte4gYO2ypgyZ0+rfjHRoSlT8EKhF/7/qOhkDp+811u97Y4blsN",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 13,
                    "token_value": "SGbPF6nHuIddsE8D8P9ljdXLcz+avA+MMvp5fkJqdI8tN54FA7QAbWnLqfleK3rccu8FTHriHJS/Uur2jILwPDREzqNIpohQQUM9r0Lw59wuLzUeGchMpyVroP4rIO0U",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 14,
                    "token_value": "fy/cG7e2H8D6WeZZpjZQ0vI8HrBvoegb27CXdO34qeNFUmVwIGJc8uBa2ZgXAEax2De/biWj3950NdkLktPHLfpOhrMlmJ5L2g/eTrfhnvbojvtmopDS5m2qCEMa+hVR",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 15,
                    "token_value": "tOPWva1ABjMUmwnwX+5PUPhwCahtCqpzCY+dTDq1gwp7vxLLImvu7fQh8ZxXBXtQ0S3nFM3iFi+32uUgPaTuBCjHyHDPFh4YzhWFq/rX51kgYamnOmiyY96B7y9ucdpc",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 16,
                    "token_value": "IQ/f6LFhC4TznTr/VOZ1/yR+Nmc+ZDrzsWr7ntQ71QjIBqqOFU5Z8UcO5R3wzsgsoURZqAOJNdLjO53ROwZJteCPnL6bcF3f8mw680Xnv2qxPRF565UYXKGqckDAruRD",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 17,
                    "token_value": "WbBa+g8gswJXdp1uzeKihitE8+UeQ5jNBfDYZoE/FGTNfOCJBlupaYCVPDlyRuJuwIblrNlg7PkEAhWDbX/k6mjhLHE0Iz8Ja0Ap8GXUcmYs1Qb42YqNflPYUbrlqLEq",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 18,
                    "token_value": "bBA2g5lOcSHHEYv0mM/5h4daRfR5qR1vwrG2KqhFe85ULIp5mMuvG95pVFSW+2le6OmU1nOqZ7Tu1oo9KNiISR4n+6mpqMqnAjihipUReaOFaFMBJwPpOM35hviA0vt7",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 19,
                    "token_value": "R9Xg1FlimIxyc8VMqTMb0r3e+jqhawB1G/Rm5//NU4LKrdpM6ddgutbHTvpa3xBBh09kC9GEPWccfTiaYB1obxSbbsPdyXVm0XvMYyLvn2Bz3SQbtQqBghLwtkwL8moR",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 20,
                    "token_value": "Xt+vdRY8Ittw889kAUTw4pexkfzZw87M7zmSygvl/rRjbsKinRWb3B+MOOMwH+WCwajdceEky9VhGzKZrgth8LYYnzST2f3IFUA1gTRu7nWdQuc3tbE3duQOrlkhOElq",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 21,
                    "token_value": "mUDGOYQd5Ghrvi44al36s3O8xC4ICYmZTDQTl2VlSJWwuUFBCl9d6nbv2hNWvvYNRBiLjxeLJs8JxCmMgKOfaci1k8+18ngVyL5NRzkghY5qV4QN3yWQG96qXLQ5f41c",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 22,
                    "token_value": "78lQdjxlE3hrpmFE/Smb/US6GjZzLAeTC2Daw8mKWmRqPSkXR+F5DsSBZthJzZHnHirSqUix/qQmT03l4/yQOgSyMn1BcrOu9fVd+GmBlg9upIPlZ1d5M0r/mi7kunhR",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 23,
                    "token_value": "532Ly+DXGH6wjiulcISujevzBUw8IwyTNJiebLAZITHv2lRS7k/LZSgglQLmbNkYf7AjslS8fE8txp/gGyv4r4iy/jIl8Y5cJT27nu4FSvfrCP5YMhOKNSZwbjS9sNgf",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 24,
                    "token_value": "5Oom+hqi4zPtOr+iJXO9Eoz4aU7F7Xpy+jDvaShHdu2JUphM2hE2IQRINK0idI1yRlccco3hu4tqAvJhZCd5Mjik0u/hOJFNgV2BkrhU0fLreipGvhvfE0934/icwkhH",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 25,
                    "token_value": "/tHpsfMfvhSjnBhe+Bo7Xa9URgxS6wmmTpoD2J93C9hlUOKI1p1XTVvgZ4Fzh3kh/eNHvD1OD+wYgYeG9nuO5fhpZj1fOubemmpE7K5rhm8RMqYOue9+djrLBjOXkgo1",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 26,
                    "token_value": "isscCij9v1IZBJeqdz/vl2Bf0p7qz0CdyZo4l/qjEa94Ix7Khmf/VDxZmxdoIbzM1QMtgUvjwWxxO+3tlgKSs8SFCwCIBe2cp00g1zPsWsfLmxhhRSzr1FOKPqnT/uhV",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 27,
                    "token_value": "PDvDGv0+ZQllZrasgwOUlBE2GSUi+ZEuBwZLoHJbvwoateDk88mr9CgXslCQ8gUDnOYGYVmNzdK/jYUkLaBcKKTjWCPSsHx4DPhdHiWiP53u2+VCjgn7dDKlUHwvPtgS",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 28,
                    "token_value": "SPNB6sVUXmYWkfS+q03JdvInQ91Oxicw4oQIGXD2q+1ZMm4cqd3KKaX04JNnAD2iUb35TeprxzU+1p9+HZJ8ATzb16ecVZuo1BCUM7i3qaqHGa/cxx44kZp2MBiU+AJj",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 29,
                    "token_value": "3ZxuweX/U+TYxRhNvhNKYTSbaYb7kWwqL54lGFqkni1TkYnk6gmWclTtDOSRQB/GbJOd1/vkIrUMsXpK7E33zH7fWVMFNxepoSz7cZ8l5QKQMRSdpYiGmuK7V+7PaFR3",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 30,
                    "token_value": "0qBzktBK6AFPz73riQY+eZHX9IGtuzg/0c/gkRyR4/8ldNRiKQuuMG76r8nq7SwFgtdlP4URZQaZeTVyM//8eMRGsl2WutKdwldRMaUqWlh6Fwj+BQwdFYf907aXxvFA",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 31,
                    "token_value": "7R+LL0wUuHrD9jifOmVkf+adtRxgzeWQ7nZljB4bNjZvd5efCbAlW9i4RGK4eVy4Kl/pyBR3zxmDpqjzVb71DoZPtXo7ULoXZ+r8nhUbfJPpxygMk1HYulJAhIw5qxQs",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 32,
                    "token_value": "c3p5J7XE5/Cxny66stcs+1w3STuL4FBniJbHs62o6Y3KYYtrrZsdpllCqd4EaH2JnmlDHOGeNcAMwqUXCJo1XkJvMVu4ACz/MN/E3Z1R+/LsSe4qvsancHg9T6ElvxpI",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 33,
                    "token_value": "N9O5MHctv7tPUWsn+muYTL4JzX0ZdaEChjx9HUJHVPrMJWNirX0lCzWc28/fylHFas9xdv4zQCve6YlI9GYZW/byvWqJ+2fAIONUACLxI03AexZX4AXeFmXtahwhj8I1",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 34,
                    "token_value": "MpYo5UayyN+wJCQthpwn9ToqXH+OZhfp5hmiiQS+2zhAH4fBSdEJ6Vx8USIKTxWLcwARZxq0Fq+EeW1kdh2XEiSf4z208lTG1wxJaC+hVnOdTkD72EPl3Yb6LO0PGSEK",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 35,
                    "token_value": "7ZcNt2yHN7gDWMwv0WxmueNJFWp3DewkEgH4Ium3fhlpkPW+H1b9ObJyVLxYfsck5vy3z9p6skiuUtAT2vVfXFB8qrUAlRiUzZVbbRYotKTlRdPgRABkn5MoQEGC/HB5",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 36,
                    "token_value": "7eh1UZMCFtnJOTHeCAmneQMFNFIVvE5c9wlJPRmVgnVulV0QChzZBseEbc7Uqt1QOdpq6V/iDNqXCEUZzvEDaKJXjJQWwxMDRuzD4sq0zyyOIoq1UCGf2pWTbPgXE4UH",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 37,
                    "token_value": "GAgN9KS7hKO78oz0tqcoJl47OYQeST3ma7dwc1gD/6uLHIpAA2H7Tw/f8Acby7D5vWIN0spYyFWod3QU7+Y0bySDjGWLvMZvghtm0rpccXrwLTh2Pi9is0llTsTIKeo+",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 38,
                    "token_value": "kABTJ32NEPAkh9E5+aWHeM1h6FMnF0Wi3RWY1amU2JZ28OqEhgMnDmWrPNqBjFn0gR8+8+N2cLx7/KZNEYPhDJjhW/4hM8z/T94JzcHpdJbHQbicbo2DadaxixOSY5NP",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 39,
                    "token_value": "ZXY7GHX0Fw/J/y7hJ4RE0KkK9IGMFmuELL54M2qXHllEzAa5/CFrcJNuZca4YIJCEKlBKYfGwXGjSuf8+z2XxMjSoM1/IVcnXCRleBw1ykg38+8PPFUbrM+JKs1H9n0T",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 40,
                    "token_value": "EmmVnNKY/evoXkuIqTWT/v3GjcPlCBV5Ajf2B0o4qaFUWyl1wRYJwd+njP69XKx5tnqsB9ucxEebGY/gwLQ9ldrQlcxVpfp6ifQziEBrllGPiyfdES0sA4xzaPQbBYJo",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 41,
                    "token_value": "64hkg27zuwX95UHgXMSY3S7MFufu3bjBn6nzoBF0+7uDS4WdmvoKGuwcB4pGbymHdgzXVfxysJMJ7J81xlc4xGqCbrhl+VOKjNGGeULrqDmFanO2i9nKWWQNDOYMDZUK",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 42,
                    "token_value": "FustheQMIQfEXFHzwzz5Pufa9id5q7EsG4MxPb8NLDeg29oa5DQgAETc2r4yFVVYwmpvEW2OM9J4cgZQVPHf7Jr5j6RjyvYYDv7ypZHrO3n5yYkiRLARFzaz/rdzUmh6",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 43,
                    "token_value": "QIFw48P69FmZ/i8JZMcaugsWqEQieTto2K5yxCBKBRqMVMbox2FdOviSF0S2aNUpdkkxTWH6heja1l0S5B1jMCTPTKqh37VBCR6KKYKwGV0MsEFDCzc960AlHjx1LMsc",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 44,
                    "token_value": "UjtiaQ1ZtHUU7FP4x78ETZQqamfPEGn+XMkU+O8MbHutr1X/9Z02xz4y+iU8Guiyz5CXiLtePZ+cEc0nCeizGsj27fC+pMApzHOChcBDrlg889+0Hc/kvLTOSQkCLB1M",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 45,
                    "token_value": "Nzw/aVe+4IKjLC79YZWJD4jq1Xa4Nhgxqv+YxBuZdH1CxenuYj2jjD4Ry4ZQuRH/MsLVyk7HszUIvOyMT+gril5e2PAxmG/zmKrrqhHjqBq1s5Qyxgwf+8HIgUYZY6Y5",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 46,
                    "token_value": "ITLNWoBvigNNbPiFMwO/9fA0G4ST42s4Wgm3P2+k+NdX+dPiY3Ft680z/CiqFPKkBo1ggPr3Xxkcc8QvH4LbdRCOmwUVsfEQwPK1cvZshWI1uL6kK9qxsA/ZloBXvSJ3",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 47,
                    "token_value": "yfP9QkZPuDmSn9M3AXmadGGczBIpo0NZg4HvKDD1V2rEhR2bfNsnaE7gHHRI+qLSMsc5DKPDqVc47TTlQPHz4L486FaCeskJMoA2KvBG97T9gTGyLfELrF45LOPpDdch",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 48,
                    "token_value": "5nZ2WfCqzzyP//dylTqhe6aLW5liYKuOlOCfFwB9QaDOOWUQPLZ8dhjvV5b4IFZrA7HoEEB1eJ3Oc1w4gAV5CPoKgGns9L84I9BT+sF2WqXPzz0JUE0uo3/Vr89KxpsL",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 49,
                    "token_value": "hRdXV02b1zdDMxqmSf2KZwX21MfOS5pBfMqW30lgoVdFDibpG+Inq8lxxL6PZ4ifuoEopqqaU/pC3VDrk+9qlY4j1d/aQ4tdWqW3n6PRs7NvipxzVlfCm0S8CbJNyhNQ",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 50,
                    "token_value": "GaLPHqj+i49OjHkUhYxE8k768rlmSH1PbExMfbFXfVuh8eMzg+CQMyd4Et69HHt/GB9owN470Y3m0tlfb49UESDfbjllicmWRL3RuvK/iAn24eecM47s0pCwAKwpvw8j",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 51,
                    "token_value": "jgHTDs7TXm060Vdw8sCKPufnaKJ2h399I2JBiNuBJo1tnxiId+KFeK8nRJfCvYEadQ/BfOo0Ba00JfoPmS8C1MJCpd3sK/sN0tUBM0blvvMlF/7BcYr/xpkTeWV7FRMG",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 52,
                    "token_value": "d76aLMxDmR71IsrbPKvYb8tAyGEtVGDS2FrLhbwD648oJ4++wPsPUPY+Gt3BQdCfqE/0sMKWJcFiik/PjsibVcCYOpdo44hmuytxt7vwEkx+gznu8IYCDwfoZPRZMJ0h",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 53,
                    "token_value": "deSA84s1NOZwYCg0Cyk9khtY9fBjzfecYN3D2WlAWehuxIJBKheu5E58+erepn7C+7c1zhP1ngFymQf05/u1KBBQUy3q56YqIQDyp2o9sqPRXohVGuKVzgN7aa+i2OAd",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 54,
                    "token_value": "9oHzCIy33DFSKLivdXpAWBP+fJWgaQ0Gl/1vzR/Ts5q9yNqtz3nxKbsLJ1/77bSv15Qnc4HGDtDWDJEsDKPIZhYdJZL8EqASpIdjoUNWMJrT+5B9YrB8iza4nnmPagkE",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 55,
                    "token_value": "HCigH2jRMxOOSpAqvry3NRQpO66D38kWQExeoUur7ajE0MURRNm0AQRDJbgERj32i/NKwulfEseH8zVdKxGXfBBcXs5JNtGXIzkxDO4n/vpfhtH0ncNpklCfaQ9hVwFe",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 56,
                    "token_value": "7w4RxSj8RiZtMhuGENIi9g7t42r/7/Gn64y+45rEA9w7lbbV+uyhs87IjmHnQCag4NbcC3S1BKBUS9X+no5YeVL7dO3wqEXhfkplovfF2xWivnbOsFxZSJvwv4z9j7RP",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 57,
                    "token_value": "DGoAnxOhxj6VdmIvkyzSSWLB3TXTBkI+Gq9aanMUTz16hPcjL1wEFioVEun2wIkY4umIUETmEuBIn4ivnbPI+iiqjljFzhpHttV69lQ0blum1cqhYZ8gmmCMFUmilcl0",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 58,
                    "token_value": "CG7tYHPTrj6Otdqre/TdGudSupjery0UAdurB4IZgyeEntJMWj3wzriZMGYjB58NQaWop4l9RVu8YU1yQ/XrrxRR6Tk4qzYxNY+bkDILr4nQxdqjUCNwbEKBj7sbVP4m",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 59,
                    "token_value": "zVrpguSqN3rHPDLE3qJss0ttpDaiSa/uKpO5eAsvlgnOP4bau85Pf6pggHItV4tNUhODXsZT5897g205AvqWfkimhrFFH5cc5oq9pkvr6eVFN8mg5EVb8G5Zof5dIkpz",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 60,
                    "token_value": "dSG32EbboQXr85xxRLbLwr9ZtnGF1N+mEwG9/NObR7uzpSHXW+A9t3ykYdCCQy+iRrVbkgFqo92f/Ga8NEmnaNoGtIDOPKn8HPh1kI3Zudbn7IE/LilwR8pXj8v+QbYI",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 61,
                    "token_value": "ifrhm6iPJUq2BMwSJ2OY/NN4yXj2u87kCpn71hmLlSK77LwE4n9K7Gd/ewa93kG8NHPPpWpi8dXzf2v2lyBvOyC3iaofJIkiUT0bPA4eJUEhCTp+yb0I03JnsqAhGT1G",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 62,
                    "token_value": "RZWTto2qvFiUKYwyokxsaC5XI2Hn9k8n63N0N7sBqA2WdMWyPxbvs+Hw8e0IBvt8llPZ+vZUyHZztlXLxBw1CrzVifZKjBK8zv1fM9rPO1OUN62/PmWCT0/MPLS/x9Ic",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 63,
                    "token_value": "rcmazuQ32aqV5WcINkhCUK/fJlRLbLVoK8Xfm6vzKPwg6YQxFvvpcDMcrjjln+JVQVAjqM8+R22dHE2DfoSmiNAe2zCd2FickacMO48OCKy5Ir3lKYP3Wyzf+CKRPtgR",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 64,
                    "token_value": "mQUwrT4mNMpq9RNJ9hTJ3X4ezhPR8m5+4kwXuuS1X0xn3jAcnLQMEliVEO+r88FH6OAAnk3Ko8vmh39eg38R51DHPASW++RvLa0eZsfJeUsT1U+7Aedb3dFxKHZatY9c",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 65,
                    "token_value": "JxYw5AWi3YY4nFTZqDw9vIeavQBjBI/n7YSQyafPLz+dHhCyeIiI+k9xIb7jeucbOeryiTMXD554cnbnKUWUWvolYxeToFoWKt5dfW0C6WbeDwR5Kb+2tDvRYGj53E5E",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 66,
                    "token_value": "W/fqWgEsewVDtfbaWMCSm10Mju5znUO6yizmsX77IXF7nJ/wPjd79UDT/Q91MqDXUOnvAuikwyKAj8oVy5ISgX6eS08oi5Kd8wKQPYDx7JzM1RB5Kf3rihQy0XWqhuET",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 67,
                    "token_value": "kn6YJTk5iYjQkdpH1SGk1vuar8M/CjLeyi8w54Telf0i2iwC40N9sC0BEc1cPH/GAck643XyS8GMf4B8LO0axeSl2DOhALcHEUI2PtiNIGau68ep3mEQsvdBzn3UG+cv",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 68,
                    "token_value": "Z93HP0MUYexM5IGV5dhCfLctzJE6V2hYKGGWZOt5yUkBQ8kAk/TcUdQEUjZoLVgaA/+PuM0eeS9wsN+Brmb6RVrGAufp9GlodizOg4IkESUZvJdLB3TuvwZO1D21LrM2",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 69,
                    "token_value": "0ajQPPnbMiwrkzaEEC9NbqkzpH2H8UrAbSRDJ94uYpkO3gDldCytjGm5+yGdz/JxQyC2SRMhCEv00YeJJ9h4GO6XWjYPno8P4wHGemO2sFgAzxsK4tQgOnLGE33UKwk3",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 70,
                    "token_value": "71OKCVJSPlOPFd99IGLq4e/i0iiTVaJXRaq6ZySi0YSABsV60uWLeWNqgaI1kMGqK/JG/QZAUVLIu4eXIpH1qBYifyDinPNSpuukkw2JLWL2Moan9NSmsNMdJ9+y+xpB",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 71,
                    "token_value": "YVrGtvPqedrjPcLdLQEXYuvJ5biDAgYFqwWXf/T3B2ZEl9RyF85D54YqU37cWDQVrarvbNelAsRK+artrM69j9ahiG3iYlHgmFHeb5kc2xefBPQ9ObdYD8KLhbLEmO82",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 72,
                    "token_value": "sVumFjohm2OY4GV0KTfaNo3V/vX8DmoG2PW4XiImf3GEzym5BTzgFfqmIr7sxqKqB8Rr4p3iCiJBLxTWJ5VNUiCTF/pPY17jRLuU0OazlPl5oCtEo49znzDJK4jOvKpv",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 73,
                    "token_value": "XCSZugqhtO72GMDe1NuEP25CpQ4fsc1iLrDfkNPLGfsKMCw9SLZEJ9uU3W6HZOUpvbUS/QHiIkb1ym1u6Ab3EEYmCfn46cyLP+cynNh4704BsCbP1dWYJiAp+1cnymck",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 74,
                    "token_value": "0qPS/ViorZQDPP9/qDyuXt6EcBwd0BWXHbb7pKpRJ8KT4l422G/CnCj7fpHAIKd72lOnWl8a6PNVjQVtqMW6J/gxDFhg5XJGhl8q5NdzhIHK4zw07KrCPYtj10l7zUMt",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 75,
                    "token_value": "4UVWMguEZcHIOb4m4qWH4Xyu3V2nTf4UO0Uy1SsQqSfVTrutqQ7zEfWw9OLv+NZ8JcsWtLGLg5eu1264F9qvN7R+d/SKUKQgrFBVY5fqkZIwey1nlsEM3FZkpDwlvYRM",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 76,
                    "token_value": "iq0PdOwvoXuLoBV4rAbk4TPrER6/voNjtyUDrnTqome9ezVfoGHSqL1qAnKxRwNDW5pQxkGFNxt3XBp1BAMnYVpk1rA2eHddUgOTuWBaomSXL+u8QrLrJ19Gwq570Whf",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 77,
                    "token_value": "QgoQQEApUayrb95fKzI9mWugrDQOlAfW2Yw86SknLA6UYwnUnfyqt0I1dID8XIDRCRQcwID5oHmUxzqPVSd4dnhvoBNHArth5tPSAPH6dg7VWBRBb2DzH//i5HT4dOBE",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 78,
                    "token_value": "yHa9+s373mCQbA+MJgoPL2MHDKEyf7INXaGZiiHbgZgtDkqzV+i3X2yktnSHERpFQMymIyFGwWmVOoZ37H7p06KKqq0gvJg2yy6YD+aRY1RbXd2G6P4nKryMlClRYFRU",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 79,
                    "token_value": "wl25dE9+F+tyZNbGmOYlKYueJ256KkfMI91rV/+nVG8GzGuQUIVTfNvZGo2Ltbs+NO30HuC6N3haydYl5BHtQFyDeBkW3HsO7Sra9YtrHxPytMUeZq3k5uuZaP0SQggu",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 80,
                    "token_value": "l6AQ70pqH4XFwEnFGP30JpKyQKARS8zpuxqYKyUUC59qiMdsGl2k6g6Z3U4S3tOMgFJPTIBaWIyiGSY+/G6htOKxNXHfy6xO+coGu1559Uyn3VCdLrE+F/V01SMx0yJs",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 81,
                    "token_value": "8I5wznUOeaD36Q0LeAL7hdqojn2mlaW0IYzXCRYUqSHyY7F+eEEGGkhXD/njj1seM7JFtHK0utUnWBFxaKZ+dxDHECTI0E6Ly6CwlgXAh9BBvoexZwRjO1wqlMw1ouoE",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 82,
                    "token_value": "9efQaohZr87tYFvfx1Fu8l6UyPpsLheWWn7pjlyCPJQspppBZbJVSMaBl8eOBo12SZP8m9yikn22Z+yAJvARVsLqBBfOkN5Q8PFQLX7KX4SmpGxmSPydzt5ELWXCy0tJ",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 83,
                    "token_value": "WdaC5UMj0dVALasI0B0iEEIRKIC4pW8mDxvxQ0KmRkVpn5Q9tIwxl2h2jLx7a2Cyc7naZR+MaF3+tET/Pt86ReAxXxrn2NLKOZtL8MLWibgm6Vq7N15mpiesjo33rgo7",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 84,
                    "token_value": "9qzDb9py0QzeX0foQYxboZ4EwexOBnUA9c+wxv2TdtBOb2GyjZskhNEDfWteEaOJtNOwmXo7dZbaOqzIzB3fOo7P+8YPKVSQ3CY4G8jgoXkGokhRHPCShQz8EUpxmV5J",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 85,
                    "token_value": "K2n4LGdw/QblvtjwonK6IrQNgN2zbkC+SVPo7ppSXIyYReT9wP4ca4w5tLoaPsbEpIEHB5XWsUuKtJC6GJuQytJitGr46tQUXQRcKo363XMiZ/bDDQoyi5QSzqAksPNo",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 86,
                    "token_value": "Cbxrp3aF5K4T8kL+TITNQdHu1vojvXVroxCjPYROwPiaUb5T0g5ye5o9afk6npoRCW6WcAdA6P3cYjNN/DbEdzDK/p9MJ5/z4iEOQX/7SN99n4VYmVFV0ztYFlEi86t3",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 87,
                    "token_value": "jPJEGqwgKW1p0wPITSpl2quYaQkp11AJauw/yZcK/J50F2KXKBXuKIDB0ZloSJoNetrS10DM+iiQN8NKvITLKVoN0lmfIxDjYV/K246PnhMjF6kSkabZojjlBATHmH4x",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 88,
                    "token_value": "HcM2tRUkgB8QycAUglH0HJBwxsoFDD9tVfocdtpnpbhdS4cBe/YtkIUhkX1NlTIsMCGJ+uEI+flQct4vKnDEhOaf9m0AtiTj+SYJYkrpCdBl0Z7qISnt1V5prDsxKV8t",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 89,
                    "token_value": "JyuKgCoaqPEEuKW+zInYq7BlgSdmkfrn5uCuQ1+h63fUEHv6b+Ascnn/7wKZbmyLSkDRB7gt8IX7sBMLj42gIIogo8fsbRTil+b/ydp5es7/W+5giTwfyk3NBfMkyI1a",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 90,
                    "token_value": "/HC/fyLwntaOBpbBR9B8GRUE+xG3nRMi/M252aqCppd3uR1WO/maC7HDvLR4Po9Fo0gCJCb6kSXFF2lDQXcZmhj7kq2/CFYon5ZLYCWHRZIu+Ee57/OLuyuIxX7/aQEu",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 91,
                    "token_value": "nkIPYz9UkfPxdTaB/COsI7ubZ1PJDBr+4+2sljCdUj7KgW4VsvXXfynvar3x3oJOf6AYwlUwaPyKG9jduuekKf6IvczYSw4GU2qOTxOUKeqcIR/MVhUK4xV6ijcKTAki",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 92,
                    "token_value": "m2RygvgSxcv2T1QXsZkEn0cUCL9i8L4XOZ29l3ZXAwDnsNDeaphVtGPFcozkguvcjh9Mn5GBNLqaHuIqGmr65+zoYkSYz2laeYLTAk/bP07sFPdqpZyIxkjrH6R66HFu",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 93,
                    "token_value": "TUE2W4rxqzYJu+0YW2MMZALRPk8Q2UGs5FwkOs5kaSp21A7F1lxxnUlYDEoBfBK6ZWBrVDD3yspz/hZ0cjsNYzR8ncmrNOMb4dCcCzCJStrACih6p0VFoF8GCiO2HZtS",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 94,
                    "token_value": "zKidQhlPO1cHNSCR7ABhcv6D9G/F/xDZrGnyBLcou0sx79iFim95l5gDKgzyCst56GjKg1HXBLRQR9D2Vq8uOKqDf8Bis2o1eOhjhhAc2NxklNQL7heTHMoiOW+XXpYF",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 95,
                    "token_value": "bMO3pDXvmINfApGPgKalfy5TV2wHLKG61f/teZvOQkMrjdQTn/v7p0H4W3nLUZFEaT9jHlDO5vlv62WrKErxTBRt86LQiMlLIZ2EcOzNhXg+zMQXoowIXboyOsNlLu5m",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 96,
                    "token_value": "IAahZUkR5n3m0xIQIBOHTQ8H20Tkx4Kzzs1sGNAcgpozTryNWxfuSQQ9oIy80mxuTCxUScczq0qj8IAhVgFez9AA4Er99756jO9UzOUIkP6O2KgRS/mcuvZxz3CoPHcj",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 97,
                    "token_value": "SuxczxWJtlQndUE/yEdU1laSXbOa5flfXuf3VMZsiElJ/D+duo2DfNWQHZTdNkuGRDjnl5FIpHbtEa4GxSsDwt4/i7dSKyG/Vhtigfc9je2JsDEqFjU6IkVZImV/gud8",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 98,
                    "token_value": "zV/z2UxGkAvPIfYhDLTye0SHFpyXuRYHj2G0uV4gKPuAdFTZ20dTnPHLQXfbjN/8WIJgLLkhv18iq8l1534pDoDQrIJdYApkkwqYD5gYb5Kef1A4XAVCV9cS1y8U5Qoq",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 99,
                    "token_value": "6XMYhfJtJ2sbEvVmkGpkvBDlGNFI/N7CBIYvFK78MHqdmHJzbkwS6C3xirikmhU0kpW62yDGLeNemhnPN5FhGH6BuiWxuZ/wY+MDrEz6q0mcSK/GnVKTF/I8Hj0pCL8C",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 100,
                    "token_value": "AQhRyHd1DNlZJ6LFZlsioBO2CEZTzaqs4c67o327XKJD+aZELHbO8E2d8kEuU4lPClzaEtvKxsjPt4cucsIMfJCG6tp4YM22Whi1/HrD0NSfNLynucs7MRPn6t7lDFt6",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 101,
                    "token_value": "PdKvW9TfBaa3MC4Bu4rZl575xyE4rqJLNAQvCWtjEWHb4IlvuzDoeXEWElE1l5TGB6N74FCakW6Zxyb5F3QOm6apzUgBxOIMgPaXCExJqox36/jfl/ZEnlMN7wVjljtc",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 102,
                    "token_value": "ZO/Bg49dJrelLjKqkIjHW5HRt2Wl5EN8TMM/b7uxpl/24ft5K+szIL/MAW58OQdwM8GLeSCIcWYnLZAxsXsmdJgeIAQILJazVrfn6c8JDO8XNMmfVWrOtO2VcdtB+DUQ",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 103,
                    "token_value": "IbPNTvPV6PBujBAdzE6boDmORsWLjDQHDH+TnQW6gjVtooZE9pPscsSxxY/aV03joJf4ZS77cyKMcJcCfMLaO5oZA9ln86+CX57n+IJmaOB96mGH+mMOVayiKx7az/4f",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 104,
                    "token_value": "y5appCKcHkkZ+j5v9YTfc7F65v/FwJFxxC1SC+J0VW5bXX2bBfDy1AnGhKKD0B5d4plLC7nY4AgiR0slDr6Wnq5ehWwrJzmULC/YMnLm8IatS+pW/BwC79U58kAGvmhD",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 105,
                    "token_value": "ujguwzdG5gZp7J2keub4smTDD50qzaOIOI4hCDVx/hqihUvsg6HuRhF21SwkvAk+vY0/5wfPgrHVDupFfiufkkTsd7/7J/J0OABY/cHwTNJ/gGUPNsJqTjuYbmeQClse",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 106,
                    "token_value": "qhayighb80hyYu++N3L/zioSTZpXAKtnWnrynq121E/J59EHEAMQ+L4OP30sspvzI1xL42INySVWrYgu76qinCyS0oECOUEIdtJK8I6/v2wGAlFKl2Zo4yZhVO/QO9dE",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 107,
                    "token_value": "fAKTnfVjbe5TdY89lMC86FsV6manjQl56kr0PUTdK77YeEXLtsjD3eBkuUk30Lu0RrkVntc3VIT0CMIta9dWVqitJUq4XYGqq4LZGyJ3lbtY/MI9W9UO/9NcY0hwC6Vo",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 108,
                    "token_value": "uXbBBGDb6eaT9i87upkU9Ib/5fN8stTGWdD527Vc19hKStwzTxxe1W/+DArYL+yfJSsP07GHfSJxYMpt17znwBbZts/ieL+vhnkmAOqdaWhkWFf9MaR53aQm+tjLqvpK",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 109,
                    "token_value": "ujlG68BMH2FOaghyAQQtF7/pF4Qkr8oyJxfxFR9NPBE4fIu+T6N5/rMJU9a8GAsimKiPqDp+TIL/cjE34ukNxk6MYqqw/LAQb6oTkBxgQeN5HNPj8p8skCaWhU0825c+",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 110,
                    "token_value": "VCaV1+axgteFOPnbMb9oeQuvkhzg+w4AJ93Mf8MmluGW7sxUVuRnnYM2gT/ySE4F7ntoITs/Po2qzD3J/Mb5lh4JDsLSjAZHB0tcA4145kvoKY1mKMvr/cE2f/iXFmI/",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 111,
                    "token_value": "ar2f3MxvnwHOWivcZm6qZ1gagGq+yGd9BSZFv+3jKqyhYkGsxKqMHvf0FBOZZCIYnwmTmDQBB7nBzaJ0Gv96tzhzkwanYkHNvzooQ+DpfudkWQzfBCIdHuzatkLwz/1W",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 112,
                    "token_value": "qEaqP+ySVu2UF0CnBNVp5NVflRzNWqeLjknSr5MaoYHi9JAQd/nCRS36Qyr6FeHy1yE45DTMmrPlHpVNA2uB2jLdQcwVAnVYA4JnCC6FdJXJvtOH2RSH695RGYIoZNIv",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 113,
                    "token_value": "AsCjmHJ+JgmJifCmaMwuMd8mZBqd2aw1524ADP3VsZzRnzgvp9XG+BWpzLwHjmiT5HXx/3x/h+/pDdjjDWBN4hqBs9G7/gV5WF3hL9BUy5h9VN1524A0kqgteZJysc0U",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 114,
                    "token_value": "C3Y6hqkktiKZ8CBk0LaBUcn1b0TnD3DkRCoiEpnwE0Ah/69aMAhM87VcHrXmSxW2vKmjmaeD6Y8pBXRRjxIduDDwfMb+2LfY5r5yRlCaoAUjZNLW6uLL83ZpM+OCVg0o",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 115,
                    "token_value": "ap63Sq635ut0iWIsOdWccZO1W29835AFGE0KGWns90ptKSWaXm3xD4LIAF3gfLezF0gSDyzoH/TVoJ2V2S0TvwYa4qL/ANWbeHLmzBY0hMEDqyCcxuE8b4to8MiZ2tgI",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 116,
                    "token_value": "as4WLaOPk2yqKi6ARUJjpsfn+dGsVgylyMU2Y8ItG9xU9HAsNrojDxkSOPeS/sLV+2XgAKFfQyOkm1IMdCaneSQLSDHHGontPl9Y9yi3ab6NlEV+vhaRJVqCmnn19dA3",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 117,
                    "token_value": "GcsQdRxHj+fo+IY1Qse3HMKLm2RKXGzYNUqxSeD19Rm/h42+5rTGjmmLwi40JptpCL7Veb1HNd/hMgHKzpOmIsqFhlEFipOtGxM/VTZ9dqquy9h7ybV2jYe6Hzf40aE3",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 118,
                    "token_value": "K0fmnWsCLvmonMnIOesSoPKn7F+wYPeXaTkmdgquWnukEdJZrCznQCvvYC9OjllNsyLYSUUXGlJ9JvjFlxFvOCRSgvirNk4AwllRtUmV/xsXsRsaVi5o36UDtQw2Kxdx",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 119,
                    "token_value": "eMwa+ThyM9iqFp7vvCz3hqPL+y70D5lvg0xPRR9kANxzKOV/9bNbuynH13kAoiQqvRcHvTf2f6Oj9ZZdBg4VI7rElXwGQju9yc3itgm66L1SWS3YQ7I1oRoGxLgrkOcv",
                    "value": 0.25
                },
                {
                    "expires_at": 1639220745,
                    "token_id": 120,
                    "token_value": "sNkAJB+Wvm8wlLZu4kP3B0/GRs2USh0zsQUR/T16nC7waFH8oz7rlQI2jEY8gtUXFgba3KpqXfk1xGXMKkp6CHwT4eD4MlRJ9XYf16gGT/NzKvm6wIJzlIPdZ6dtHVM6",
                    "value": 0.25
                }
            ],
            "trigger_id": "g05568e6-f4a1-4438-8e81-cc0490fd14f7",
            "trigger_type": 1
        }
    ]
}
    )",
        // ---
        R"(
{
    "backed_up_at": 1635415527,
    "vg_spend_statuses": [
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 1
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 2
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 3
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 4
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 5
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 6
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 7
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 8
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 9
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 10
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 11
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 12
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 13
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 14
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 15
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 16
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 17
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 18
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 19
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 20
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 21
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 22
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 23
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 24
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 25
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 26
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 27
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 28
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 29
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 30
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 31
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 32
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 33
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 34
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 35
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 36
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 37
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 38
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 39
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 40
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 41
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 42
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 43
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 44
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 45
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 46
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 47
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 48
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 49
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 50
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 51
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 52
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 53
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 54
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 55
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 56
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 57
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 58
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 59
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 60
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 61
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 62
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 63
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 64
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 65
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 66
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 67
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 68
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 69
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 70
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 71
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 72
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 73
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 74
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 75
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 76
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 77
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 78
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 79
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 80
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 81
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 82
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 83
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 84
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 85
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 86
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 87
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 88
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 89
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 90
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 91
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 92
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 93
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 94
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 95
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 96
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 97
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 98
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 99
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 100
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 101
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 102
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 103
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 104
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 105
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 106
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 107
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 108
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 109
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 110
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 111
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 112
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 113
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 114
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 115
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 116
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 117
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 118
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 119
        },
        {
            "redeem_type": 0,
            "redeemed_at": 0,
            "token_id": 120
        }
    ]
}
    )",
        std::move(callback));
  });
}

bool LedgerImpl::IsShuttingDown() const {
  return ready_state_ == ReadyState::kShuttingDown;
}

void LedgerImpl::GetBraveWallet(GetBraveWalletCallback callback) {
  WhenReady([this, callback]() { callback(wallet()->GetWallet()); });
}

std::string LedgerImpl::GetWalletPassphrase() {
  if (!IsReady())
    return "";

  auto brave_wallet = wallet()->GetWallet();
  if (!brave_wallet) {
    return "";
  }

  return wallet()->GetWalletPassphrase(std::move(brave_wallet));
}

void LedgerImpl::LinkBraveWallet(const std::string& destination_payment_id,
                                 PostSuggestionsClaimCallback callback) {
  WhenReady([this, destination_payment_id, callback]() {
    wallet()->LinkBraveWallet(destination_payment_id, callback);
  });
}

void LedgerImpl::GetTransferableAmount(GetTransferableAmountCallback callback) {
  WhenReady(
      [this, callback]() { promotion()->GetTransferableAmount(callback); });
}

void LedgerImpl::GetDrainStatus(const std::string& drain_id,
                                GetDrainCallback callback) {
  WhenReady([this, drain_id, callback]() {
    promotion()->GetDrainStatus(drain_id, callback);
  });
}

void LedgerImpl::SetInitializedForTesting() {
  ready_state_ = ReadyState::kReady;
}

bool LedgerImpl::IsReady() const {
  return ready_state_ == ReadyState::kReady;
}

template <typename T>
void LedgerImpl::WhenReady(T callback) {
  switch (ready_state_) {
    case ReadyState::kReady:
      callback();
      break;
    case ReadyState::kShuttingDown:
      NOTREACHED();
      break;
    default:
      ready_callbacks_.push(std::function<void()>(
          [shared_callback = std::make_shared<T>(std::move(callback))]() {
            (*shared_callback)();
          }));
      break;
  }
}

}  // namespace ledger
