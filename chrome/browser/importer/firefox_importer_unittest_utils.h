// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_IMPORTER_FIREFOX_IMPORTER_UNITTEST_UTILS_H_
#define CHROME_BROWSER_IMPORTER_FIREFOX_IMPORTER_UNITTEST_UTILS_H_

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "base/process_util.h"
#include "chrome/browser/importer/nss_decryptor.h"

class FFDecryptorServerChannelListener;

namespace base {
class MessageLoopForIO;
}

namespace IPC {
class Channel;
}  // namespace IPC

// On OS X NSSDecryptor needs to run in a separate process. To allow us to use
// the same unit test on all platforms we use a proxy class which spawns a
// child process to do decryption on OS X, and calls through directly
// to NSSDecryptor on other platforms.
// On OS X:
// 2 IPC messages are sent for every method of NSSDecryptor, one containing the
// input arguments sent from Server->Child and one coming back containing
// the return argument e.g.
//
// -> Msg_Decryptor_Init(dll_path, db_path)
// <- Msg_Decryptor_InitReturnCode(bool)
class FFUnitTestDecryptorProxy {
 public:
  FFUnitTestDecryptorProxy();
  virtual ~FFUnitTestDecryptorProxy();

  // Initialize a decryptor, returns true if the object was
  // constructed successfully.
  bool Setup(const base::FilePath& nss_path);

  // This match the parallel functions in NSSDecryptor.
  bool DecryptorInit(const base::FilePath& dll_path,
                     const base::FilePath& db_path);
  string16 Decrypt(const std::string& crypt);

 private:
#if defined(OS_MACOSX)
  // Blocks until either a timeout is reached, or until the client process
  // responds to an IPC message.
  // Returns true if a reply was received successfully and false if the
  // the operation timed out.
  bool WaitForClientResponse();

  base::ProcessHandle child_process_;
  scoped_ptr<IPC::Channel> channel_;
  scoped_ptr<FFDecryptorServerChannelListener> listener_;
  scoped_ptr<base::MessageLoopForIO> message_loop_;
#else
  NSSDecryptor decryptor_;
#endif  // !OS_MACOSX
  DISALLOW_COPY_AND_ASSIGN(FFUnitTestDecryptorProxy);
};

// On Non-OSX platforms FFUnitTestDecryptorProxy simply calls through to
// NSSDecryptor.
#if !defined(OS_MACOSX)
FFUnitTestDecryptorProxy::FFUnitTestDecryptorProxy() {
}

FFUnitTestDecryptorProxy::~FFUnitTestDecryptorProxy() {
}

bool FFUnitTestDecryptorProxy::Setup(const base::FilePath& nss_path) {
  return true;
}

bool FFUnitTestDecryptorProxy::DecryptorInit(const base::FilePath& dll_path,
                                             const base::FilePath& db_path) {
  return decryptor_.Init(dll_path, db_path);
}

string16 FFUnitTestDecryptorProxy::Decrypt(const std::string& crypt) {
  return decryptor_.Decrypt(crypt);
}
#endif  // !OS_MACOSX

#endif  // CHROME_BROWSER_IMPORTER_FIREFOX_IMPORTER_UNITTEST_UTILS_H_
