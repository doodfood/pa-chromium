/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef LIBRARIES_NACL_IO_MOUNT_HTTP_H_
#define LIBRARIES_NACL_IO_MOUNT_HTTP_H_

#include <string>
#include "nacl_io/mount.h"
#include "nacl_io/pepper_interface.h"

class MountNode;
class MountNodeDir;
class MountNodeHttp;
class MountHttpMock;

std::string NormalizeHeaderKey(const std::string& s);

class MountHttp : public Mount {
 public:
  typedef std::map<std::string, MountNode*> NodeMap_t;

  virtual Error Open(const Path& path, int mode, MountNode** out_node);
  virtual Error Unlink(const Path& path);
  virtual Error Mkdir(const Path& path, int permissions);
  virtual Error Rmdir(const Path& path);
  virtual Error Remove(const Path& path);

  PP_Resource MakeUrlRequestInfo(const std::string& url,
                                 const char* method,
                                 StringMap_t* additional_headers);

 protected:
  MountHttp();

  virtual Error Init(int dev, StringMap_t& args, PepperInterface* ppapi);
  virtual void Destroy();
  Error FindOrCreateDir(const Path& path, MountNodeDir** out_node);
  Error LoadManifest(const std::string& path, char** out_manifest);
  Error ParseManifest(char *text);

 private:
  std::string url_root_;
  StringMap_t headers_;
  NodeMap_t node_cache_;
  bool allow_cors_;
  bool allow_credentials_;
  bool cache_stat_;
  bool cache_content_;

  friend class Mount;
  friend class MountNodeHttp;
  friend class MountHttpMock;
};

#endif  // LIBRARIES_NACL_IO_MOUNT_HTTP_H_
