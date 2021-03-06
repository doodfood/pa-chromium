// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tabs/dock_info.h"

#include <gtk/gtk.h>

#include "base/logging.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/gtk/browser_window_gtk.h"
#include "chrome/browser/ui/gtk/gtk_util.h"
#include "chrome/browser/ui/gtk/tabs/tab_gtk.h"
#include "chrome/browser/ui/host_desktop.h"
#include "ui/base/x/x11_util.h"
#include "ui/gfx/native_widget_types.h"

////////////////////////////////////////////////////////////////////////////////
// BaseWindowFinder
//
// Base class used to locate a window. A subclass need only override
// ShouldStopIterating to determine when iteration should stop.
class BaseWindowFinder : public ui::EnumerateWindowsDelegate {
 public:
  explicit BaseWindowFinder(const std::set<GtkWidget*>& ignore) {
    std::set<GtkWidget*>::iterator iter;
    for (iter = ignore.begin(); iter != ignore.end(); iter++) {
      XID xid = ui::GetX11WindowFromGtkWidget(*iter);
      ignore_.insert(xid);
    }
  }

  virtual ~BaseWindowFinder() {}

 protected:
  // Returns true if |window| is in the ignore list.
  bool ShouldIgnoreWindow(XID window) {
    return (ignore_.find(window) != ignore_.end());
  }

  // Returns true if iteration should stop, false otherwise.
  virtual bool ShouldStopIterating(XID window) OVERRIDE {
    return false;
  }

 private:
  std::set<XID> ignore_;

  DISALLOW_COPY_AND_ASSIGN(BaseWindowFinder);
};

////////////////////////////////////////////////////////////////////////////////
// TopMostFinder
//
// Helper class to determine if a particular point of a window is not obscured
// by another window.
class TopMostFinder : public BaseWindowFinder {
 public:
  // Returns true if |window| is not obscured by another window at the
  // location |screen_loc|, not including the windows in |ignore|.
  static bool IsTopMostWindowAtPoint(XID window,
                                     const gfx::Point& screen_loc,
                                     const std::set<GtkWidget*>& ignore) {
    TopMostFinder finder(window, screen_loc, ignore);
    return finder.is_top_most_;
  }

 protected:
  virtual bool ShouldStopIterating(XID window) OVERRIDE {
    if (BaseWindowFinder::ShouldIgnoreWindow(window))
      return false;

    if (window == target_) {
      // Window is topmost, stop iterating.
      is_top_most_ = true;
      return true;
    }

    if (!ui::IsWindowVisible(window)) {
      // The window isn't visible, keep iterating.
      return false;
    }

    if (ui::WindowContainsPoint(window, screen_loc_))
      return true;

    return false;
  }

 private:
  TopMostFinder(XID window,
                const gfx::Point& screen_loc,
                const std::set<GtkWidget*>& ignore)
    : BaseWindowFinder(ignore),
      target_(window),
      screen_loc_(screen_loc),
      is_top_most_(false) {
    ui::EnumerateTopLevelWindows(this);
  }

  // The window we're looking for.
  XID target_;

  // Location of window to find.
  gfx::Point screen_loc_;

  // Is target_ the top most window? This is initially false but set to true
  // in ShouldStopIterating if target_ is passed in.
  bool is_top_most_;

  DISALLOW_COPY_AND_ASSIGN(TopMostFinder);
};

////////////////////////////////////////////////////////////////////////////////
// LocalProcessWindowFinder
//
// Helper class to determine if a particular point of a window from our process
// is not obscured by another window.
class LocalProcessWindowFinder : public BaseWindowFinder {
 public:
  // Returns the XID from our process at screen_loc that is not obscured by
  // another window. Returns 0 otherwise.
  static XID GetProcessWindowAtPoint(const gfx::Point& screen_loc,
                                     const std::set<GtkWidget*>& ignore) {
    LocalProcessWindowFinder finder(screen_loc, ignore);
    if (finder.result_ &&
        TopMostFinder::IsTopMostWindowAtPoint(finder.result_, screen_loc,
                                              ignore)) {
      return finder.result_;
    }
    return 0;
  }

 protected:
  virtual bool ShouldStopIterating(XID window) OVERRIDE {
    if (BaseWindowFinder::ShouldIgnoreWindow(window))
      return false;

    // Check if this window is in our process.
    if (!BrowserWindowGtk::GetBrowserWindowForXID(window))
      return false;

    if (!ui::IsWindowVisible(window))
      return false;

    if (ui::WindowContainsPoint(window, screen_loc_)) {
      result_ = window;
      return true;
    }

    return false;
  }

 private:
  LocalProcessWindowFinder(const gfx::Point& screen_loc,
                           const std::set<GtkWidget*>& ignore)
    : BaseWindowFinder(ignore),
      screen_loc_(screen_loc),
      result_(0) {
    ui::EnumerateTopLevelWindows(this);
  }

  // Position of the mouse.
  gfx::Point screen_loc_;

  // The resulting window. This is initially null but set to true in
  // ShouldStopIterating if an appropriate window is found.
  XID result_;

  DISALLOW_COPY_AND_ASSIGN(LocalProcessWindowFinder);
};

// static
DockInfo DockInfo::GetDockInfoAtPoint(chrome::HostDesktopType host_desktop_type,
                                      const gfx::Point& screen_point,
                                      const std::set<GtkWidget*>& ignore) {
  NOTIMPLEMENTED();
  return DockInfo();
}

// static
GtkWindow* DockInfo::GetLocalProcessWindowAtPoint(
    chrome::HostDesktopType host_desktop_type,
    const gfx::Point& screen_point,
    const std::set<GtkWidget*>& ignore) {
  XID xid =
      LocalProcessWindowFinder::GetProcessWindowAtPoint(screen_point, ignore);
  return BrowserWindowGtk::GetBrowserWindowForXID(xid);
}

bool DockInfo::GetWindowBounds(gfx::Rect* bounds) const {
  if (!window())
    return false;

  int x, y, w, h;
  gtk_window_get_position(window(), &x, &y);
  gtk_window_get_size(window(), &w, &h);
  bounds->SetRect(x, y, w, h);
  return true;
}

void DockInfo::SizeOtherWindowTo(const gfx::Rect& bounds) const {
  gtk_window_move(window(), bounds.x(), bounds.y());
  gtk_window_resize(window(), bounds.width(), bounds.height());
}

// static
int DockInfo::GetHotSpotDeltaY() {
  return TabGtk::GetMinimumUnselectedSize().height() - 1;
}
