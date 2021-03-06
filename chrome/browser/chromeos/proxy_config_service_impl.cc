// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/proxy_config_service_impl.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/prefs/pref_registry_simple.h"
#include "base/prefs/pref_service.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/net/proxy_config_handler.h"
#include "chrome/browser/policy/browser_policy_connector.h"
#include "chrome/browser/policy/cloud/cloud_policy_constants.h"
#include "chrome/browser/prefs/proxy_config_dictionary.h"
#include "chrome/browser/prefs/proxy_prefs.h"
#include "chrome/common/pref_names.h"
#include "chromeos/network/network_profile.h"
#include "chromeos/network/network_profile_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/onc/onc_utils.h"
#include "components/user_prefs/pref_registry_syncable.h"

namespace chromeos {

namespace {

// Writes the proxy config of |network| to |proxy_config|.  Returns false if no
// proxy was configured for this network.
bool GetProxyConfig(const NetworkState& network,
                    net::ProxyConfig* proxy_config) {
  scoped_ptr<ProxyConfigDictionary> proxy_dict =
      proxy_config::GetProxyConfigForNetwork(network);
  if (!proxy_dict)
    return false;
  return PrefProxyConfigTrackerImpl::PrefConfigToNetConfig(*proxy_dict,
                                                           proxy_config);
}

}  // namespace

ProxyConfigServiceImpl::ProxyConfigServiceImpl(PrefService* pref_service)
    : PrefProxyConfigTrackerImpl(pref_service),
      active_config_state_(ProxyPrefs::CONFIG_UNSET),
      pointer_factory_(this) {

  // Register for notifications of UseSharedProxies user preference.
  if (pref_service->FindPreference(prefs::kUseSharedProxies)) {
    use_shared_proxies_.Init(
        prefs::kUseSharedProxies, pref_service,
        base::Bind(&ProxyConfigServiceImpl::OnUseSharedProxiesChanged,
                   base::Unretained(this)));
  }

  // Register for changes to the default network.
  NetworkStateHandler* state_handler =
      NetworkHandler::Get()->network_state_handler();
  state_handler->AddObserver(this, FROM_HERE);
  DefaultNetworkChanged(state_handler->DefaultNetwork());
}

ProxyConfigServiceImpl::~ProxyConfigServiceImpl() {
  if (NetworkHandler::IsInitialized()) {
    NetworkHandler::Get()->network_state_handler()->RemoveObserver(
        this, FROM_HERE);
  }
}

void ProxyConfigServiceImpl::OnProxyConfigChanged(
    ProxyPrefs::ConfigState config_state,
    const net::ProxyConfig& config) {
  VLOG(1) << "Got prefs change: "
          << ProxyPrefs::ConfigStateToDebugString(config_state)
          << ", mode=" << config.proxy_rules().type;
  DetermineEffectiveConfigFromDefaultNetwork();
}

// static
void ProxyConfigServiceImpl::RegisterPrefs(PrefRegistrySimple* registry) {
  // Use shared proxies default to off.  GetUseSharedProxies will return the
  // correct value based on pre-login and login.
  registry->RegisterBooleanPref(prefs::kUseSharedProxies, true);
}

// static
void ProxyConfigServiceImpl::RegisterUserPrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(
      prefs::kUseSharedProxies,
      true,
      user_prefs::PrefRegistrySyncable::UNSYNCABLE_PREF);
}

void ProxyConfigServiceImpl::OnUseSharedProxiesChanged() {
  VLOG(1) << "New use-shared-proxies = " << GetUseSharedProxies(prefs());
  DetermineEffectiveConfigFromDefaultNetwork();
}

void ProxyConfigServiceImpl::DefaultNetworkChanged(
    const NetworkState* new_network) {
  std::string new_network_path;
  if (new_network)
    new_network_path = new_network->path();

  VLOG(1) << "DefaultNetworkChanged to '" << new_network_path << "'.";
  VLOG_IF(1, new_network) << "New network: name=" << new_network->name()
                          << ", proxy=" << new_network->proxy_config()
                          << ", profile=" << new_network->profile_path();

  // Even if the default network is the same, its proxy config (e.g. if private
  // version of network replaces the shared version after login), or
  // use-shared-proxies setting (e.g. after login) may have changed, so
  // re-determine effective proxy config, and activate if different.
  DetermineEffectiveConfigFromDefaultNetwork();
}

// static
bool ProxyConfigServiceImpl::GetUseSharedProxies(
    const PrefService* pref_service) {
  const PrefService::Preference* use_shared_proxies_pref =
      pref_service->FindPreference(prefs::kUseSharedProxies);
  if (!use_shared_proxies_pref || !use_shared_proxies_pref->GetValue()) {
    if (UserManager::Get()->IsUserLoggedIn()) {
      VLOG(1) << "use-shared-proxies not set, defaulting to false/IgnoreProxy.";
      return false;
    } else {
      // Make sure that proxies are always enabled at sign in screen.
      VLOG(1) << "Use proxy on login screen.";
      return true;
    }
  }
  bool use_shared_proxies = false;
  use_shared_proxies_pref->GetValue()->GetAsBoolean(&use_shared_proxies);
  return use_shared_proxies;
}

// static
bool ProxyConfigServiceImpl::IgnoreProxy(const PrefService* pref_service,
                                         const std::string network_profile_path,
                                         onc::ONCSource onc_source) {
  const NetworkProfile* profile =
      NetworkHandler::Get()->network_profile_handler()->
      GetProfileForPath(network_profile_path);
  if (!profile) {
    LOG(WARNING) << "Unknown profile_path " << network_profile_path;
    return true;
  }
  if (profile->type() == NetworkProfile::TYPE_USER) {
    VLOG(1) << "Respect proxy of not-shared networks.";
    return false;
  }

  if (onc_source == onc::ONC_SOURCE_DEVICE_POLICY &&
      UserManager::Get()->IsUserLoggedIn()) {
    policy::BrowserPolicyConnector* connector =
        g_browser_process->browser_policy_connector();
    const User* logged_in_user = UserManager::Get()->GetLoggedInUser();
    if (connector->GetUserAffiliation(logged_in_user->email()) ==
            policy::USER_AFFILIATION_MANAGED) {
      VLOG(1) << "Respecting proxy for network, as logged-in user belongs to "
              << "the domain the device is enrolled to.";
      return false;
    }
  }

  return !GetUseSharedProxies(pref_service);
}

void ProxyConfigServiceImpl::DetermineEffectiveConfigFromDefaultNetwork() {
  const NetworkState* network =
      NetworkHandler::Get()->network_state_handler()->DefaultNetwork();

  // Get prefs proxy config if available.
  net::ProxyConfig pref_config;
  ProxyPrefs::ConfigState pref_state = GetProxyConfig(&pref_config);

  // Get network proxy config if available.
  net::ProxyConfig network_config;
  net::ProxyConfigService::ConfigAvailability network_availability =
      net::ProxyConfigService::CONFIG_UNSET;
  bool ignore_proxy = true;
  if (network) {
    ignore_proxy =
        IgnoreProxy(prefs(), network->profile_path(), network->onc_source());
    // If network is shared but use-shared-proxies is off, use direct mode.
    if (ignore_proxy) {
      VLOG(1) << "Shared network && !use-shared-proxies, use direct";
      network_availability = net::ProxyConfigService::CONFIG_VALID;
    } else if (chromeos::GetProxyConfig(*network, &network_config)) {
      // Network is private or shared with user using shared proxies.
      VLOG(1) << this << ": using network proxy: "
              << network->proxy_config();
      network_availability = net::ProxyConfigService::CONFIG_VALID;
    }
  }

  // Determine effective proxy config, either from prefs or network.
  ProxyPrefs::ConfigState effective_config_state;
  net::ProxyConfig effective_config;
  GetEffectiveProxyConfig(pref_state, pref_config,
                          network_availability, network_config, ignore_proxy,
                          &effective_config_state, &effective_config);

  // Activate effective proxy and store into |active_config_|.
  // If last update didn't complete, we definitely update now.
  bool update_now = update_pending();
  if (!update_now) {  // Otherwise, only update now if there're changes.
    update_now = active_config_state_ != effective_config_state ||
        (active_config_state_ != ProxyPrefs::CONFIG_UNSET &&
         !active_config_.Equals(effective_config));
  }
  if (update_now) {  // Activate and store new effective config.
    active_config_state_ = effective_config_state;
    if (active_config_state_ != ProxyPrefs::CONFIG_UNSET)
      active_config_ = effective_config;
    // If effective config is from system (i.e. network), it's considered a
    // special kind of prefs that ranks below policy/extension but above
    // others, so bump it up to CONFIG_OTHER_PRECEDE to force its precedence
    // when PrefProxyConfigTrackerImpl pushes it to ChromeProxyConfigService.
    if (effective_config_state == ProxyPrefs::CONFIG_SYSTEM)
      effective_config_state = ProxyPrefs::CONFIG_OTHER_PRECEDE;
    // If config is manual, add rule to bypass local host.
    if (effective_config.proxy_rules().type !=
        net::ProxyConfig::ProxyRules::TYPE_NO_RULES)
      effective_config.proxy_rules().bypass_rules.AddRuleToBypassLocal();
    PrefProxyConfigTrackerImpl::OnProxyConfigChanged(effective_config_state,
                                                     effective_config);
    if (VLOG_IS_ON(1) && !update_pending()) {  // Update was successful.
      scoped_ptr<base::DictionaryValue> config_dict(
          effective_config.ToValue());
      VLOG(1) << this << ": Proxy changed: "
              << ProxyPrefs::ConfigStateToDebugString(active_config_state_)
              << ", " << *config_dict;
    }
  }
}

}  // namespace chromeos
