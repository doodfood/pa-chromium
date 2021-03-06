// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/media_galleries/media_galleries_dialog_controller.h"
#include "chrome/browser/media_galleries/media_galleries_preferences.h"
#include "chrome/browser/storage_monitor/storage_info.h"
#include "testing/gtest/include/gtest/gtest.h"

std::string GalleryName(const chrome::MediaGalleryPrefInfo& gallery) {
  using chrome::MediaGalleriesDialogController;
  string16 name =
      MediaGalleriesDialogController::GetGalleryDisplayNameNoAttachment(
          gallery);
  return UTF16ToASCII(name);
}

TEST(MediaGalleriesDialogControllerTest, TestNameGeneration) {
  chrome::MediaGalleryPrefInfo gallery;
  gallery.pref_id = 1;
  gallery.device_id = chrome::StorageInfo::MakeDeviceId(
      chrome::StorageInfo::FIXED_MASS_STORAGE, "/path/to/gallery");
  gallery.type = chrome::MediaGalleryPrefInfo::kAutoDetected;
  EXPECT_EQ("gallery", GalleryName(gallery));

  gallery.display_name = ASCIIToUTF16("override");
  EXPECT_EQ("override", GalleryName(gallery));

  gallery.display_name = string16();
  gallery.volume_label = ASCIIToUTF16("label");
  EXPECT_EQ("gallery", GalleryName(gallery));

  gallery.path = base::FilePath(FILE_PATH_LITERAL("sub/gallery2"));
  EXPECT_EQ("gallery2", GalleryName(gallery));

  gallery.path = base::FilePath();
  gallery.device_id = chrome::StorageInfo::MakeDeviceId(
      chrome::StorageInfo::REMOVABLE_MASS_STORAGE_WITH_DCIM,
      "/path/to/dcim");
  gallery.display_name = ASCIIToUTF16("override");
  EXPECT_EQ("override", GalleryName(gallery));

  gallery.volume_label = ASCIIToUTF16("volume");
  gallery.vendor_name = ASCIIToUTF16("vendor");
  gallery.model_name = ASCIIToUTF16("model");
  EXPECT_EQ("override", GalleryName(gallery));

  gallery.display_name = string16();
  EXPECT_EQ("volume", GalleryName(gallery));

  gallery.volume_label = string16();
  EXPECT_EQ("vendor, model", GalleryName(gallery));

  gallery.total_size_in_bytes = 1000000;
  EXPECT_EQ("977 KB vendor, model", GalleryName(gallery));

  gallery.path = base::FilePath(FILE_PATH_LITERAL("sub/path"));
  EXPECT_EQ("path - 977 KB vendor, model", GalleryName(gallery));
}
