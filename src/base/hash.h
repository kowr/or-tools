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

#ifndef OR_TOOLS_BASE_HASH_H_
#define OR_TOOLS_BASE_HASH_H_

// Hash maps and hash sets are compiler dependant.
#ifdef __GNUC__
#include <ext/hash_map>
#include <ext/hash_set>
namespace operations_research {
  using namespace __gnu_cxx;
}  // namespace operations_research
#else
#include <hash_map>
#include <hash_set>
#endif
#include <string>
#include <utility>

#include "base/basictypes.h"

namespace operations_research {
static inline void mix(uint32& a, uint32& b, uint32& c) {     // 32bit version
  a -= b; a -= c; a ^= (c>>13);
  b -= c; b -= a; b ^= (a<<8);
  c -= a; c -= b; c ^= (b>>13);
  a -= b; a -= c; a ^= (c>>12);
  b -= c; b -= a; b ^= (a<<16);
  c -= a; c -= b; c ^= (b>>5);
  a -= b; a -= c; a ^= (c>>3);
  b -= c; b -= a; b ^= (a<<10);
  c -= a; c -= b; c ^= (b>>15);
}

static inline void mix(uint64& a, uint64& b, uint64& c) {     // 64bit version
  a -= b; a -= c; a ^= (c>>43);
  b -= c; b -= a; b ^= (a<<9);
  c -= a; c -= b; c ^= (b>>8);
  a -= b; a -= c; a ^= (c>>38);
  b -= c; b -= a; b ^= (a<<23);
  c -= a; c -= b; c ^= (b>>5);
  a -= b; a -= c; a ^= (c>>35);
  b -= c; b -= a; b ^= (a<<49);
  c -= a; c -= b; c ^= (b>>11);
  a -= b; a -= c; a ^= (c>>12);
  b -= c; b -= a; b ^= (a<<18);
  c -= a; c -= b; c ^= (b>>22);
}
}  // namespace operations_research
#if !defined(SWIG)
#if !defined(_MSC_VER)
namespace __gnu_cxx {
template<class T> struct hash<T*> {
  size_t operator()(T *x) const { return reinterpret_cast<size_t>(x); }
};

template<> struct hash<int64> {
  size_t operator()(int64 x) const { return static_cast<size_t>(x); }
};

template<> struct hash<std::string> {
  size_t operator()(const std::string& x) const {
    size_t hash = 0;
    int c;
    const char* s = x.c_str();
    while (c = *s++) {
      hash = ((hash << 5) + hash) ^ c;
    }
    return hash;
  }
};

inline uint32 Hash32NumWithSeed(uint32 num, uint32 c) {
  uint32 b = 0x9e3779b9UL;            // the golden ratio; an arbitrary value
  operations_research::mix(num, b, c);
  return c;
}

inline uint64 Hash64NumWithSeed(uint64 num, uint64 c) {
  uint64 b = GG_ULONGLONG(0xe08c1d668b756f82);   // more of the golden ratio
  operations_research::mix(num, b, c);
  return c;
}

template<class First, class Second>
struct hash<std::pair<First, Second> > {
  size_t operator()(const std::pair<First, Second>& p) const {
    size_t h1 = hash<First>()(p.first);
    size_t h2 = hash<Second>()(p.second);
    // The decision below is at compile time
    return (sizeof(h1) <= sizeof(uint32)) ?
        Hash32NumWithSeed(h1, h2)
        : Hash64NumWithSeed(h1, h2);
  }
};
}  // namespace __gnu_cxx
#else  // !defined(_MSC_VER)
// The following class defines a hash function for pair<int64, int64>.
class PairInt64Hasher : public stdext::hash_compare <std::pair<int64, int64> > {
 public:
  size_t operator() (const std::pair<int64, int64>& a) const {
    uint64 x = a.first;
    uint64 y = GG_ULONGLONG(0xe08c1d668b756f82);
    uint64 z = a.second;
    operations_research::mix(x, y, z);
    return z;
  }
  bool operator() (const std::pair<int64, int64>& a1,
                   const std::pair<int64, int64>& a2) const {
    return a1.first < a2.first ||
        (a1.first == a2.first && a1.second < a2.second);
  }
};

class PairIntHasher : public stdext::hash_compare <std::pair<int, int> > {
 public:
  size_t operator() (const std::pair<int, int>& a) const {
    uint32 x = a.first;
    uint32 y = 0x9e3779b9UL;
    uint32 z = a.second;
    operations_research::mix(x, y, z);
    return z;
  }
  bool operator() (const std::pair<int, int>& a1,
                   const std::pair<int, int>& a2) const {
    return a1.first < a2.first ||
        (a1.first == a2.first && a1.second < a2.second);
  }
};
#endif  // defined(_MSC_VER)
#endif  // defined(SWIG)

#if !defined(SWIG)
# if defined(__GNUC__)
using __gnu_cxx::hash;
using __gnu_cxx::hash_set;
# elif defined(_MSC_VER)
using std::hash;
using stdext::hash_map;
using stdext::hash_set;
# else
using std::hash;
using std::hash_map;
using std::hash_set;
# endif
#endif

#endif  // OR_TOOLS_BASE_HASH_H_