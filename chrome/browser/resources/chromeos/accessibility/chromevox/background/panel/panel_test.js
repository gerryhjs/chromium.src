// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Include test fixture.
GEN_INCLUDE([
  '//chrome/browser/resources/chromeos/accessibility/chromevox/testing/chromevox_next_e2e_test_base.js'
]);

GEN_INCLUDE([
  '//chrome/browser/resources/chromeos/accessibility/chromevox/testing/mock_feedback.js'
]);

/**
 * Test fixture for Panel.
 */
ChromeVoxPanelTest = class extends ChromeVoxNextE2ETest {
  /** @override */
  testGenCppIncludes() {
    ChromeVoxE2ETest.prototype.testGenCppIncludes.call(this);
  }

  /**
   * @return {!MockFeedback}
   */
  createMockFeedback() {
    const mockFeedback =
        new MockFeedback(this.newCallback(), this.newCallback.bind(this));
    mockFeedback.install();
    return mockFeedback;
  }

  getPanelWindow() {
    let panelWindow = null;
    while (!panelWindow) {
      panelWindow = chrome.extension.getViews().find(function(view) {
        return view.location.href.indexOf('background/panel.html') > 0;
      });
    }
    return panelWindow;
  }

  /**
   * Gets the Panel object in the panel.html window. Note that the extension
   * system destroys our reference to this object unpredictably so always ask
   * chrome.extension.getViews for it.
   */
  getPanel() {
    return this.getPanelWindow().Panel;
  }

  fireMockEvent(key) {
    return function() {
      const obj = {};
      obj.preventDefault = function() {};
      obj.stopPropagation = function() {};
      obj.key = key;
      this.getPanel().onKeyDown(obj);
    }.bind(this);
  }

  get linksDoc() {
    return `
      <p>start</p>
      <a href="#">apple</a>
      <a href="#">grape</a>
      <a href="#">banana</a>
    `;
  }
};


// TODO: Flaky timeouts. https://crbug.com/795840 and https://crbug.com/990229
TEST_F('ChromeVoxPanelTest', 'DISABLED_ActivateMenu', function() {
  const mockFeedback = this.createMockFeedback();
  this.runWithLoadedTree(this.linksDoc, function(root) {
    const openMenus = new PanelCommand(PanelCommandType.OPEN_MENUS);
    mockFeedback.call(openMenus.send.bind(openMenus))
        .expectSpeech(
            'Jump', 'Menu',
            'Go To Beginning Of Table ChromeVox+Alt+Shift+Left arrow',
            / 1 of [0-9]{2} /)
        .call(this.fireMockEvent('ArrowRight'))
        .expectSpeech(
            'Speech', 'Menu',
            'Announce The Title Of The Current Page ChromeVox+A>W', 'Menu item',
            / 1 of [0-9]{2} /)
        .replay();
  });
});

// TODO: Flaky timeouts. https://crbug.com/795840 and https://crbug.com/990229
TEST_F('ChromeVoxPanelTest', 'DISABLED_LinkMenu', function() {
  const mockFeedback = this.createMockFeedback();
  this.runWithLoadedTree(this.linksDoc, function(root) {
    const openMenus =
        new PanelCommand(PanelCommandType.OPEN_MENUS, 'role_link');
    mockFeedback.call(openMenus.send.bind(openMenus))
        .expectSpeech(
            'Link',
            'Menu',
            'apple Link',
            'Menu item',
            ' 1 of 3 ',
            )
        .call(this.fireMockEvent('ArrowLeft'))
        .expectSpeech('Landmark', 'Menu', 'No items.', 'Menu item', ' 1 of 1 ')
        .call(this.fireMockEvent('ArrowRight'))
        .expectSpeech('Link', 'Menu', 'apple Link', 'Menu item', ' 1 of 3 ')
        .call(this.fireMockEvent('ArrowUp'))
        .expectSpeech('banana Link', 'Menu item', ' 3 of 3 ')
        .clearPendingOutput()
        .call(this.fireMockEvent('Enter'))
        .expectSpeech('banana', 'Link')
        .replay();
  });
});
