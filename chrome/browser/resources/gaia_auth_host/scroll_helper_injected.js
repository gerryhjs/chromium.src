// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * -- WebviewScrollShadowsHelper --
 *
 *  Sends scroll information from within the webview. This information is used
 *  to appropriately add classes to the webview in order to display shadows on
 *  top of it. The shadows show to the user if there is content hidden that
 *  could be seen if the user would scroll up/down.
 */

/* #export */ const WebviewScrollShadowsHelper = (function() {
  function WebviewScrollShadowsHelper() {}

  WebviewScrollShadowsHelper.prototype = {
    init(channel) {
      this.channel_ = channel;

      window.addEventListener('scroll', this.sendScrollInfo_.bind(this));
      window.addEventListener('resize', this.sendScrollInfo_.bind(this));

      // Besides binding to the 'scroll' and 'resize' events, send once the
      // scroll information when the page if fully loaded.
      if (document.readyState === 'complete') {
        this.sendScrollInfo_();
        return;
      }

      const boundSendScrollInfo = this.sendScrollInfo_.bind(this);
      window.addEventListener('readystatechange', function listener(event) {
        if (document.readyState != 'complete') {
          return;
        }
        boundSendScrollInfo();
        window.removeEventListener(event.type, listener, true);
      }, true);
    },

    sendScrollInfo_(event) {
      this.channel_.send({
        name: 'scrollInfo',
        scrollTop: window.scrollY,
        scrollHeight: document.body.scrollHeight
      });
    },
  };

  return WebviewScrollShadowsHelper;
})();

const WebviewScrollShadowsHelperConstructor = function() {
  return new WebviewScrollShadowsHelper();
};
