// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_ANDROID_MEDIA_DRM_BRIDGE_H_
#define MEDIA_BASE_ANDROID_MEDIA_DRM_BRIDGE_H_

#include <jni.h>
#include <string>
#include <vector>

#include "base/android/scoped_java_ref.h"
#include "media/base/media_export.h"
#include "media/base/media_keys.h"

namespace media {

// This class provides DRM services for android EME implementation.
// TODO(qinmin): implement all the functions in this class.
class MEDIA_EXPORT MediaDrmBridge : public MediaKeys {
 public:
  MediaDrmBridge(int media_keys_id, const std::vector<uint8>& uuid);
  virtual ~MediaDrmBridge();

  // Checks whether DRM is available.
  static bool IsAvailable();

  // MediaKeys implementations.
  virtual bool GenerateKeyRequest(const std::string& key_system,
                                  const std::string& type,
                                  const uint8* init_data,
                                  int init_data_length) OVERRIDE;
  virtual void AddKey(const std::string& key_system,
                      const uint8* key, int key_length,
                      const uint8* init_data, int init_data_length,
                      const std::string& session_id) OVERRIDE;
  virtual void CancelKeyRequest(const std::string& key_system,
                                const std::string& session_id) OVERRIDE;

  // Drm related message was received.
  void OnDrmEvent(JNIEnv* env, jobject, jstring session_id,
                  jint event, jint extra, jstring data);

  // Called after we got the response for GenerateKeyRequest().
  void OnKeyMessage(JNIEnv* env, jobject, jstring session_id,
                    jbyteArray message, jstring destination_url);

  // Methods to create and release a MediaCrypto object.
  base::android::ScopedJavaLocalRef<jobject> CreateMediaCrypto(
      const std::string& session_id);
  void ReleaseMediaCrypto(const std::string& session_id);

  int media_keys_id() const { return media_keys_id_; }

 private:
  // Id of the MediaKeys object.
  int media_keys_id_;

  DISALLOW_COPY_AND_ASSIGN(MediaDrmBridge);
};

}  // namespace media

#endif  // MEDIA_BASE_ANDROID_MEDIA_DRM_BRIDGE_H_
