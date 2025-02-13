// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_BROWSER_AGENT_INTERACTION_HANDLERS_PASSWORDS_PASSWORD_INFOBAR_MODAL_INTERACTION_HANDLER_H_
#define IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_BROWSER_AGENT_INTERACTION_HANDLERS_PASSWORDS_PASSWORD_INFOBAR_MODAL_INTERACTION_HANDLER_H_

#import "ios/chrome/browser/infobars/overlays/browser_agent/interaction_handlers/common/infobar_modal_interaction_handler.h"

@protocol ApplicationSettingsCommands;
class Browser;
class IOSChromeSavePasswordInfoBarDelegate;

class PasswordInfobarModalInteractionHandler
    : public InfobarModalInteractionHandler {
 public:
  PasswordInfobarModalInteractionHandler(Browser* browser);
  ~PasswordInfobarModalInteractionHandler() override;

  // Instructs the handler to update the credentials with |username| and
  // |password| for interaction with |infobar|'s modal UI.
  // TODO(crbug.com/1040653): This function is only virtual so it can be mocked
  // for testing purposes.  It should become non-virtual once the password
  // infobar delegate is refactored for testability.
  virtual void UpdateCredentials(InfoBarIOS* infobar,
                                 NSString* username,
                                 NSString* password);
  // Instructs the handler that the user has used |infobar|'s modal UI to
  // request that credentials are never saved for the current site.
  // TODO(crbug.com/1040653): This function is only virtual so it can be mocked
  // for testing purposes.  It should become non-virtual once the password
  // infobar delegate is refactored for testability.
  virtual void NeverSaveCredentials(InfoBarIOS* infobar);
  // Instructs the handler that the user has requested the passwords settings
  // page through |infobar|'s modal UI.  The settings will be presented after
  // the dismissal of |infobar|'s modal UI.
  // TODO(crbug.com/1040653): This function is only virtual so it can be mocked
  // for testing purposes.  It should become non-virtual once the password
  // infobar delegate is refactored for testability.
  virtual void PresentPasswordsSettings(InfoBarIOS* infobar);

  // InfobarModalInteractionHandler:
  void PerformMainAction(InfoBarIOS* infobar) override;

  // InfobarInteractionHandler::Handler:
  void InfobarVisibilityChanged(InfoBarIOS* infobar, bool visible) override;

 protected:
  // TODO(crbug.com/1040653): This class is only subclassed as a mock for use in
  // tests.  This constructor can be removed once the password infobar delegate
  // is refactored for testing and this class no longer needs to be mocked.
  PasswordInfobarModalInteractionHandler();

 private:
  // InfobarModalInteractionHandler:
  std::unique_ptr<InfobarModalOverlayRequestCallbackInstaller>
  CreateModalInstaller() override;

  // Returns the password delegate from |infobar|.
  IOSChromeSavePasswordInfoBarDelegate* GetDelegate(InfoBarIOS* infobar);

  // Dispatcher used to open the password settings.
  id<ApplicationSettingsCommands> settings_command_handler_ = nil;
};

#endif  // IOS_CHROME_BROWSER_INFOBARS_OVERLAYS_BROWSER_AGENT_INTERACTION_HANDLERS_PASSWORDS_PASSWORD_INFOBAR_MODAL_INTERACTION_HANDLER_H_
