// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FRAME_HOST_NAVIGATION_REQUEST_INFO_H_
#define CONTENT_BROWSER_FRAME_HOST_NAVIGATION_REQUEST_INFO_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/unguessable_token.h"
#include "content/common/content_export.h"
#include "content/common/navigation_params.h"
#include "content/common/navigation_params.mojom.h"
#include "content/public/common/referrer.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {

// A struct to hold the parameters needed to start a navigation request in
// ResourceDispatcherHost. It is initialized on the UI thread, and then passed
// to the IO thread by a NavigationRequest object.
struct CONTENT_EXPORT NavigationRequestInfo {
  NavigationRequestInfo(mojom::CommonNavigationParamsPtr common_params,
                        mojom::BeginNavigationParamsPtr begin_params,
                        const net::SiteForCookies& site_for_cookies,
                        const net::NetworkIsolationKey& network_isolation_key,
                        bool is_main_frame,
                        bool parent_is_main_frame,
                        bool are_ancestors_secure,
                        int frame_tree_node_id,
                        bool is_for_guests_only,
                        bool report_raw_headers,
                        bool is_prerendering,
                        bool upgrade_if_insecure,
                        std::unique_ptr<network::PendingSharedURLLoaderFactory>
                            blob_url_loader_factory,
                        const base::UnguessableToken& devtools_navigation_token,
                        const base::UnguessableToken& devtools_frame_token,
                        bool obey_origin_policy, bool nw_trust = false);
  NavigationRequestInfo(const NavigationRequestInfo& other) = delete;
  ~NavigationRequestInfo();

  mojom::CommonNavigationParamsPtr common_params;
  mojom::BeginNavigationParamsPtr begin_params;

  // Used to check which URLs (if any) are third-party for purposes of cookie
  // blocking policy.
  const net::SiteForCookies site_for_cookies;

  // Navigation resource requests will be keyed using |network_isolation_key|
  // for accessing shared network resources like the http cache.
  const net::NetworkIsolationKey network_isolation_key;

  const bool is_main_frame;
  const bool parent_is_main_frame;

  // Whether all ancestor frames of the frame that is navigating have a secure
  // origin. True for main frames.
  const bool are_ancestors_secure;

  const int frame_tree_node_id;

  const bool is_for_guests_only;

  const bool report_raw_headers;

  const bool is_prerendering;

  // If set to true, any HTTP redirects of this request will be upgraded to
  // HTTPS. This only applies for subframe navigations.
  const bool upgrade_if_insecure;

  // URLLoaderFactory to facilitate loading blob URLs.
  std::unique_ptr<network::PendingSharedURLLoaderFactory>
      blob_url_loader_factory;

  const base::UnguessableToken devtools_navigation_token;

  const base::UnguessableToken devtools_frame_token;

  // If set, the network service will attempt to retrieve the appropriate origin
  // policy, if necessary, and attach it to the ResourceResponseHead.
  // Spec: https://wicg.github.io/origin-policy/
  const bool obey_origin_policy;
  bool nw_trusted;
};

}  // namespace content

#endif  // CONTENT_BROWSER_FRAME_HOST_NAVIGATION_REQUEST_INFO_H_
