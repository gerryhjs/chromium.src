// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_LIST_LAYOUT_NG_LIST_ITEM_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_LIST_LAYOUT_NG_LIST_ITEM_H_

#include "third_party/blink/renderer/core/core_export.h"
#include "third_party/blink/renderer/core/dom/pseudo_element.h"
#include "third_party/blink/renderer/core/html/list_item_ordinal.h"
#include "third_party/blink/renderer/core/layout/ng/layout_ng_block_flow.h"

namespace blink {

// A LayoutObject subclass for 'display: list-item' in LayoutNG.
class CORE_EXPORT LayoutNGListItem final : public LayoutNGBlockFlow {
 public:
  explicit LayoutNGListItem(Element*);

  ListItemOrdinal& Ordinal() { return ordinal_; }

  int Value() const;

  LayoutObject* Marker() const {
    Element* list_item = To<Element>(GetNode());
    if (PseudoElement* marker = list_item->GetPseudoElement(kPseudoIdMarker))
      return marker->GetLayoutObject();
    return nullptr;
  }

  void UpdateMarkerTextIfNeeded();

  void OrdinalValueChanged();
  void WillCollectInlines() override;

  static const LayoutObject* FindSymbolMarkerLayoutText(const LayoutObject*);

  const char* GetName() const override { return "LayoutNGListItem"; }

 private:
  bool IsOfType(LayoutObjectType) const override;

  void InsertedIntoTree() override;
  void WillBeRemovedFromTree() override;
  void StyleDidChange(StyleDifference, const ComputedStyle* old_style) override;
  void SubtreeDidChange() final;

  bool IsInside() const;

  ListItemOrdinal ordinal_;
};

DEFINE_LAYOUT_OBJECT_TYPE_CASTS(LayoutNGListItem, IsLayoutNGListItem());

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_LIST_LAYOUT_NG_LIST_ITEM_H_
