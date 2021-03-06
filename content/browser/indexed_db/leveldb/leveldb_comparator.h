// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_INDEXED_DB_LEVELDB_LEVELDB_COMPARATOR_H_
#define CONTENT_BROWSER_INDEXED_DB_LEVELDB_LEVELDB_COMPARATOR_H_

#include "base/strings/string16.h"

namespace content {

class LevelDBSlice;

class LevelDBComparator {
 public:
  virtual ~LevelDBComparator() {}

  virtual int Compare(const LevelDBSlice& a, const LevelDBSlice& b) const = 0;
  virtual const char* Name() const = 0;
};

}  // namespace content

#endif  // CONTENT_BROWSER_INDEXED_DB_LEVELDB_LEVELDB_COMPARATOR_H_
