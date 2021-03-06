// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/file_cache.h"

#include <string>
#include <vector>

#include "base/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/fake_free_disk_space_getter.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/test_util.h"
#include "chrome/browser/google_apis/test_util.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace drive {
namespace internal {
namespace {

// Copies results from Iterate().
void OnIterate(std::vector<std::string>* out_resource_ids,
               std::vector<FileCacheEntry>* out_cache_entries,
               const std::string& resource_id,
               const FileCacheEntry& cache_entry) {
  out_resource_ids->push_back(resource_id);
  out_cache_entries->push_back(cache_entry);
}

// Called upon completion of Iterate().
void OnIterateCompleted(bool* out_is_called) {
  *out_is_called = true;
}

}  // namespace

// Tests FileCache methods from UI thread. It internally uses a real blocking
// pool and tests the interaction among threads.
// TODO(hashimoto): remove this class. crbug.com/231221.
class FileCacheTestOnUIThread : public testing::Test {
 protected:
  FileCacheTestOnUIThread() : expected_error_(FILE_ERROR_OK),
                              expected_cache_state_(0) {
  }

  virtual void SetUp() OVERRIDE {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(file_util::CreateTemporaryFileInDir(temp_dir_.path(),
                                                    &dummy_file_path_));
    fake_free_disk_space_getter_.reset(new FakeFreeDiskSpaceGetter);

    scoped_refptr<base::SequencedWorkerPool> pool =
        content::BrowserThread::GetBlockingPool();
    blocking_task_runner_ =
        pool->GetSequencedTaskRunner(pool->GetSequenceToken());
    cache_.reset(new FileCache(temp_dir_.path(),
                               blocking_task_runner_.get(),
                               fake_free_disk_space_getter_.get()));

    bool success = false;
    base::PostTaskAndReplyWithResult(
        blocking_task_runner_,
        FROM_HERE,
        base::Bind(&FileCache::Initialize,
                   base::Unretained(cache_.get())),
        google_apis::test_util::CreateCopyResultCallback(&success));
    google_apis::test_util::RunBlockingPoolTask();
    ASSERT_TRUE(success);
  }

  virtual void TearDown() OVERRIDE {
    cache_.reset();
  }

  void TestGetFileFromCacheByResourceIdAndMd5(
      const std::string& resource_id,
      const std::string& md5,
      FileError expected_error,
      const std::string& expected_file_extension) {
    FileError error = FILE_ERROR_OK;
    base::FilePath cache_file_path;
    cache_->GetFileOnUIThread(resource_id, md5,
                              google_apis::test_util::CreateCopyResultCallback(
                                  &error, &cache_file_path));
    google_apis::test_util::RunBlockingPoolTask();

    EXPECT_EQ(expected_error, error);
    if (error == FILE_ERROR_OK) {
      // Verify filename of |cache_file_path|.
      base::FilePath base_name = cache_file_path.BaseName();
      EXPECT_EQ(util::EscapeCacheFileName(resource_id) +
                base::FilePath::kExtensionSeparator +
                util::EscapeCacheFileName(
                    expected_file_extension.empty() ?
                    md5 : expected_file_extension),
                base_name.value());
    } else {
      EXPECT_TRUE(cache_file_path.empty());
    }
  }

  void TestStoreToCache(const std::string& resource_id,
                        const std::string& md5,
                        const base::FilePath& source_path,
                        FileError expected_error,
                        int expected_cache_state) {
    expected_error_ = expected_error;
    expected_cache_state_ = expected_cache_state;

    FileError error = FILE_ERROR_OK;
    cache_->StoreOnUIThread(
        resource_id, md5, source_path,
        FileCache::FILE_OPERATION_COPY,
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();
    VerifyCacheFileState(error, resource_id, md5);
  }

  void TestRemoveFromCache(const std::string& resource_id,
                           FileError expected_error) {
    expected_error_ = expected_error;

    FileError error = FILE_ERROR_OK;
    cache_->RemoveOnUIThread(
        resource_id,
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();
    VerifyRemoveFromCache(error, resource_id, "");
  }

  // Returns number of files matching to |path_pattern|.
  int CountFilesWithPathPattern(const base::FilePath& path_pattern) {
    int result = 0;
    base::FileEnumerator enumerator(
        path_pattern.DirName(), false /* not recursive*/,
        base::FileEnumerator::FILES,
        path_pattern.BaseName().value());
    for (base::FilePath current = enumerator.Next(); !current.empty();
         current = enumerator.Next())
      ++result;
    return result;
  }

  void VerifyRemoveFromCache(FileError error,
                             const std::string& resource_id,
                             const std::string& md5) {
    EXPECT_EQ(expected_error_, error);

    FileCacheEntry cache_entry;
    if (!GetCacheEntryFromOriginThread(resource_id, md5, &cache_entry)) {
      EXPECT_EQ(FILE_ERROR_OK, error);

      // Verify that no files with "<resource_id>.*" exist.
      const base::FilePath path_pattern = cache_->GetCacheFilePath(
          resource_id, util::kWildCard, FileCache::CACHED_FILE_FROM_SERVER);
      EXPECT_EQ(0, CountFilesWithPathPattern(path_pattern));
    }
  }

  void TestPin(const std::string& resource_id,
               const std::string& md5,
               FileError expected_error,
               int expected_cache_state) {
    expected_error_ = expected_error;
    expected_cache_state_ = expected_cache_state;

    FileError error = FILE_ERROR_OK;
    cache_->PinOnUIThread(
        resource_id, md5,
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();
    VerifyCacheFileState(error, resource_id, md5);
  }

  void TestUnpin(const std::string& resource_id,
                 const std::string& md5,
                 FileError expected_error,
                 int expected_cache_state) {
    expected_error_ = expected_error;
    expected_cache_state_ = expected_cache_state;

    FileError error = FILE_ERROR_OK;
    cache_->UnpinOnUIThread(
        resource_id, md5,
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();
    VerifyCacheFileState(error, resource_id, md5);
  }

  void TestMarkDirty(const std::string& resource_id,
                     const std::string& md5,
                     FileError expected_error,
                     int expected_cache_state) {
    expected_error_ = expected_error;
    expected_cache_state_ = expected_cache_state;

    FileError error = FILE_ERROR_OK;
    cache_->MarkDirtyOnUIThread(
        resource_id, md5,
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();

    VerifyCacheFileState(error, resource_id, md5);

    // Verify filename.
    if (error == FILE_ERROR_OK) {
      base::FilePath cache_file_path;
      cache_->GetFileOnUIThread(
          resource_id, md5,
          google_apis::test_util::CreateCopyResultCallback(
              &error, &cache_file_path));
      google_apis::test_util::RunBlockingPoolTask();

      EXPECT_EQ(FILE_ERROR_OK, error);
      base::FilePath base_name = cache_file_path.BaseName();
      EXPECT_EQ(util::EscapeCacheFileName(resource_id) +
                base::FilePath::kExtensionSeparator +
                "local",
                base_name.value());
    }
  }

  void TestClearDirty(const std::string& resource_id,
                      const std::string& md5,
                      FileError expected_error,
                      int expected_cache_state) {
    expected_error_ = expected_error;
    expected_cache_state_ = expected_cache_state;

    FileError error = FILE_ERROR_OK;
    PostTaskAndReplyWithResult(
        blocking_task_runner_.get(),
        FROM_HERE,
        base::Bind(&FileCache::ClearDirty,
                   base::Unretained(cache_.get()),
                   resource_id,
                   md5),
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();
    VerifyCacheFileState(error, resource_id, md5);
  }

  void TestMarkAsMounted(const std::string& resource_id,
                         FileError expected_error,
                         int expected_cache_state) {
    expected_error_ = expected_error;
    expected_cache_state_ = expected_cache_state;

    FileCacheEntry entry;
    EXPECT_TRUE(GetCacheEntryFromOriginThread(resource_id, std::string(),
                                              &entry));

    FileError error = FILE_ERROR_OK;
    base::FilePath cache_file_path;
    cache_->MarkAsMountedOnUIThread(
        resource_id,
        google_apis::test_util::CreateCopyResultCallback(
            &error, &cache_file_path));
    google_apis::test_util::RunBlockingPoolTask();

    EXPECT_TRUE(file_util::PathExists(cache_file_path));
    EXPECT_EQ(cache_file_path,
              cache_->GetCacheFilePath(resource_id, entry.md5(),
                                       FileCache::CACHED_FILE_FROM_SERVER));
  }

  void TestMarkAsUnmounted(const std::string& resource_id,
                           const std::string& md5,
                           const base::FilePath& file_path,
                           FileError expected_error,
                           int expected_cache_state) {
    expected_error_ = expected_error;
    expected_cache_state_ = expected_cache_state;

    FileError error = FILE_ERROR_OK;
    cache_->MarkAsUnmountedOnUIThread(
        file_path,
        google_apis::test_util::CreateCopyResultCallback(&error));
    google_apis::test_util::RunBlockingPoolTask();

    base::FilePath cache_file_path;
    cache_->GetFileOnUIThread(
        resource_id, md5,
        google_apis::test_util::CreateCopyResultCallback(
            &error, &cache_file_path));
    google_apis::test_util::RunBlockingPoolTask();
    EXPECT_EQ(FILE_ERROR_OK, error);

    EXPECT_TRUE(file_util::PathExists(cache_file_path));
    EXPECT_EQ(cache_file_path,
              cache_->GetCacheFilePath(resource_id, md5,
                                       FileCache::CACHED_FILE_FROM_SERVER));
  }

  void VerifyCacheFileState(FileError error,
                            const std::string& resource_id,
                            const std::string& md5) {
    EXPECT_EQ(expected_error_, error);

    // Verify cache map.
    FileCacheEntry cache_entry;
    const bool cache_entry_found =
        GetCacheEntryFromOriginThread(resource_id, md5, &cache_entry);
    if (test_util::ToCacheEntry(expected_cache_state_).is_present() ||
        test_util::ToCacheEntry(expected_cache_state_).is_pinned()) {
      ASSERT_TRUE(cache_entry_found);
      EXPECT_TRUE(test_util::CacheStatesEqual(
          test_util::ToCacheEntry(expected_cache_state_), cache_entry));
    } else {
      EXPECT_FALSE(cache_entry_found);
    }

    // Verify actual cache file.
    base::FilePath dest_path = cache_->GetCacheFilePath(
        resource_id,
        md5,
        test_util::ToCacheEntry(expected_cache_state_).is_dirty() ?
            FileCache::CACHED_FILE_LOCALLY_MODIFIED :
        FileCache::CACHED_FILE_FROM_SERVER);
    bool exists = file_util::PathExists(dest_path);
    if (test_util::ToCacheEntry(expected_cache_state_).is_present())
      EXPECT_TRUE(exists);
    else
      EXPECT_FALSE(exists);
  }

  base::FilePath GetCacheFilePath(const std::string& resource_id,
                                  const std::string& md5,
                                  FileCache::CachedFileOrigin file_origin) {
    return cache_->GetCacheFilePath(resource_id, md5, file_origin);
  }

  // Helper function to call GetCacheEntry from origin thread.
  bool GetCacheEntryFromOriginThread(const std::string& resource_id,
                                     const std::string& md5,
                                     FileCacheEntry* cache_entry) {
    bool result = false;
    cache_->GetCacheEntryOnUIThread(
        resource_id, md5,
        google_apis::test_util::CreateCopyResultCallback(&result, cache_entry));
    google_apis::test_util::RunBlockingPoolTask();
    return result;
  }

  // Returns true if the cache entry exists for the given resource ID and MD5.
  bool CacheEntryExists(const std::string& resource_id,
                        const std::string& md5) {
    FileCacheEntry cache_entry;
    return GetCacheEntryFromOriginThread(resource_id, md5, &cache_entry);
  }

  void TestGetCacheFilePath(const std::string& resource_id,
                            const std::string& md5,
                            const std::string& expected_filename) {
    base::FilePath actual_path = cache_->GetCacheFilePath(
        resource_id, md5, FileCache::CACHED_FILE_FROM_SERVER);
    base::FilePath expected_path =
        cache_->GetCacheDirectoryPath(FileCache::CACHE_TYPE_FILES);
    expected_path = expected_path.Append(
        base::FilePath::FromUTF8Unsafe(expected_filename));
    EXPECT_EQ(expected_path, actual_path);

    base::FilePath base_name = actual_path.BaseName();

    // base::FilePath::Extension returns ".", so strip it.
    std::string unescaped_md5 = util::UnescapeCacheFileName(
        base_name.Extension().substr(1));
    EXPECT_EQ(md5, unescaped_md5);
    std::string unescaped_resource_id = util::UnescapeCacheFileName(
        base_name.RemoveExtension().value());
    EXPECT_EQ(resource_id, unescaped_resource_id);
  }

  // Returns the number of the cache files with name <resource_id>, and Confirm
  // that they have the <md5>. This should return 1 or 0.
  size_t CountCacheFiles(const std::string& resource_id,
                         const std::string& md5) {
    base::FilePath path = GetCacheFilePath(
        resource_id, util::kWildCard, FileCache::CACHED_FILE_FROM_SERVER);
    base::FileEnumerator enumerator(path.DirName(), false,
                                    base::FileEnumerator::FILES,
                                    path.BaseName().value());
    size_t num_files_found = 0;
    for (base::FilePath current = enumerator.Next(); !current.empty();
         current = enumerator.Next()) {
      ++num_files_found;
      EXPECT_EQ(util::EscapeCacheFileName(resource_id) +
                base::FilePath::kExtensionSeparator +
                util::EscapeCacheFileName(md5),
                current.BaseName().value());
    }
    return num_files_found;
  }

  content::TestBrowserThreadBundle thread_bundle_;
  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;
  base::ScopedTempDir temp_dir_;
  base::FilePath dummy_file_path_;

  scoped_ptr<FileCache, test_util::DestroyHelperForTests> cache_;
  scoped_ptr<FakeFreeDiskSpaceGetter> fake_free_disk_space_getter_;

  FileError expected_error_;
  int expected_cache_state_;
  std::string expected_file_extension_;
};

TEST_F(FileCacheTestOnUIThread, GetCacheFilePath) {
  // Use alphanumeric characters for resource id.
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");
  TestGetCacheFilePath(resource_id, md5,
                       resource_id + base::FilePath::kExtensionSeparator + md5);

  // Use non-alphanumeric characters for resource id, including '.' which is an
  // extension separator, to test that the characters are escaped and unescaped
  // correctly, and '.' doesn't mess up the filename format and operations.
  resource_id = "pdf:`~!@#$%^&*()-_=+[{|]}\\;',<.>/?";
  std::string escaped_resource_id = util::EscapeCacheFileName(resource_id);
  std::string escaped_md5 = util::EscapeCacheFileName(md5);
  TestGetCacheFilePath(resource_id, md5, escaped_resource_id +
                       base::FilePath::kExtensionSeparator + escaped_md5);
}

TEST_F(FileCacheTestOnUIThread, StoreToCacheSimple) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // Store an existing file.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  // Store a non-existent file to the same |resource_id| and |md5|.
  TestStoreToCache(resource_id, md5,
                   base::FilePath::FromUTF8Unsafe("non_existent_file"),
                   FILE_ERROR_FAILED,
                   test_util::TEST_CACHE_STATE_PRESENT);

  // Store a different existing file to the same |resource_id| but different
  // |md5|.
  md5 = "new_md5";
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  // Verify that there's only one file with name <resource_id>, i.e. previously
  // cached file with the different md5 should be deleted.
  EXPECT_EQ(1U, CountCacheFiles(resource_id, md5));
}


TEST_F(FileCacheTestOnUIThread, GetFromCacheSimple) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");
  // First store a file to cache.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  // Then try to get the existing file from cache.
  TestGetFileFromCacheByResourceIdAndMd5(
      resource_id, md5, FILE_ERROR_OK, md5);

  // Get file from cache with same resource id as existing file but different
  // md5.
  TestGetFileFromCacheByResourceIdAndMd5(
      resource_id, "9999", FILE_ERROR_NOT_FOUND, md5);

  // Get file from cache with different resource id from existing file but same
  // md5.
  resource_id = "document:1a2b";
  TestGetFileFromCacheByResourceIdAndMd5(
      resource_id, md5, FILE_ERROR_NOT_FOUND, md5);
}

TEST_F(FileCacheTestOnUIThread, RemoveFromCacheSimple) {
  // Use alphanumeric characters for resource id.
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");
  // First store a file to cache.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  // Then try to remove existing file from cache.
  TestRemoveFromCache(resource_id, FILE_ERROR_OK);

  // Repeat using non-alphanumeric characters for resource id, including '.'
  // which is an extension separator.
  resource_id = "pdf:`~!@#$%^&*()-_=+[{|]}\\;',<.>/?";
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  TestRemoveFromCache(resource_id, FILE_ERROR_OK);
}

TEST_F(FileCacheTestOnUIThread, PinAndUnpin) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // First store a file to cache.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  // Pin the existing file in cache.
  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PRESENT |
          test_util::TEST_CACHE_STATE_PINNED);

  // Unpin the existing file in cache.
  TestUnpin(resource_id, md5, FILE_ERROR_OK,
            test_util::TEST_CACHE_STATE_PRESENT);

  // Pin back the same existing file in cache.
  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PRESENT |
          test_util::TEST_CACHE_STATE_PINNED);

  // Pin a non-existent file in cache.
  resource_id = "document:1a2b";

  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PINNED);

  // Unpin the previously pinned non-existent file in cache.
  TestUnpin(resource_id, md5, FILE_ERROR_OK,
            test_util::TEST_CACHE_STATE_NONE);

  // Unpin a file that doesn't exist in cache and is not pinned, i.e. cache
  // has zero knowledge of the file.
  resource_id = "not-in-cache:1a2b";

  TestUnpin(resource_id, md5, FILE_ERROR_NOT_FOUND,
            test_util::TEST_CACHE_STATE_NONE);
}

TEST_F(FileCacheTestOnUIThread, StoreToCachePinned) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // Pin a non-existent file.
  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PINNED);

  // Store an existing file to a previously pinned file.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK,
                   test_util::TEST_CACHE_STATE_PRESENT |
                   test_util::TEST_CACHE_STATE_PINNED);

  // Store a non-existent file to a previously pinned and stored file.
  TestStoreToCache(resource_id, md5,
                   base::FilePath::FromUTF8Unsafe("non_existent_file"),
                   FILE_ERROR_FAILED,
                   test_util::TEST_CACHE_STATE_PRESENT |
                   test_util::TEST_CACHE_STATE_PINNED);
}

TEST_F(FileCacheTestOnUIThread, GetFromCachePinned) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // Pin a non-existent file.
  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PINNED);

  // Get the non-existent pinned file from cache.
  TestGetFileFromCacheByResourceIdAndMd5(
      resource_id, md5, FILE_ERROR_NOT_FOUND, md5);

  // Store an existing file to the previously pinned non-existent file.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK,
                   test_util::TEST_CACHE_STATE_PRESENT |
                   test_util::TEST_CACHE_STATE_PINNED);

  // Get the previously pinned and stored file from cache.
  TestGetFileFromCacheByResourceIdAndMd5(
      resource_id, md5, FILE_ERROR_OK, md5);
}

TEST_F(FileCacheTestOnUIThread, RemoveFromCachePinned) {
  // Use alphanumeric characters for resource_id.
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // Store a file to cache, and pin it.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);
  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PRESENT |
          test_util::TEST_CACHE_STATE_PINNED);

  // Remove |resource_id| from cache.
  TestRemoveFromCache(resource_id, FILE_ERROR_OK);

  // Repeat using non-alphanumeric characters for resource id, including '.'
  // which is an extension separator.
  resource_id = "pdf:`~!@#$%^&*()-_=+[{|]}\\;',<.>/?";

  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);
  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PRESENT |
          test_util::TEST_CACHE_STATE_PINNED);

  TestRemoveFromCache(resource_id, FILE_ERROR_OK);
}

TEST_F(FileCacheTestOnUIThread, DirtyCacheSimple) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // First store a file to cache.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  // Mark the file dirty.
  TestMarkDirty(resource_id, md5, FILE_ERROR_OK,
                test_util::TEST_CACHE_STATE_PRESENT |
                test_util::TEST_CACHE_STATE_DIRTY);

  // Clear dirty state of the file.
  TestClearDirty(resource_id, md5, FILE_ERROR_OK,
                 test_util::TEST_CACHE_STATE_PRESENT);
}

TEST_F(FileCacheTestOnUIThread, DirtyCachePinned) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // First store a file to cache and pin it.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);
  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PRESENT |
          test_util::TEST_CACHE_STATE_PINNED);

  // Mark the file dirty.
  TestMarkDirty(resource_id, md5, FILE_ERROR_OK,
                test_util::TEST_CACHE_STATE_PRESENT |
                test_util::TEST_CACHE_STATE_DIRTY |
                test_util::TEST_CACHE_STATE_PINNED);

  // Clear dirty state of the file.
  TestClearDirty(resource_id, md5, FILE_ERROR_OK,
                 test_util::TEST_CACHE_STATE_PRESENT |
                 test_util::TEST_CACHE_STATE_PINNED);
}

TEST_F(FileCacheTestOnUIThread, PinAndUnpinDirtyCache) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // First store a file to cache and mark it as dirty.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);
  TestMarkDirty(resource_id, md5, FILE_ERROR_OK,
                test_util::TEST_CACHE_STATE_PRESENT |
                test_util::TEST_CACHE_STATE_DIRTY);

  // Verifies dirty file exists.
  base::FilePath dirty_path;
  FileError error = FILE_ERROR_FAILED;
  cache_->GetFileOnUIThread(
      resource_id, md5,
      google_apis::test_util::CreateCopyResultCallback(&error, &dirty_path));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);
  EXPECT_TRUE(file_util::PathExists(dirty_path));

  // Pin the dirty file.
  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PRESENT |
          test_util::TEST_CACHE_STATE_DIRTY |
          test_util::TEST_CACHE_STATE_PINNED);

  // Verify dirty file still exist at the same pathname.
  EXPECT_TRUE(file_util::PathExists(dirty_path));

  // Unpin the dirty file.
  TestUnpin(resource_id, md5, FILE_ERROR_OK,
            test_util::TEST_CACHE_STATE_PRESENT |
            test_util::TEST_CACHE_STATE_DIRTY);

  // Verify dirty file still exist at the same pathname.
  EXPECT_TRUE(file_util::PathExists(dirty_path));
}

TEST_F(FileCacheTestOnUIThread, DirtyCacheRepetitive) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // First store a file to cache.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  // Mark the file dirty.
  TestMarkDirty(resource_id, md5, FILE_ERROR_OK,
                test_util::TEST_CACHE_STATE_PRESENT |
                test_util::TEST_CACHE_STATE_DIRTY);

  // Again, mark the file dirty.  Nothing should change.
  TestMarkDirty(resource_id, md5, FILE_ERROR_OK,
                test_util::TEST_CACHE_STATE_PRESENT |
                test_util::TEST_CACHE_STATE_DIRTY);

  // Clear dirty state of the file.
  TestClearDirty(resource_id, md5, FILE_ERROR_OK,
                 test_util::TEST_CACHE_STATE_PRESENT);

  // Again, clear dirty state of the file, which is no longer dirty.
  TestClearDirty(resource_id, md5, FILE_ERROR_INVALID_OPERATION,
                 test_util::TEST_CACHE_STATE_PRESENT);
}

TEST_F(FileCacheTestOnUIThread, DirtyCacheInvalid) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // Mark a non-existent file dirty.
  TestMarkDirty(resource_id, md5, FILE_ERROR_NOT_FOUND,
                test_util::TEST_CACHE_STATE_NONE);

  // Clear dirty state of a non-existent file.
  TestClearDirty(resource_id, md5, FILE_ERROR_NOT_FOUND,
                 test_util::TEST_CACHE_STATE_NONE);

  // Store a file to cache.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  // Clear dirty state of a non-dirty existing file.
  TestClearDirty(resource_id, md5, FILE_ERROR_INVALID_OPERATION,
                 test_util::TEST_CACHE_STATE_PRESENT);

  // Mark an existing file dirty, then store a new file to the same resource id
  // but different md5, which should fail.
  TestMarkDirty(resource_id, md5, FILE_ERROR_OK,
                test_util::TEST_CACHE_STATE_PRESENT |
                test_util::TEST_CACHE_STATE_DIRTY);
  md5 = "new_md5";
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_IN_USE,
                   test_util::TEST_CACHE_STATE_PRESENT |
                   test_util::TEST_CACHE_STATE_DIRTY);
}

TEST_F(FileCacheTestOnUIThread, RemoveFromDirtyCache) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // Store a file to cache, pin it, mark it dirty and commit it.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);
  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PRESENT |
          test_util::TEST_CACHE_STATE_PINNED);
  TestMarkDirty(resource_id, md5, FILE_ERROR_OK,
                test_util::TEST_CACHE_STATE_PRESENT |
                test_util::TEST_CACHE_STATE_PINNED |
                test_util::TEST_CACHE_STATE_DIRTY);

  // Try to remove the file.  Since file is dirty, it should not be removed.
  TestRemoveFromCache(resource_id, FILE_ERROR_IN_USE);
}

TEST_F(FileCacheTestOnUIThread, MountUnmount) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // First store a file to cache.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  // Mark the file mounted.
  TestMarkAsMounted(resource_id, FILE_ERROR_OK,
                    test_util::TEST_CACHE_STATE_PRESENT);
  EXPECT_TRUE(CacheEntryExists(resource_id, md5));

  // Try to remove the file.
  TestRemoveFromCache(resource_id, FILE_ERROR_IN_USE);

  // Clear mounted state of the file.
  base::FilePath file_path;
  FileError error = FILE_ERROR_FAILED;
  cache_->GetFileOnUIThread(
      resource_id, md5,
      google_apis::test_util::CreateCopyResultCallback(&error, &file_path));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_EQ(FILE_ERROR_OK, error);

  TestMarkAsUnmounted(resource_id, md5, file_path,
                      FILE_ERROR_OK,
                      test_util::TEST_CACHE_STATE_PRESENT);
  EXPECT_TRUE(CacheEntryExists(resource_id, md5));

  // Try to remove the file.
  TestRemoveFromCache(resource_id, FILE_ERROR_OK);
}

TEST_F(FileCacheTestOnUIThread, Iterate) {
  const std::vector<test_util::TestCacheResource> cache_resources(
      test_util::GetDefaultTestCacheResources());
  ASSERT_TRUE(test_util::PrepareTestCacheResources(cache_.get(),
                                                   cache_resources));

  std::vector<std::string> resource_ids;
  std::vector<FileCacheEntry> cache_entries;
  bool completed = false;
  cache_->IterateOnUIThread(
      base::Bind(&OnIterate, &resource_ids, &cache_entries),
      base::Bind(&OnIterateCompleted, &completed));
  google_apis::test_util::RunBlockingPoolTask();

  ASSERT_TRUE(completed);

  sort(resource_ids.begin(), resource_ids.end());
  ASSERT_EQ(6U, resource_ids.size());
  EXPECT_EQ("dirty:existing", resource_ids[0]);
  EXPECT_EQ("dirty_and_pinned:existing", resource_ids[1]);
  EXPECT_EQ("pinned:existing", resource_ids[2]);
  EXPECT_EQ("pinned:non-existent", resource_ids[3]);
  EXPECT_EQ("tmp:`~!@#$%^&*()-_=+[{|]}\\;',<.>/?", resource_ids[4]);
  EXPECT_EQ("tmp:resource_id", resource_ids[5]);

  ASSERT_EQ(6U, cache_entries.size());
}

TEST_F(FileCacheTestOnUIThread, ClearAll) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // Store an existing file.
  TestStoreToCache(resource_id, md5, dummy_file_path_,
                   FILE_ERROR_OK, test_util::TEST_CACHE_STATE_PRESENT);

  // Verify that there's only one cached file.
  EXPECT_EQ(1U, CountCacheFiles(resource_id, md5));

  // Clear cache.
  bool success = false;
  cache_->ClearAllOnUIThread(
      google_apis::test_util::CreateCopyResultCallback(&success));
  google_apis::test_util::RunBlockingPoolTask();
  EXPECT_TRUE(success);

  // Verify that all the cache is removed.
  expected_error_ = FILE_ERROR_OK;
  VerifyRemoveFromCache(FILE_ERROR_OK, resource_id, md5);
  EXPECT_EQ(0U, CountCacheFiles(resource_id, md5));
}

TEST_F(FileCacheTestOnUIThread, StoreToCacheNoSpace) {
  fake_free_disk_space_getter_->set_default_value(0);

  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");

  // Try to store an existing file.
  TestStoreToCache(resource_id, md5, dummy_file_path_, FILE_ERROR_NO_SPACE,
                   test_util::TEST_CACHE_STATE_NONE);

  // Verify that there's no files added.
  EXPECT_EQ(0U, CountCacheFiles(resource_id, md5));
}

// Don't use TEST_F, as we don't want SetUp() and TearDown() for this test.
TEST(FileCacheExtraTest, InitializationFailure) {
  content::TestBrowserThreadBundle thread_bundle;

  // Set the cache root to a non existent path, so the initialization fails.
  scoped_ptr<FileCache, test_util::DestroyHelperForTests> cache(new FileCache(
      base::FilePath::FromUTF8Unsafe("/somewhere/nonexistent/blah/blah"),
      base::MessageLoopProxy::current(),
      NULL /* free_disk_space_getter */));

  EXPECT_FALSE(cache->Initialize());
}

TEST_F(FileCacheTestOnUIThread, UpdatePinnedCache) {
  std::string resource_id("pdf:1a2b");
  std::string md5("abcdef0123456789");
  std::string md5_modified("aaaaaa0000000000");

  // Store an existing file.
  TestStoreToCache(resource_id, md5, dummy_file_path_, FILE_ERROR_OK,
                   test_util::TEST_CACHE_STATE_PRESENT);

  // Pin the file.
  TestPin(resource_id, md5, FILE_ERROR_OK,
          test_util::TEST_CACHE_STATE_PRESENT |
          test_util::TEST_CACHE_STATE_PINNED);

  // Store the file with a modified content and md5. It should stay pinned.
  TestStoreToCache(resource_id, md5_modified, dummy_file_path_, FILE_ERROR_OK,
                   test_util::TEST_CACHE_STATE_PRESENT |
                   test_util::TEST_CACHE_STATE_PINNED);
}

// Tests FileCache methods working with the blocking task runner.
class FileCacheTest : public testing::Test {
 protected:
  virtual void SetUp() OVERRIDE {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_free_disk_space_getter_.reset(new FakeFreeDiskSpaceGetter);

    cache_.reset(new FileCache(temp_dir_.path(),
                               base::MessageLoopProxy::current(),
                               fake_free_disk_space_getter_.get()));

    ASSERT_TRUE(cache_->Initialize());
  }

  virtual void TearDown() OVERRIDE {
    cache_.reset();
  }

  static void MigrateFilesFromOldDirectories(FileCache* cache) {
    cache->MigrateFilesFromOldDirectories();
  }

  content::TestBrowserThreadBundle thread_bundle_;
  base::ScopedTempDir temp_dir_;

  scoped_ptr<FileCache, test_util::DestroyHelperForTests> cache_;
  scoped_ptr<FakeFreeDiskSpaceGetter> fake_free_disk_space_getter_;
};

TEST_F(FileCacheTest, MigrateFilesFromOldDirectories) {
  const base::FilePath persistent_directory =
      temp_dir_.path().AppendASCII("persistent");
  const base::FilePath tmp_directory = temp_dir_.path().AppendASCII("tmp");
  const base::FilePath files_directory =
      cache_->GetCacheDirectoryPath(FileCache::CACHE_TYPE_FILES);

  // Prepare directories with previously used names.
  ASSERT_TRUE(file_util::CreateDirectory(persistent_directory));
  ASSERT_TRUE(file_util::CreateDirectory(tmp_directory));

  // Put some files.
  ASSERT_TRUE(google_apis::test_util::WriteStringToFile(
      persistent_directory.AppendASCII("foo.abc"), "foo"));
  ASSERT_TRUE(google_apis::test_util::WriteStringToFile(
      tmp_directory.AppendASCII("bar.123"), "bar"));

  // Migrate.
  MigrateFilesFromOldDirectories(cache_.get());

  EXPECT_FALSE(file_util::PathExists(persistent_directory));
  EXPECT_TRUE(file_util::PathExists(files_directory.AppendASCII("foo.abc")));
  EXPECT_TRUE(file_util::PathExists(files_directory.AppendASCII("bar.123")));
}

TEST_F(FileCacheTest, ScanCacheFile) {
  // Set up files in the cache directory.
  const base::FilePath directory =
      cache_->GetCacheDirectoryPath(FileCache::CACHE_TYPE_FILES);
  ASSERT_TRUE(google_apis::test_util::WriteStringToFile(
      directory.AppendASCII("id_foo.md5foo"), "foo"));
  ASSERT_TRUE(google_apis::test_util::WriteStringToFile(
      directory.AppendASCII("id_bar.local"), "bar"));

  // Remove the existing DB.
  ASSERT_TRUE(file_util::Delete(
      cache_->GetCacheDirectoryPath(FileCache::CACHE_TYPE_META),
      true /* recursive */));

  // Create a new cache and initialize it.
  cache_.reset(new FileCache(temp_dir_.path(),
                             base::MessageLoopProxy::current(),
                             fake_free_disk_space_getter_.get()));
  ASSERT_TRUE(cache_->Initialize());

  // Check contents of the cache.
  FileCacheEntry cache_entry;
  EXPECT_TRUE(cache_->GetCacheEntry("id_foo", std::string(), &cache_entry));
  EXPECT_TRUE(cache_entry.is_present());
  EXPECT_EQ("md5foo", cache_entry.md5());

  EXPECT_TRUE(cache_->GetCacheEntry("id_bar", std::string(), &cache_entry));
  EXPECT_TRUE(cache_entry.is_present());
  EXPECT_TRUE(cache_entry.is_dirty());
}

TEST_F(FileCacheTest, FreeDiskSpaceIfNeededFor) {
  base::FilePath src_file;
  ASSERT_TRUE(file_util::CreateTemporaryFileInDir(temp_dir_.path(), &src_file));

  // Store a file as a 'temporary' file and remember the path.
  const std::string resource_id_tmp = "id_tmp", md5_tmp = "md5_tmp";
  ASSERT_EQ(FILE_ERROR_OK,
            cache_->Store(resource_id_tmp, md5_tmp, src_file,
                          FileCache::FILE_OPERATION_COPY));
  base::FilePath tmp_path;
  ASSERT_EQ(FILE_ERROR_OK,
            cache_->GetFile(resource_id_tmp, md5_tmp, &tmp_path));

  // Store a file as a pinned file and remember the path.
  const std::string resource_id_pinned = "id_pinned", md5_pinned = "md5_pinned";
  ASSERT_EQ(FILE_ERROR_OK,
            cache_->Store(resource_id_pinned, md5_pinned, src_file,
                          FileCache::FILE_OPERATION_COPY));
  ASSERT_EQ(FILE_ERROR_OK, cache_->Pin(resource_id_pinned, md5_pinned));
  base::FilePath pinned_path;
  ASSERT_EQ(FILE_ERROR_OK,
            cache_->GetFile(resource_id_pinned, md5_pinned, &pinned_path));

  // Call FreeDiskSpaceIfNeededFor().
  fake_free_disk_space_getter_->set_default_value(test_util::kLotsOfSpace);
  fake_free_disk_space_getter_->PushFakeValue(0);
  const int64 kNeededBytes = 1;
  EXPECT_TRUE(cache_->FreeDiskSpaceIfNeededFor(kNeededBytes));

  // Only 'temporary' file gets removed.
  FileCacheEntry entry;
  EXPECT_FALSE(cache_->GetCacheEntry(resource_id_tmp, md5_tmp, &entry));
  EXPECT_FALSE(file_util::PathExists(tmp_path));

  EXPECT_TRUE(cache_->GetCacheEntry(resource_id_pinned, md5_pinned, &entry));
  EXPECT_TRUE(file_util::PathExists(pinned_path));

  // Returns false when disk space cannot be freed.
  fake_free_disk_space_getter_->set_default_value(0);
  EXPECT_FALSE(cache_->FreeDiskSpaceIfNeededFor(kNeededBytes));
}

}  // namespace internal
}  // namespace drive
