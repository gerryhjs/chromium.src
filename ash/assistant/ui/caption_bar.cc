// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/assistant/ui/caption_bar.h"

#include "ash/assistant/model/assistant_ui_model.h"
#include "ash/assistant/ui/assistant_ui_constants.h"
#include "ash/assistant/ui/base/assistant_button.h"
#include "ash/public/cpp/vector_icons/vector_icons.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/event_monitor.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/vector_icons/vector_icons.h"

namespace ash {

namespace {

// Appearance.
constexpr int kCaptionButtonSizeDip = 32;
constexpr int kPreferredHeightDip = 48;
constexpr int kVectorIconSizeDip = 12;

// CaptionButton ---------------------------------------------------------------

std::unique_ptr<AssistantButton> CreateCaptionButton(
    const gfx::VectorIcon& icon,
    int accessible_name_id,
    AssistantButtonId button_id,
    AssistantButtonListener* listener) {
  return AssistantButton::Create(listener, icon, kCaptionButtonSizeDip,
                                 kVectorIconSizeDip, accessible_name_id,
                                 button_id);
}

}  // namespace

// CaptionBar ------------------------------------------------------------------

CaptionBar::CaptionBar() {
  InitLayout();
}

CaptionBar::~CaptionBar() = default;

const char* CaptionBar::GetClassName() const {
  return "CaptionBar";
}

gfx::Size CaptionBar::CalculatePreferredSize() const {
  return gfx::Size(INT_MAX, GetHeightForWidth(INT_MAX));
}

int CaptionBar::GetHeightForWidth(int width) const {
  return kPreferredHeightDip;
}

void CaptionBar::VisibilityChanged(views::View* starting_from, bool visible) {
  if (!IsDrawn()) {
    event_monitor_.reset();
    return;
  }

  views::Widget* widget = GetWidget();
  if (!widget)
    return;

  // Only when the CaptionBar is drawn do we allow it to monitor key press
  // events. We monitor key press events to handle hotkeys that behave the same
  // as caption bar buttons. Note that we use an EventMonitor rather than adding
  // accelerators so that we always receive key events, even if an embedded
  // navigable contents in our view hierarchy has focus.
  gfx::NativeWindow root_window = widget->GetNativeWindow()->GetRootWindow();
  event_monitor_ = views::EventMonitor::CreateWindowMonitor(
      this, root_window, {ui::ET_KEY_PRESSED});
}

void CaptionBar::OnButtonPressed(AssistantButtonId button_id) {
  HandleButton(button_id);
}

void CaptionBar::OnEvent(const ui::Event& event) {
  const ui::KeyEvent& key_event = static_cast<const ui::KeyEvent&>(event);
  switch (key_event.key_code()) {
    case ui::VKEY_BROWSER_BACK:
      HandleButton(AssistantButtonId::kBack);
      break;
    case ui::VKEY_ESCAPE:
      HandleButton(AssistantButtonId::kClose);
      break;
    case ui::VKEY_W:
      if (key_event.IsControlDown())
        HandleButton(AssistantButtonId::kClose);
      break;
    default:
      // No action necessary.
      break;
  }
}

void CaptionBar::SetButtonVisible(AssistantButtonId id, bool visible) {
  views::View* button = GetButtonWithId(id);
  if (button)
    button->SetVisible(visible);
}

void CaptionBar::InitLayout() {
  views::BoxLayout* layout_manager =
      SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets(0, kSpacingDip), kSpacingDip));

  layout_manager->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Back.
  AddButton(CreateCaptionButton(kWindowControlBackIcon, IDS_APP_LIST_BACK,
                                AssistantButtonId::kBack, this));

  // Spacer.
  auto spacer = std::make_unique<views::View>();
  layout_manager->SetFlexForView(AddChildView(std::move(spacer)), 1);

  // Minimize.
  AddButton(CreateCaptionButton(views::kWindowControlMinimizeIcon,
                                IDS_APP_ACCNAME_MINIMIZE,
                                AssistantButtonId::kMinimize, this));

  // Close.
  AddButton(CreateCaptionButton(views::kWindowControlCloseIcon,
                                IDS_APP_ACCNAME_CLOSE,
                                AssistantButtonId::kClose, this));
}

void CaptionBar::AddButton(std::unique_ptr<AssistantButton> button) {
  buttons_.push_back(button.get());
  AddChildView(std::move(button));
}

void CaptionBar::HandleButton(AssistantButtonId id) {
  if (!GetButtonWithId(id)->GetVisible())
    return;

  // If the delegate returns |true| it has handled the event and wishes to
  // prevent default behavior from being performed.
  if (delegate_ && delegate_->OnCaptionButtonPressed(id))
    return;

  switch (id) {
    case AssistantButtonId::kClose:
      GetWidget()->Close();
      break;
    default:
      // No default behavior defined.
      NOTIMPLEMENTED();
      break;
  }
}

AssistantButton* CaptionBar::GetButtonWithId(AssistantButtonId id) {
  for (auto* button : buttons_) {
    if (button->GetAssistantButtonId() == id)
      return button;
  }
  return nullptr;
}

}  // namespace ash
