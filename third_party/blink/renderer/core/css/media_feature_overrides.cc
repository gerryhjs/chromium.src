// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/media_feature_overrides.h"

#include "third_party/blink/renderer/core/css/parser/css_parser_context.h"
#include "third_party/blink/renderer/core/css/parser/css_parser_token_range.h"
#include "third_party/blink/renderer/core/css/parser/css_tokenizer.h"

namespace blink {

void MediaFeatureOverrides::SetOverride(const AtomicString& feature,
                                        const String& value_string) {
  CSSTokenizer tokenizer(value_string);
  const auto tokens = tokenizer.TokenizeToEOF();
  CSSParserTokenRange range(tokens);

  // TODO(xiaochengh): This is a fake CSSParserContext that only passes
  // down the CSSParserMode. Plumb the real CSSParserContext through, so that
  // web features can be counted correctly.
  const CSSParserContext* fake_context = MakeGarbageCollected<CSSParserContext>(
      kHTMLStandardMode, SecureContextMode::kInsecureContext);

  auto value = MediaQueryExp::Create(feature, range, *fake_context).ExpValue();

  if (value.IsValid())
    overrides_.Set(feature, value);
  else
    overrides_.erase(feature);
}

}  // namespace blink
