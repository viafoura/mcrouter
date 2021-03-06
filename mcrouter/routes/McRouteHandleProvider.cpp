/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "McRouteHandleProvider.h"

#include "mcrouter/lib/network/MessageHelpers.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/routes/AllAsyncRouteFactory.h"
#include "mcrouter/routes/AllFastestRouteFactory.h"
#include "mcrouter/routes/AllInitialRouteFactory.h"
#include "mcrouter/routes/AllMajorityRouteFactory.h"
#include "mcrouter/routes/AllSyncRouteFactory.h"
#include "mcrouter/routes/BlackholeRoute.h"
#include "mcrouter/routes/CarbonLookasideRoute.h"
#include "mcrouter/routes/DevNullRoute.h"
#include "mcrouter/routes/ErrorRoute.h"
#include "mcrouter/routes/FailoverRoute.h"
#include "mcrouter/routes/FailoverWithExptimeRouteFactory.h"
#include "mcrouter/routes/HashRouteFactory.h"
#include "mcrouter/routes/HostIdRouteFactory.h"
#include "mcrouter/routes/KeySplitRoute.h"
#include "mcrouter/routes/L1L2CacheRouteFactory.h"
#include "mcrouter/routes/L1L2SizeSplitRoute.h"
#include "mcrouter/routes/LatencyInjectionRoute.h"
#include "mcrouter/routes/LatestRoute.h"
#include "mcrouter/routes/LoadBalancerRoute.h"
#include "mcrouter/routes/LoggingRoute.h"
#include "mcrouter/routes/McExtraRouteHandleProvider.h"
#include "mcrouter/routes/MigrateRouteFactory.h"
#include "mcrouter/routes/MissFailoverRoute.h"
#include "mcrouter/routes/ModifyExptimeRoute.h"
#include "mcrouter/routes/ModifyKeyRoute.h"
#include "mcrouter/routes/OperationSelectorRoute.h"
#include "mcrouter/routes/OutstandingLimitRoute.h"
#include "mcrouter/routes/RandomRouteFactory.h"
#include "mcrouter/routes/RoutingGroupRoute.h"
#include "mcrouter/routes/ShadowRoute.h"
#include "mcrouter/routes/StagingRoute.h"

namespace folly {
struct dynamic;
}

namespace facebook {
namespace memcache {
namespace mcrouter {

using McRouteHandleFactory = RouteHandleFactory<McrouterRouteHandleIf>;
using MemcacheRouterInfo = facebook::memcache::MemcacheRouterInfo;

/**
 * This implementation is only for test purposes. Typically the users of
 * CarbonLookaside will be services other than memcache.
 */
class MemcacheCarbonLookasideHelper {
 public:
  MemcacheCarbonLookasideHelper(const folly::dynamic* /* jsonConfig */) {}

  static std::string name() {
    return "MemcacheCarbonLookasideHelper";
  }

  template <typename Request>
  bool cacheCandidate(const Request& /* unused */) const {
    if (HasKeyTrait<Request>::value) {
      return true;
    }
    return false;
  }

  template <typename Request>
  std::string buildKey(const Request& req) const {
    if (HasKeyTrait<Request>::value) {
      return req.key_ref()->fullKey().str();
    }
    return std::string();
  }

  template <typename Reply>
  bool shouldCacheReply(const Reply& /* unused */) const {
    return true;
  }

  template <typename Reply>
  void postProcessCachedReply(Reply& /* reply */) const {}
};

McrouterRouteHandlePtr makeWarmUpRoute(
    McRouteHandleFactory& factory,
    const folly::dynamic& json);

template <>
std::unique_ptr<ExtraRouteHandleProviderIf<MemcacheRouterInfo>>
McRouteHandleProvider<MemcacheRouterInfo>::buildExtraProvider() {
  return std::make_unique<McExtraRouteHandleProvider<MemcacheRouterInfo>>();
}

template <>
std::shared_ptr<MemcacheRouterInfo::RouteHandleIf>
McRouteHandleProvider<MemcacheRouterInfo>::createSRRoute(
    RouteHandleFactory<MemcacheRouterInfo::RouteHandleIf>& factory,
    const folly::dynamic& json) {
  if (makeSRRoute) {
    auto route = makeSRRoute(factory, json, proxy_);

    bool needAsynclog = true;
    if (json.isObject()) {
      if (auto* jNeedAsynclog = json.get_ptr("asynclog")) {
        needAsynclog = parseBool(*jNeedAsynclog, "asynclog");
      }

      if (needAsynclog) {
        auto jAsynclogName = json.get_ptr("service_name");
        checkLogic(
            jAsynclogName != nullptr,
            "AsynclogRoute over SRRoute: 'service_name' property is missing");
        auto asynclogName = parseString(*jAsynclogName, "service_name");
        return createAsynclogRoute(std::move(route), asynclogName.toString());
      }
      return route;
    }
  }

  throwLogic("SRRoute is not implemented for this router");
}

template <>
typename McRouteHandleProvider<MemcacheRouterInfo>::RouteHandleFactoryMap
McRouteHandleProvider<MemcacheRouterInfo>::buildRouteMap() {
  RouteHandleFactoryMap map{
      {"AllAsyncRoute", &makeAllAsyncRoute<MemcacheRouterInfo>},
      {"AllFastestRoute", &makeAllFastestRoute<MemcacheRouterInfo>},
      {"AllInitialRoute", &makeAllInitialRoute<MemcacheRouterInfo>},
      {"AllMajorityRoute", &makeAllMajorityRoute<MemcacheRouterInfo>},
      {"AllSyncRoute", &makeAllSyncRoute<MemcacheRouterInfo>},
      {"BlackholeRoute", &makeBlackholeRoute<MemcacheRouterInfo>},
      {"CarbonLookasideRoute",
       &createCarbonLookasideRoute<
           MemcacheRouterInfo,
           MemcacheCarbonLookasideHelper>},
      {"DevNullRoute", &makeDevNullRoute<MemcacheRouterInfo>},
      {"ErrorRoute", &makeErrorRoute<MemcacheRouterInfo>},
      {"FailoverWithExptimeRoute",
       &makeFailoverWithExptimeRoute<MemcacheRouterInfo>},
      {"HashRoute",
       [](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return makeHashRoute<McrouterRouterInfo>(factory, json);
       }},
      {"HostIdRoute", &makeHostIdRoute<MemcacheRouterInfo>},
      {"LatencyInjectionRoute", &makeLatencyInjectionRoute<MemcacheRouterInfo>},
      {"L1L2CacheRoute", &makeL1L2CacheRoute<MemcacheRouterInfo>},
      {"L1L2SizeSplitRoute", &makeL1L2SizeSplitRoute},
      {"KeySplitRoute", &makeKeySplitRoute},
      {"LatestRoute", &makeLatestRoute<MemcacheRouterInfo>},
      {"LoadBalancerRoute", &makeLoadBalancerRoute<MemcacheRouterInfo>},
      {"LoggingRoute", &makeLoggingRoute<MemcacheRouterInfo>},
      {"MigrateRoute", &makeMigrateRoute<MemcacheRouterInfo>},
      {"MissFailoverRoute", &makeMissFailoverRoute<MemcacheRouterInfo>},
      {"ModifyKeyRoute", &makeModifyKeyRoute<MemcacheRouterInfo>},
      {"ModifyExptimeRoute", &makeModifyExptimeRoute<MemcacheRouterInfo>},
      {"NullRoute", &makeNullRoute<MemcacheRouteHandleIf>},
      {"OperationSelectorRoute",
       &makeOperationSelectorRoute<MemcacheRouterInfo>},
      {"PoolRoute",
       [this](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return makePoolRoute(factory, json);
       }},
      {"PrefixPolicyRoute", &makeOperationSelectorRoute<MemcacheRouterInfo>},
      {"RandomRoute", &makeRandomRoute<MemcacheRouterInfo>},
      {"RateLimitRoute",
       [](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return makeRateLimitRoute(factory, json);
       }},
      {"RoutingGroupRoute", &makeRoutingGroupRoute<MemcacheRouterInfo>},
      {"StagingRoute", &makeStagingRoute},
      {"SRRoute",
       [this](McRouteHandleFactory& factory, const folly::dynamic& json) {
         return createSRRoute(factory, json);
       }},
      {"WarmUpRoute", &makeWarmUpRoute},
  };
  return map;
}

template <>
typename McRouteHandleProvider<
    MemcacheRouterInfo>::RouteHandleFactoryMapWithProxy
McRouteHandleProvider<MemcacheRouterInfo>::buildRouteMapWithProxy() {
  return RouteHandleFactoryMapWithProxy();
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
