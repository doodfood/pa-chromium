// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "base/process_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/plugins/plugin_prefs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_process_type.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/browser_child_process_host_iterator.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/common/content_paths.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "net/base/net_util.h"
#include "webkit/plugins/plugin_constants.h"
#include "webkit/plugins/webplugininfo.h"

using content::BrowserThread;

namespace {

class CallbackBarrier : public base::RefCountedThreadSafe<CallbackBarrier> {
 public:
  explicit CallbackBarrier(const base::Closure& target_callback)
      : target_callback_(target_callback),
        outstanding_callbacks_(0),
        did_enable_(true) {
  }

  base::Callback<void(bool)> CreateCallback() {
    outstanding_callbacks_++;
    return base::Bind(&CallbackBarrier::MayRunTargetCallback, this);
  }

 private:
  friend class base::RefCountedThreadSafe<CallbackBarrier>;

  ~CallbackBarrier() {
    EXPECT_TRUE(target_callback_.is_null());
  }

  void MayRunTargetCallback(bool did_enable) {
    EXPECT_GT(outstanding_callbacks_, 0);
    did_enable_ = did_enable_ && did_enable;
    if (--outstanding_callbacks_ == 0) {
      EXPECT_TRUE(did_enable_);
      target_callback_.Run();
      target_callback_.Reset();
    }
  }

  base::Closure target_callback_;
  int outstanding_callbacks_;
  bool did_enable_;
};

}  // namespace

class ChromePluginTest : public InProcessBrowserTest {
 protected:
  ChromePluginTest() {}

  static GURL GetURL(const char* filename) {
    base::FilePath path;
    PathService::Get(content::DIR_TEST_DATA, &path);
    path = path.AppendASCII("plugin").AppendASCII(filename);
    CHECK(file_util::PathExists(path));
    return net::FilePathToFileURL(path);
  }

  static void LoadAndWait(Browser* window, const GURL& url, bool pass) {
    content::WebContents* web_contents =
        window->tab_strip_model()->GetActiveWebContents();
    string16 expected_title(ASCIIToUTF16(pass ? "OK" : "plugin_not_found"));
    content::TitleWatcher title_watcher(web_contents, expected_title);
    title_watcher.AlsoWaitForTitle(ASCIIToUTF16("FAIL"));
    title_watcher.AlsoWaitForTitle(ASCIIToUTF16(
        pass ? "plugin_not_found" : "OK"));
    ui_test_utils::NavigateToURL(window, url);
    ASSERT_EQ(expected_title, title_watcher.WaitAndGetTitle());
  }

  static void CrashFlash() {
    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    BrowserThread::PostTask(
        BrowserThread::IO,
        FROM_HERE,
        base::Bind(&CrashFlashInternal, runner->QuitClosure()));
    runner->Run();
  }

  static void GetFlashPath(std::vector<base::FilePath>* paths) {
    paths->clear();
    std::vector<webkit::WebPluginInfo> plugins = GetPlugins();
    for (std::vector<webkit::WebPluginInfo>::const_iterator it =
             plugins.begin(); it != plugins.end(); ++it) {
      if (it->name == ASCIIToUTF16(kFlashPluginName))
        paths->push_back(it->path);
    }
  }

  static std::vector<webkit::WebPluginInfo> GetPlugins() {
    std::vector<webkit::WebPluginInfo> plugins;
    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    content::PluginService::GetInstance()->GetPlugins(
        base::Bind(&GetPluginsInfoCallback, &plugins, runner->QuitClosure()));
    runner->Run();
    return plugins;
  }

  static void EnableFlash(bool enable, Profile* profile) {
    std::vector<base::FilePath> paths;
    GetFlashPath(&paths);
    ASSERT_FALSE(paths.empty());

    PluginPrefs* plugin_prefs = PluginPrefs::GetForProfile(profile).get();
    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    scoped_refptr<CallbackBarrier> callback_barrier(
        new CallbackBarrier(runner->QuitClosure()));
    for (std::vector<base::FilePath>::iterator iter = paths.begin();
         iter != paths.end(); ++iter) {
      plugin_prefs->EnablePlugin(enable, *iter,
                                 callback_barrier->CreateCallback());
    }
    runner->Run();
  }

  static void EnsureFlashProcessCount(int expected) {
    int actual = 0;
    scoped_refptr<content::MessageLoopRunner> runner =
        new content::MessageLoopRunner;
    BrowserThread::PostTask(
        BrowserThread::IO,
        FROM_HERE,
        base::Bind(&CountPluginProcesses, &actual, runner->QuitClosure()));
    runner->Run();
    ASSERT_EQ(expected, actual);
  }

 private:
  static void CrashFlashInternal(const base::Closure& quit_task) {
    bool found = false;
    for (content::BrowserChildProcessHostIterator iter; !iter.Done(); ++iter) {
      if (iter.GetData().process_type != content::PROCESS_TYPE_PLUGIN &&
          iter.GetData().process_type != content::PROCESS_TYPE_PPAPI_PLUGIN) {
        continue;
      }
      base::KillProcess(iter.GetData().handle, 0, true);
      found = true;
    }
    ASSERT_TRUE(found) << "Didn't find Flash process!";
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, quit_task);
  }

  static void GetPluginsInfoCallback(
      std::vector<webkit::WebPluginInfo>* rv,
      const base::Closure& quit_task,
      const std::vector<webkit::WebPluginInfo>& plugins) {
    *rv = plugins;
    quit_task.Run();
  }

  static void CountPluginProcesses(int* count, const base::Closure& quit_task) {
    for (content::BrowserChildProcessHostIterator iter; !iter.Done(); ++iter) {
      if (iter.GetData().process_type == content::PROCESS_TYPE_PLUGIN ||
          iter.GetData().process_type == content::PROCESS_TYPE_PPAPI_PLUGIN) {
        (*count)++;
      }
    }
    BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, quit_task);
  }
};

// Tests a bunch of basic scenarios with Flash.
// This test fails under ASan on Mac, see http://crbug.com/147004.
// It fails elsewhere, too.  See http://crbug.com/152071.
IN_PROC_BROWSER_TEST_F(ChromePluginTest, DISABLED_Flash) {
  // Official builds always have bundled Flash.
#if !defined(OFFICIAL_BUILD)
  std::vector<base::FilePath> flash_paths;
  GetFlashPath(&flash_paths);
  if (flash_paths.empty()) {
    LOG(INFO) << "Test not running because couldn't find Flash.";
    return;
  }
#endif

  GURL url = GetURL("flash.html");
  EnsureFlashProcessCount(0);

  // Try a single tab.
  ASSERT_NO_FATAL_FAILURE(LoadAndWait(browser(), url, true));
  EnsureFlashProcessCount(1);
  Profile* profile = browser()->profile();
  // Try another tab.
  ASSERT_NO_FATAL_FAILURE(LoadAndWait(CreateBrowser(profile), url, true));
  // Try an incognito window.
  ASSERT_NO_FATAL_FAILURE(LoadAndWait(CreateIncognitoBrowser(), url, true));
  EnsureFlashProcessCount(1);

  // Now kill Flash process and verify it reloads.
  CrashFlash();
  EnsureFlashProcessCount(0);

  ASSERT_NO_FATAL_FAILURE(LoadAndWait(browser(), url, true));
  EnsureFlashProcessCount(1);

  // Now try disabling it.
  EnableFlash(false, profile);
  CrashFlash();

  ASSERT_NO_FATAL_FAILURE(LoadAndWait(browser(), url, false));
  EnsureFlashProcessCount(0);

  // Now enable it again.
  EnableFlash(true, profile);
  ASSERT_NO_FATAL_FAILURE(LoadAndWait(browser(), url, true));
  EnsureFlashProcessCount(1);
}

// Verify that the official builds have the known set of plugins.
IN_PROC_BROWSER_TEST_F(ChromePluginTest, InstalledPlugins) {
#if !defined(OFFICIAL_BUILD)
  return;
#endif
  const char* expected[] = {
    "Chrome PDF Viewer",
    "Shockwave Flash",
    "Native Client",
    "Chrome Remote Desktop Viewer",
#if defined(OS_CHROMEOS)
    "Google Talk Plugin",
    "Google Talk Plugin Video Accelerator",
    "Netflix",
#endif
  };

  std::vector<webkit::WebPluginInfo> plugins = GetPlugins();
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(expected); ++i) {
    size_t j = 0;
    for (; j < plugins.size(); ++j) {
      if (plugins[j].name == ASCIIToUTF16(expected[i]))
        break;
    }
    ASSERT_TRUE(j != plugins.size()) << "Didn't find " << expected[i];
  }
}
