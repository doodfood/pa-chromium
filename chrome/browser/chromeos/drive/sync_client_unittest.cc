// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/sync_client.h"

#include "base/file_util.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_ptr.h"
#include "base/run_loop.h"
#include "base/test/test_timeouts.h"
#include "chrome/browser/chromeos/drive/change_list_loader.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/file_cache.h"
#include "chrome/browser/chromeos/drive/file_system/operation_observer.h"
#include "chrome/browser/chromeos/drive/job_scheduler.h"
#include "chrome/browser/chromeos/drive/resource_entry_conversion.h"
#include "chrome/browser/chromeos/drive/resource_metadata.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "chrome/browser/google_apis/fake_drive_service.h"
#include "chrome/browser/google_apis/test_util.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace internal {

namespace {

// The content of files iniitally stored in the cache.
const char kLocalContent[] = "Hello!";

// The content of files stored in the service.
const char kRemoteContent[] = "World!";

// SyncClientTestDriveService will return GDATA_CANCELLED when a request is
// made with the specified resource ID.
class SyncClientTestDriveService : public google_apis::FakeDriveService {
 public:
  // FakeDriveService override:
  virtual google_apis::CancelCallback GetResourceEntry(
      const std::string& resource_id,
      const google_apis::GetResourceEntryCallback& callback) OVERRIDE {
    if (resource_id == resource_id_to_be_cancelled_) {
      scoped_ptr<google_apis::ResourceEntry> null;
      base::MessageLoopProxy::current()->PostTask(
          FROM_HERE,
          base::Bind(callback,
                     google_apis::GDATA_CANCELLED,
                     base::Passed(&null)));
      return google_apis::CancelCallback();
    }
    return FakeDriveService::GetResourceEntry(resource_id, callback);
  }

  void set_resource_id_to_be_cancelled(const std::string& resource_id) {
    resource_id_to_be_cancelled_ = resource_id;
  }

 private:
  std::string resource_id_to_be_cancelled_;
};

class DummyOperationObserver : public file_system::OperationObserver {
  // OperationObserver override:
  virtual void OnDirectoryChangedByOperation(
      const base::FilePath& path) OVERRIDE {}
  virtual void OnCacheFileUploadNeededByOperation(
      const std::string& resource_id) OVERRIDE {}
};

}  // namespace

class SyncClientTest : public testing::Test {
 public:
  virtual void SetUp() OVERRIDE {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    profile_.reset(new TestingProfile);

    drive_service_.reset(new SyncClientTestDriveService);
    drive_service_->LoadResourceListForWapi("chromeos/gdata/empty_feed.json");
    drive_service_->LoadAccountMetadataForWapi(
        "chromeos/gdata/account_metadata.json");

    scheduler_.reset(new JobScheduler(profile_.get(), drive_service_.get()));
    metadata_.reset(new internal::ResourceMetadata(
        temp_dir_.path(), base::MessageLoopProxy::current()));
    ASSERT_EQ(FILE_ERROR_OK, metadata_->Initialize());

    cache_.reset(new FileCache(temp_dir_.path(),
                               base::MessageLoopProxy::current(),
                               NULL /* free_disk_space_getter */));
    ASSERT_TRUE(cache_->Initialize());

    ASSERT_NO_FATAL_FAILURE(SetUpTestData());

    sync_client_.reset(new SyncClient(base::MessageLoopProxy::current(),
                                      &observer_,
                                      scheduler_.get(),
                                      metadata_.get(),
                                      cache_.get()));

    // Disable delaying so that DoSyncLoop() starts immediately.
    sync_client_->set_delay_for_testing(base::TimeDelta::FromSeconds(0));
  }

  virtual void TearDown() OVERRIDE {
    sync_client_.reset();
    cache_.reset();
    metadata_.reset();
  }

  // Adds a file to the service root and |resource_ids_|.
  void AddFileEntry(const std::string& title) {
    google_apis::GDataErrorCode error = google_apis::GDATA_FILE_ERROR;
    scoped_ptr<google_apis::ResourceEntry> entry;
    drive_service_->AddNewFile(
        "text/plain",
        kRemoteContent,
        drive_service_->GetRootResourceId(),
        title,
        false,  // shared_with_me
        google_apis::test_util::CreateCopyResultCallback(&error, &entry));
    base::RunLoop().RunUntilIdle();
    ASSERT_EQ(google_apis::HTTP_CREATED, error);
    ASSERT_TRUE(entry);
    resource_ids_[title] = entry->resource_id();
  }

  // Sets up data for tests.
  void SetUpTestData() {
    // Prepare a temp file.
    base::FilePath temp_file;
    EXPECT_TRUE(file_util::CreateTemporaryFileInDir(temp_dir_.path(),
                                                    &temp_file));
    ASSERT_TRUE(google_apis::test_util::WriteStringToFile(temp_file,
                                                          kLocalContent));

    // Prepare 3 pinned-but-not-present files.
    ASSERT_NO_FATAL_FAILURE(AddFileEntry("foo"));
    EXPECT_EQ(FILE_ERROR_OK, cache_->Pin(resource_ids_["foo"], std::string()));

    ASSERT_NO_FATAL_FAILURE(AddFileEntry("bar"));
    EXPECT_EQ(FILE_ERROR_OK, cache_->Pin(resource_ids_["bar"], std::string()));

    ASSERT_NO_FATAL_FAILURE(AddFileEntry("baz"));
    EXPECT_EQ(FILE_ERROR_OK, cache_->Pin(resource_ids_["baz"], std::string()));

    // Prepare a pinned-and-fetched file.
    const std::string md5_fetched = "md5";
    ASSERT_NO_FATAL_FAILURE(AddFileEntry("fetched"));
    EXPECT_EQ(FILE_ERROR_OK,
              cache_->Store(resource_ids_["fetched"], md5_fetched,
                            temp_file, FileCache::FILE_OPERATION_COPY));
    EXPECT_EQ(FILE_ERROR_OK,
              cache_->Pin(resource_ids_["fetched"], md5_fetched));

    // Prepare a pinned-and-fetched-and-dirty file.
    const std::string md5_dirty = "";  // Don't care.
    ASSERT_NO_FATAL_FAILURE(AddFileEntry("dirty"));
    EXPECT_EQ(FILE_ERROR_OK,
              cache_->Store(resource_ids_["dirty"], md5_dirty,
                            temp_file, FileCache::FILE_OPERATION_COPY));
    EXPECT_EQ(FILE_ERROR_OK, cache_->Pin(resource_ids_["dirty"], md5_dirty));
    EXPECT_EQ(FILE_ERROR_OK,
              cache_->MarkDirty(resource_ids_["dirty"], md5_dirty));

    // Load data from the service to the metadata.
    FileError error = FILE_ERROR_FAILED;
    internal::ChangeListLoader change_list_loader(
        base::MessageLoopProxy::current(), metadata_.get(), scheduler_.get());
    change_list_loader.LoadIfNeeded(
        DirectoryFetchInfo(),
        google_apis::test_util::CreateCopyResultCallback(&error));
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(FILE_ERROR_OK, error);
  }

 protected:
  content::TestBrowserThreadBundle thread_bundle_;
  base::ScopedTempDir temp_dir_;
  scoped_ptr<TestingProfile> profile_;
  scoped_ptr<SyncClientTestDriveService> drive_service_;
  DummyOperationObserver observer_;
  scoped_ptr<JobScheduler> scheduler_;
  scoped_ptr<internal::ResourceMetadata, test_util::DestroyHelperForTests>
      metadata_;
  scoped_ptr<FileCache, test_util::DestroyHelperForTests> cache_;
  scoped_ptr<SyncClient> sync_client_;

  std::map<std::string, std::string> resource_ids_;  // Name-to-id map.
};

TEST_F(SyncClientTest, StartProcessingBacklog) {
  sync_client_->StartProcessingBacklog();
  base::RunLoop().RunUntilIdle();

  FileCacheEntry cache_entry;
  // Pinned files get downloaded.
  EXPECT_TRUE(cache_->GetCacheEntry(resource_ids_["foo"], std::string(),
                                    &cache_entry));
  EXPECT_TRUE(cache_entry.is_present());

  EXPECT_TRUE(cache_->GetCacheEntry(resource_ids_["bar"], std::string(),
                                    &cache_entry));
  EXPECT_TRUE(cache_entry.is_present());

  EXPECT_TRUE(cache_->GetCacheEntry(resource_ids_["baz"], std::string(),
                                    &cache_entry));
  EXPECT_TRUE(cache_entry.is_present());

  // Dirty file gets uploaded.
  EXPECT_TRUE(cache_->GetCacheEntry(resource_ids_["dirty"], std::string(),
                                    &cache_entry));
  EXPECT_FALSE(cache_entry.is_dirty());
}

TEST_F(SyncClientTest, AddFetchTask) {
  sync_client_->AddFetchTask(resource_ids_["foo"]);
  base::RunLoop().RunUntilIdle();

  FileCacheEntry cache_entry;
  EXPECT_TRUE(cache_->GetCacheEntry(resource_ids_["foo"], std::string(),
                                    &cache_entry));
  EXPECT_TRUE(cache_entry.is_present());
}

TEST_F(SyncClientTest, AddFetchTaskAndCancelled) {
  // Trigger fetching of a file which results in cancellation.
  drive_service_->set_resource_id_to_be_cancelled(resource_ids_["foo"]);
  sync_client_->AddFetchTask(resource_ids_["foo"]);
  base::RunLoop().RunUntilIdle();

  // The file should be unpinned if the user wants the download to be cancelled.
  FileCacheEntry cache_entry;
  EXPECT_FALSE(cache_->GetCacheEntry(resource_ids_["foo"], std::string(),
                                     &cache_entry));
}

TEST_F(SyncClientTest, RemoveFetchTask) {
  sync_client_->AddFetchTask(resource_ids_["foo"]);
  sync_client_->AddFetchTask(resource_ids_["bar"]);
  sync_client_->AddFetchTask(resource_ids_["baz"]);

  sync_client_->RemoveFetchTask(resource_ids_["foo"]);
  sync_client_->RemoveFetchTask(resource_ids_["baz"]);
  base::RunLoop().RunUntilIdle();

  // Only "bar" should be fetched.
  FileCacheEntry cache_entry;
  EXPECT_TRUE(cache_->GetCacheEntry(resource_ids_["foo"], std::string(),
                                    &cache_entry));
  EXPECT_FALSE(cache_entry.is_present());

  EXPECT_TRUE(cache_->GetCacheEntry(resource_ids_["bar"], std::string(),
                                    &cache_entry));
  EXPECT_TRUE(cache_entry.is_present());

  EXPECT_TRUE(cache_->GetCacheEntry(resource_ids_["baz"], std::string(),
                                    &cache_entry));
  EXPECT_FALSE(cache_entry.is_present());

}

TEST_F(SyncClientTest, ExistingPinnedFiles) {
  // Start checking the existing pinned files. This will collect the resource
  // IDs of pinned files, with stale local cache files.
  sync_client_->StartCheckingExistingPinnedFiles();
  base::RunLoop().RunUntilIdle();

  // "fetched" and "dirty" are the existing pinned files.
  // The non-dirty one should be synced, but the dirty one should not.
  base::FilePath cache_file;
  std::string content;
  EXPECT_EQ(FILE_ERROR_OK, cache_->GetFile(resource_ids_["fetched"],
                                           std::string(), &cache_file));
  EXPECT_TRUE(file_util::ReadFileToString(cache_file, &content));
  EXPECT_EQ(kRemoteContent, content);
  content.clear();

  EXPECT_EQ(FILE_ERROR_OK, cache_->GetFile(resource_ids_["dirty"],
                                           std::string(), &cache_file));
  EXPECT_TRUE(file_util::ReadFileToString(cache_file, &content));
  EXPECT_EQ(kLocalContent, content);
}

}  // namespace internal
}  // namespace drive
