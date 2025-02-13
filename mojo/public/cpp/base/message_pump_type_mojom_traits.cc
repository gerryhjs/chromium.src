// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/base/message_pump_type_mojom_traits.h"
#include "build/build_config.h"

namespace mojo {

// static
mojo_base::mojom::MessagePumpType
EnumTraits<mojo_base::mojom::MessagePumpType, base::MessagePumpType>::ToMojom(
    base::MessagePumpType input) {
  switch (input) {
    case base::MessagePumpType::DEFAULT:
      return mojo_base::mojom::MessagePumpType::kDefault;
    case base::MessagePumpType::UI:
      return mojo_base::mojom::MessagePumpType::kUi;
    case base::MessagePumpType::CUSTOM:
      return mojo_base::mojom::MessagePumpType::kCustom;
    case base::MessagePumpType::IO:
      return mojo_base::mojom::MessagePumpType::kIo;
    case base::MessagePumpType::NODE:
      return mojo_base::mojom::MessagePumpType::kNode;
#if defined(OS_ANDROID)
    case base::MessagePumpType::JAVA:
      return mojo_base::mojom::MessagePumpType::kJava;
#endif
#if defined(OS_MACOSX)
    case base::MessagePumpType::NS_RUNLOOP:
      return mojo_base::mojom::MessagePumpType::kNsRunloop;
#endif
#if defined(OS_WIN)
    case base::MessagePumpType::UI_WITH_WM_QUIT_SUPPORT:
      return mojo_base::mojom::MessagePumpType::kUiWithWmQuitSupport;
#endif
  }
  NOTREACHED();
  return mojo_base::mojom::MessagePumpType::kDefault;
}

// static
bool EnumTraits<mojo_base::mojom::MessagePumpType, base::MessagePumpType>::
    FromMojom(mojo_base::mojom::MessagePumpType input,
              base::MessagePumpType* output) {
  switch (input) {
    case mojo_base::mojom::MessagePumpType::kDefault:
      *output = base::MessagePumpType::DEFAULT;
      return true;
    case mojo_base::mojom::MessagePumpType::kUi:
      *output = base::MessagePumpType::UI;
      return true;
    case mojo_base::mojom::MessagePumpType::kCustom:
      *output = base::MessagePumpType::CUSTOM;
      return true;
    case mojo_base::mojom::MessagePumpType::kIo:
      *output = base::MessagePumpType::IO;
      return true;
    case mojo_base::mojom::MessagePumpType::kNode:
      *output = base::MessagePumpType::NODE;
      return true;
#if defined(OS_ANDROID)
    case mojo_base::mojom::MessagePumpType::kJava:
      *output = base::MessagePumpType::JAVA;
      return true;
#endif
#if defined(OS_MACOSX)
    case mojo_base::mojom::MessagePumpType::kNsRunloop:
      *output = base::MessagePumpType::NS_RUNLOOP;
      return true;
#endif
#if defined(OS_WIN)
    case mojo_base::mojom::MessagePumpType::kUiWithWmQuitSupport:
      *output = base::MessagePumpType::UI_WITH_WM_QUIT_SUPPORT;
      return true;
#endif
  }
  return false;
}

}  // namespace mojo
