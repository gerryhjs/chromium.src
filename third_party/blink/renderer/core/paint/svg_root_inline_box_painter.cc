// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/paint/svg_root_inline_box_painter.h"

#include "third_party/blink/renderer/core/layout/api/line_layout_api_shim.h"
#include "third_party/blink/renderer/core/layout/svg/line/svg_inline_flow_box.h"
#include "third_party/blink/renderer/core/layout/svg/line/svg_inline_text_box.h"
#include "third_party/blink/renderer/core/layout/svg/line/svg_root_inline_box.h"
#include "third_party/blink/renderer/core/paint/paint_info.h"
#include "third_party/blink/renderer/core/paint/scoped_svg_paint_state.h"
#include "third_party/blink/renderer/core/paint/svg_inline_flow_box_painter.h"
#include "third_party/blink/renderer/core/paint/svg_inline_text_box_painter.h"
#include "third_party/blink/renderer/platform/graphics/paint/drawing_recorder.h"

namespace blink {

void SVGRootInlineBoxPainter::Paint(const PaintInfo& paint_info,
                                    const LayoutPoint& paint_offset) {
  DCHECK(paint_info.phase == PaintPhase::kForeground ||
         paint_info.phase == PaintPhase::kSelection);

  bool has_selection =
      !paint_info.IsPrinting() && svg_root_inline_box_.IsSelected();

  PaintInfo paint_info_before_filtering(paint_info);
  if (has_selection && !DrawingRecorder::UseCachedDrawingIfPossible(
                           paint_info_before_filtering.context,
                           *LineLayoutAPIShim::ConstLayoutObjectFrom(
                               svg_root_inline_box_.GetLineLayoutItem()),
                           paint_info_before_filtering.phase)) {
    DrawingRecorder recorder(paint_info_before_filtering.context,
                             *LineLayoutAPIShim::ConstLayoutObjectFrom(
                                 svg_root_inline_box_.GetLineLayoutItem()),
                             paint_info_before_filtering.phase);
    for (InlineBox* child = svg_root_inline_box_.FirstChild(); child;
         child = child->NextOnLine()) {
      if (auto* svg_inline_text_box = DynamicTo<SVGInlineTextBox>(child))
        SVGInlineTextBoxPainter(*svg_inline_text_box)
            .PaintSelectionBackground(paint_info_before_filtering);
      else if (auto* svg_inline_flow_box = DynamicTo<SVGInlineFlowBox>(child))
        SVGInlineFlowBoxPainter(*svg_inline_flow_box)
            .PaintSelectionBackground(paint_info_before_filtering);
    }
  }

  ScopedSVGPaintState paint_state(*LineLayoutAPIShim::ConstLayoutObjectFrom(
                                      svg_root_inline_box_.GetLineLayoutItem()),
                                  paint_info_before_filtering);
  if (paint_state.ApplyEffects()) {
    for (InlineBox* child = svg_root_inline_box_.FirstChild(); child;
         child = child->NextOnLine())
      child->Paint(paint_state.GetPaintInfo(), paint_offset, LayoutUnit(),
                   LayoutUnit());
  }
}

}  // namespace blink
