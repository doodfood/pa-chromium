// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/basictypes.h"
#include "base/strings/string_piece.h"
#include "googleurl/src/gurl.h"
#include "webkit/common/webkit_storage_common_export.h"

namespace webkit_database {

class WEBKIT_STORAGE_COMMON_EXPORT DatabaseIdentifier {
 public:
  static const DatabaseIdentifier UniqueFileIdentifier();
  static DatabaseIdentifier CreateFromOrigin(const GURL& origin);
  static DatabaseIdentifier Parse(const std::string& identifier);
  ~DatabaseIdentifier();

  std::string ToString() const;
  GURL ToOrigin() const;

  std::string scheme() const { return scheme_; }
  std::string hostname() const { return hostname_; }
  int port() const { return port_; }
  bool is_unique() const { return is_unique_; }

 private:
  DatabaseIdentifier();
  DatabaseIdentifier(const std::string& scheme,
                     const std::string& hostname,
                     int port,
                     bool is_unique,
                     bool is_file);

  std::string scheme_;
  std::string hostname_;
  int port_;
  bool is_unique_;
  bool is_file_;
};

}  // namespace webkit_database
