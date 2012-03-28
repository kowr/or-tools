// Copyright 2010 Google
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef OR_TOOLS_BASE_STRUTIL_H_
#define OR_TOOLS_BASE_STRUTIL_H_

#include "base/stringpiece.h"

namespace operations_research {
// ----------------------------------------------------------------------
// HasSuffixString()
//    Return true if str ends in suffix.
// ----------------------------------------------------------------------

inline bool HasSuffixString(const StringPiece& str,
                            const StringPiece& suffix) {
  return str.ends_with(suffix);
}
}  // namespace operations_research
#endif  // OR_TOOLS_BASE_STRUTIL_H_