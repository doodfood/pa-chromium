// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ui/message_center/cocoa/popup_collection.h"

#include "base/memory/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#import "ui/base/test/ui_cocoa_test_helper.h"
#import "ui/message_center/cocoa/notification_controller.h"
#import "ui/message_center/cocoa/popup_controller.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/message_center_style.h"
#include "ui/message_center/notification.h"

@implementation MCNotificationController (TestingInterface)
- (NSImageView*)iconView {
  return icon_.get();
}
@end

class PopupCollectionTest : public ui::CocoaTest {
 public:
  PopupCollectionTest()
    : message_loop_(base::MessageLoop::TYPE_UI) {
    message_center::MessageCenter::Initialize();
    center_ = message_center::MessageCenter::Get();
    collection_.reset(
        [[MCPopupCollection alloc] initWithMessageCenter:center_]);
    [collection_ setAnimationDuration:0.001];
    [collection_ setAnimationEndedCallback:^{
        if (nested_run_loop_.get())
          nested_run_loop_->Quit();
    }];
  }

  virtual void TearDown() OVERRIDE {
    collection_.reset();  // Close all popups.
    ui::CocoaTest::TearDown();
  }

  virtual ~PopupCollectionTest() {
    message_center::MessageCenter::Shutdown();
  }

  void AddThreeNotifications() {
    scoped_ptr<message_center::Notification> notification;
    notification.reset(new message_center::Notification(
        message_center::NOTIFICATION_TYPE_SIMPLE,
        "1",
        ASCIIToUTF16("One"),
        ASCIIToUTF16("This is the first notification to"
                     " be displayed"),
        gfx::Image(),
        string16(),
        std::string(),
        message_center::RichNotificationData(),
        NULL));
    center_->AddNotification(notification.Pass());

    notification.reset(new message_center::Notification(
        message_center::NOTIFICATION_TYPE_SIMPLE,
        "2",
        ASCIIToUTF16("Two"),
        ASCIIToUTF16("This is the second notification."),
        gfx::Image(),
        string16(),
        std::string(),
        message_center::RichNotificationData(),
        NULL));
    center_->AddNotification(notification.Pass());

    notification.reset(new message_center::Notification(
        message_center::NOTIFICATION_TYPE_SIMPLE,
        "3",
        ASCIIToUTF16("Three"),
        ASCIIToUTF16("This is the third notification "
                     "that has a much longer body "
                     "than the other notifications. It "
                     "may not fit on the screen if we "
                     "set the screen size too small."),
        gfx::Image(),
        string16(),
        std::string(),
        message_center::RichNotificationData(),
        NULL));
    center_->AddNotification(notification.Pass());
    WaitForAnimationEnded();
  }

  bool CheckSpacingBetween(MCPopupController* upper, MCPopupController* lower) {
    CGFloat minY = NSMinY([[upper window] frame]);
    CGFloat maxY = NSMaxY([[lower window] frame]);
    CGFloat delta = minY - maxY;
    EXPECT_EQ(message_center::kMarginBetweenItems, delta);
    return delta == message_center::kMarginBetweenItems;
  }

  void WaitForAnimationEnded() {
    if (![collection_ isAnimating])
      return;
    nested_run_loop_.reset(new base::RunLoop());
    nested_run_loop_->Run();
    nested_run_loop_.reset();
  }

  base::MessageLoop message_loop_;
  scoped_ptr<base::RunLoop> nested_run_loop_;
  message_center::MessageCenter* center_;
  scoped_nsobject<MCPopupCollection> collection_;
};

TEST_F(PopupCollectionTest, AddThreeCloseOne) {
  EXPECT_EQ(0u, [[collection_ popups] count]);
  AddThreeNotifications();
  EXPECT_EQ(3u, [[collection_ popups] count]);

  center_->RemoveNotification("2", true);
  WaitForAnimationEnded();
  EXPECT_EQ(2u, [[collection_ popups] count]);
}

TEST_F(PopupCollectionTest, AttemptFourOneOffscreen) {
  [collection_ setScreenFrame:NSMakeRect(0, 0, 800, 300)];

  EXPECT_EQ(0u, [[collection_ popups] count]);
  AddThreeNotifications();
  EXPECT_EQ(2u, [[collection_ popups] count]);  // "3" does not fit on screen.

  scoped_ptr<message_center::Notification> notification;

  notification.reset(new message_center::Notification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      "4",
      ASCIIToUTF16("Four"),
      ASCIIToUTF16("This is the fourth notification."),
      gfx::Image(),
      string16(),
      std::string(),
      message_center::RichNotificationData(),
      NULL));
  center_->AddNotification(notification.Pass());
  WaitForAnimationEnded();

  // Remove "1" and "3" should fit on screen.
  center_->RemoveNotification("1", true);
  WaitForAnimationEnded();
  ASSERT_EQ(2u, [[collection_ popups] count]);

  EXPECT_EQ("2", [[[collection_ popups] objectAtIndex:0] notificationID]);
  EXPECT_EQ("3", [[[collection_ popups] objectAtIndex:1] notificationID]);

  // Remove "2" and "4" should fit on screen.
  center_->RemoveNotification("2", true);
  WaitForAnimationEnded();
  ASSERT_EQ(2u, [[collection_ popups] count]);

  EXPECT_EQ("3", [[[collection_ popups] objectAtIndex:0] notificationID]);
  EXPECT_EQ("4", [[[collection_ popups] objectAtIndex:1] notificationID]);
}

TEST_F(PopupCollectionTest, LayoutSpacing) {
  const CGFloat kScreenSize = 500;
  [collection_ setScreenFrame:NSMakeRect(0, 0, kScreenSize, kScreenSize)];

  AddThreeNotifications();
  NSArray* popups = [collection_ popups];

  EXPECT_EQ(message_center::kMarginBetweenItems,
            kScreenSize - NSMaxY([[[popups objectAtIndex:0] window] frame]));

  EXPECT_TRUE(CheckSpacingBetween([popups objectAtIndex:0],
                                  [popups objectAtIndex:1]));
  EXPECT_TRUE(CheckSpacingBetween([popups objectAtIndex:1],
                                  [popups objectAtIndex:2]));

  // Set priority so that kMaxVisiblePopupNotifications does not hide it.
  message_center::RichNotificationData optional;
  optional.priority = message_center::HIGH_PRIORITY;
  scoped_ptr<message_center::Notification> notification;
  notification.reset(new message_center::Notification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      "4",
      ASCIIToUTF16("Four"),
      ASCIIToUTF16("This is the fourth notification."),
      gfx::Image(),
      string16(),
      std::string(),
      optional,
      NULL));
  center_->AddNotification(notification.Pass());
  WaitForAnimationEnded();
  EXPECT_TRUE(CheckSpacingBetween([popups objectAtIndex:2],
                                  [popups objectAtIndex:3]));

  // Remove "2".
  center_->RemoveNotification("2", true);
  WaitForAnimationEnded();
  EXPECT_TRUE(CheckSpacingBetween([popups objectAtIndex:0],
                                  [popups objectAtIndex:1]));
  EXPECT_TRUE(CheckSpacingBetween([popups objectAtIndex:1],
                                  [popups objectAtIndex:2]));

  // Remove "1".
  center_->RemoveNotification("2", true);
  WaitForAnimationEnded();
  EXPECT_EQ(message_center::kMarginBetweenItems,
            kScreenSize - NSMaxY([[[popups objectAtIndex:0] window] frame]));
  EXPECT_TRUE(CheckSpacingBetween([popups objectAtIndex:0],
                                  [popups objectAtIndex:1]));
}

TEST_F(PopupCollectionTest, TinyScreen) {
  [collection_ setScreenFrame:NSMakeRect(0, 0, 800, 100)];

  EXPECT_EQ(0u, [[collection_ popups] count]);
  scoped_ptr<message_center::Notification> notification;
  notification.reset(new message_center::Notification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      "1",
      ASCIIToUTF16("One"),
      ASCIIToUTF16("This is the first notification to"
              " be displayed"),
      gfx::Image(),
      string16(),
      std::string(),
      message_center::RichNotificationData(),
      NULL));
  center_->AddNotification(notification.Pass());
  WaitForAnimationEnded();
  EXPECT_EQ(1u, [[collection_ popups] count]);

  // Now give the notification a longer message so that it no longer fits.
  notification.reset(new message_center::Notification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      "1",
      ASCIIToUTF16("One"),
      ASCIIToUTF16("This is now a very very very very "
              "very very very very very very very "
              "very very very very very very very "
              "very very very very very very very "
              "very very very very very very very "
              "very very very very very very very "
              "very very very very very very very "
              "long notification."),
      gfx::Image(),
      string16(),
      std::string(),
      message_center::RichNotificationData(),
      NULL));
  center_->UpdateNotification("1", notification.Pass());
  WaitForAnimationEnded();
  EXPECT_EQ(0u, [[collection_ popups] count]);
}

TEST_F(PopupCollectionTest, UpdateIconAndBody) {
  AddThreeNotifications();
  NSArray* popups = [collection_ popups];

  EXPECT_EQ(3u, [popups count]);

  // Update "2" icon.
  MCNotificationController* controller =
      [[popups objectAtIndex:1] notificationController];
  EXPECT_FALSE([[controller iconView] image]);
  center_->SetNotificationIcon("2",
      gfx::Image([[NSImage imageNamed:NSImageNameUser] retain]));
  WaitForAnimationEnded();
  EXPECT_TRUE([[controller iconView] image]);

  EXPECT_EQ(3u, [popups count]);
  EXPECT_TRUE(CheckSpacingBetween([popups objectAtIndex:0],
                                  [popups objectAtIndex:1]));
  EXPECT_TRUE(CheckSpacingBetween([popups objectAtIndex:1],
                                  [popups objectAtIndex:2]));

  // Replace "1".
  controller = [[popups objectAtIndex:0] notificationController];
  NSRect old_frame = [[controller view] frame];
  scoped_ptr<message_center::Notification> notification;
  notification.reset(new message_center::Notification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      "1",
      ASCIIToUTF16("One is going to get a much longer "
              "title than it previously had."),
      ASCIIToUTF16("This is the first notification to "
              "be displayed, but it will also be "
              "updated to have a significantly "
              "longer body"),
      gfx::Image(),
      string16(),
      std::string(),
      message_center::RichNotificationData(),
      NULL));
  center_->AddNotification(notification.Pass());
  WaitForAnimationEnded();
  EXPECT_GT(NSHeight([[controller view] frame]), NSHeight(old_frame));

  // Test updated spacing.
  EXPECT_EQ(3u, [popups count]);
  EXPECT_TRUE(CheckSpacingBetween([popups objectAtIndex:0],
                                  [popups objectAtIndex:1]));
  EXPECT_TRUE(CheckSpacingBetween([popups objectAtIndex:1],
                                  [popups objectAtIndex:2]));
  EXPECT_EQ("1", [[popups objectAtIndex:0] notificationID]);
  EXPECT_EQ("2", [[popups objectAtIndex:1] notificationID]);
  EXPECT_EQ("3", [[popups objectAtIndex:2] notificationID]);
}

// Test sometimes timesout. See http://crbug.com/249131
TEST_F(PopupCollectionTest,
       DISABLED_CloseCollectionBeforeNewPopupAnimationEnds) {
  // Add a notification and don't wait for the animation to finish.
  scoped_ptr<message_center::Notification> notification;
  notification.reset(new message_center::Notification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      "1",
      ASCIIToUTF16("One"),
      ASCIIToUTF16("This is the first notification to"
                   " be displayed"),
      gfx::Image(),
      string16(),
      std::string(),
      message_center::RichNotificationData(),
      NULL));
  center_->AddNotification(notification.Pass());

  // Release the popup collection before the animation ends. No crash should
  // be expected.
  collection_.reset();
}

TEST_F(PopupCollectionTest, CloseCollectionBeforeClosePopupAnimationEnds) {
  AddThreeNotifications();

  // Remove a notification and don't wait for the animation to finish.
  center_->RemoveNotification("1", true);

  // Release the popup collection before the animation ends. No crash should
  // be expected.
  collection_.reset();
}

TEST_F(PopupCollectionTest, CloseCollectionBeforeUpdatePopupAnimationEnds) {
  AddThreeNotifications();

  // Update a notification and don't wait for the animation to finish.
  scoped_ptr<message_center::Notification> notification;
  notification.reset(new message_center::Notification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      "1",
      ASCIIToUTF16("One"),
      ASCIIToUTF16("New message."),
      gfx::Image(),
      string16(),
      std::string(),
      message_center::RichNotificationData(),
      NULL));
  center_->UpdateNotification("1", notification.Pass());

  // Release the popup collection before the animation ends. No crash should
  // be expected.
  collection_.reset();
}
