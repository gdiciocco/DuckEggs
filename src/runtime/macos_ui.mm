#include "platform/macos_shared.hpp"
#include "schiva/heatmap.hpp"
#include "schiva/runtime.hpp"
#include "ui.hpp"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

using schiva::AdaptiveApplication;
using schiva::Json;
namespace detail = schiva::detail;
namespace heatmap = schiva::heatmap;

namespace {
constexpr NSSize kMainSize{600, 360};
NSString *const kLoginLabel = @"io.github.gdiciocco.DuckEggs";
NSString *const kLoginFile = @"io.github.gdiciocco.DuckEggs.plist";
NSString *const kMotto =
    @"I was walking past a farm and a sign said, “Duck, eggs.”\nI thought, "
    @"“That’s an unnecessary comma…” – and then it hit me.";
NSImage *app_logo() {
  NSURL *resource = [NSBundle.mainBundle URLForResource:@"duckeggs-logo-256"
                                          withExtension:@"png"];
  NSImage *image =
      resource ? [[NSImage alloc] initWithContentsOfURL:resource] : nil;
  if (image)
    return image;
  NSMutableArray<NSString *> *candidates = [NSMutableArray array];
  [candidates addObject:[NSFileManager.defaultManager.currentDirectoryPath
                            stringByAppendingPathComponent:
                                @"resources/duckeggs-logo-256.png"]];
  NSString *root = NSBundle.mainBundle.bundlePath;
  for (int i = 0; i < 5 && root.length; i++) {
    [candidates
        addObject:[[root stringByAppendingPathComponent:@"resources"]
                      stringByAppendingPathComponent:@"duckeggs-logo-256.png"]];
    root = root.stringByDeletingLastPathComponent;
  }
  for (NSString *path in candidates) {
    image = [[NSImage alloc] initWithContentsOfFile:path];
    if (image)
      return image;
  }
  return nil;
}
NSColor *color(std::string_view s) {
  if (s.size() != 7 || s[0] != '#')
    return [NSColor colorWithWhite:.22 alpha:1];
  unsigned v = 0;
  for (size_t i = 1; i < 7; i++) {
    char c = s[i];
    v = (v << 4) | (c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : 0);
  }
  return [NSColor colorWithRed:((v >> 16) & 255) / 255.0
                         green:((v >> 8) & 255) / 255.0
                          blue:(v & 255) / 255.0
                         alpha:1];
}
NSString *ns(const std::string &s) {
  NSString *v = [[NSString alloc] initWithBytes:s.data()
                                         length:s.size()
                                       encoding:NSUTF8StringEncoding];
  return v ? v : @"";
}
NSString *login_path() {
  return [[NSHomeDirectory()
      stringByAppendingPathComponent:@"Library/LaunchAgents"]
      stringByAppendingPathComponent:kLoginFile];
}
NSRect quartz_rect(schiva::Rect r, CGFloat h) {
  return NSMakeRect(r.left, h - r.bottom, r.width(), r.height());
}
schiva::Point quartz_point(NSPoint p, CGFloat h) {
  return detail::cocoa_to_quartz_point(p.x, p.y, h);
}
schiva::Rect quartz_rect(NSRect r, CGFloat h) {
  return detail::cocoa_to_quartz_rect(
      NSMinX(r), NSMinY(r), NSMaxX(r), NSMaxY(r), h);
}
NSRect map_rect(heatmap::NormalizedRect r, NSRect a) {
  return NSMakeRect(
      NSMinX(a) + r.left * NSWidth(a), NSMinY(a) + (1 - r.bottom) * NSHeight(a),
      (r.right - r.left) * NSWidth(a), (r.bottom - r.top) * NSHeight(a));
}
NSTextField *make_label(NSString *s, NSRect r, CGFloat z, NSColor *c,
                        NSFontWeight w = NSFontWeightRegular) {
  auto *f = [[NSTextField alloc] initWithFrame:r];
  f.stringValue = s;
  f.editable = NO;
  f.selectable = NO;
  f.bezeled = NO;
  f.drawsBackground = NO;
  f.textColor = c;
  f.font = [NSFont systemFontOfSize:z weight:w];
  return f;
}
NSButton *make_button(NSString *s, NSRect r, id target, SEL action) {
  auto *b = [[NSButton alloc] initWithFrame:r];
  b.title = s;
  b.target = target;
  b.action = action;
  b.bezelStyle = NSBezelStyleRounded;
  b.font = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
  return b;
}
NSURL *privacy_url(NSString *pane) {
  return [NSURL
      URLWithString:
          [@"x-apple.systempreferences:com.apple.preference.security?Privacy_"
              stringByAppendingString:pane]];
}
} // namespace

@interface DuckEggsUI : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@public
  AdaptiveApplication *_app;
  NSWindow *_main, *_maps;
  NSStatusItem *_status;
  NSTimer *_timer;
  NSButton *_pause, *_startup;
  NSTextField *_state, *_permissions, *_error, *_mapNotice;
  NSPopUpButton *_profiles;
  NSSegmentedControl *_layers;
  NSView *_mapCanvas;
  NSMutableArray<NSPanel *> *_picker;
  std::vector<NSRect> _screenFrames;
  std::vector<std::uint32_t> _pickerIds;
  std::optional<std::string> _profile;
  std::string _layer;
  long long _insightsRevision, _selectionRevision, _resetRevision;
  NSString *_lastState;
  NSString *_lastPermission;
  NSString *_lastError;
  NSString *_lastPause;
  NSString *_lastMapNotice;
  std::chrono::steady_clock::time_point _lastMapRequest;
  bool _pickerRequested, _selectionPending, _resetPending, _mapOpen;
  bool _closing, _restoreMainAfterPicker, _restoreMapsAfterPicker;
  int _hoverColumn, _hoverRow;
  CGFloat _primaryHeight_;
}
- (instancetype)initWithApplication:(AdaptiveApplication *)app;
- (void)tick:(NSTimer *)timer;
- (void)togglePause:(id)sender;
- (void)chooseWindow:(id)sender;
- (void)showMaps:(id)sender;
- (void)donate:(id)sender;
- (void)donateItem:(NSMenuItem *)item;
- (void)permissions:(id)sender;
- (void)startupChanged:(id)sender;
- (void)hide:(id)sender;
- (void)restore;
- (void)quit;
- (void)cancelPicker:(id)sender;
- (void)automatic:(id)sender;
- (void)profileChanged:(id)sender;
- (void)layerChanged:(id)sender;
- (void)mapMoved:(NSPoint)point;
- (void)pickerMoved:(NSPoint)point;
- (void)pickerClicked:(NSPoint)point;
- (void)drawMap;
- (void)drawPicker:(NSUInteger)index;
- (void)closeMain;
- (void)publishScreens;
- (void)publishPickerIDs;
- (void)buildMain;
- (void)buildStatus;
- (void)loadStartupState;
- (void)openPickerPanels;
- (void)closePicker:(BOOL)notify;
- (void)refreshPicker;
- (void)refreshMaps;
- (void)rebuildProfiles:(const Json &)data;
- (void)setLogin:(BOOL)enabled;
@end
@interface DEHeatmapView : NSView
@property(nonatomic, weak) DuckEggsUI *controller;
@property(nonatomic, strong) NSTrackingArea *trackingArea;
@end
@interface DEPickerPanel : NSPanel
@end
@interface DEPickerView : NSView
@property(nonatomic, weak) DuckEggsUI *controller;
@property(nonatomic, assign) NSUInteger screenIndex;
@property(nonatomic, assign) BOOL leftDown;
@property(nonatomic, strong) NSTrackingArea *trackingArea;
@end
@implementation DEPickerPanel
- (BOOL)canBecomeKeyWindow {
  return YES;
}
- (BOOL)canBecomeMainWindow {
  return NO;
}
@end
@implementation DEHeatmapView
- (BOOL)acceptsFirstResponder {
  return YES;
}
- (void)drawRect:(NSRect)dirty {
  (void)dirty;
  [self.controller drawMap];
}
- (void)mouseMoved:(NSEvent *)e {
  [self.controller mapMoved:[self convertPoint:e.locationInWindow
                                      fromView:nil]];
}
- (void)updateTrackingAreas {
  if (self.trackingArea)
    [self removeTrackingArea:self.trackingArea];
  self.trackingArea = [[NSTrackingArea alloc]
      initWithRect:NSZeroRect
           options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow |
                   NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:self.trackingArea];
  [super updateTrackingAreas];
}
@end
@implementation DEPickerView
- (BOOL)acceptsFirstResponder {
  return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent *)e {
  (void)e;
  return YES;
}
- (void)mouseMoved:(NSEvent *)e {
  [self.controller
      pickerMoved:[self.window convertPointToScreen:e.locationInWindow]];
}
- (void)mouseDragged:(NSEvent *)e {
  [self mouseMoved:e];
}
- (void)updateTrackingAreas {
  if (self.trackingArea)
    [self removeTrackingArea:self.trackingArea];
  self.trackingArea = [[NSTrackingArea alloc]
      initWithRect:NSZeroRect
           options:NSTrackingMouseMoved | NSTrackingActiveAlways |
                   NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:self.trackingArea];
  [super updateTrackingAreas];
}
- (void)mouseDown:(NSEvent *)e {
  (void)e;
  self.leftDown = YES;
}
- (void)mouseUp:(NSEvent *)e {
  NSPoint local = [self convertPoint:e.locationInWindow fromView:nil];
  BOOL accept = self.leftDown && local.y < NSHeight(self.bounds) - 64;
  self.leftDown = NO;
  if (accept)
    [self.controller
        pickerClicked:[self.window convertPointToScreen:e.locationInWindow]];
}
- (void)rightMouseUp:(NSEvent *)e {
  (void)e;
  [self.controller cancelPicker:nil];
}
- (void)keyDown:(NSEvent *)e {
  if (e.keyCode == 53)
    [self.controller cancelPicker:nil];
  else
    [super keyDown:e];
}
- (void)drawRect:(NSRect)dirty {
  (void)dirty;
  [self.controller drawPicker:self.screenIndex];
}
@end

@implementation DuckEggsUI
- (instancetype)initWithApplication:(AdaptiveApplication *)app {
  if ((self = [super init])) {
    _app = app;
    _layer = "activity";
    _picker = [NSMutableArray array];
    _insightsRevision = -1;
    _selectionRevision = -1;
    _resetRevision = -1;
    _hoverColumn = -1;
    _hoverRow = -1;
  }
  return self;
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
  (void)note;
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [self publishScreens];
  [self buildMain];
  [self buildStatus];
  [self loadStartupState];
  [self restore];
  _timer = [NSTimer scheduledTimerWithTimeInterval:.25
                                            target:self
                                          selector:@selector(tick:)
                                          userInfo:nil
                                           repeats:YES];
  [NSApp activateIgnoringOtherApps:YES];
}
- (void)publishScreens {
  NSArray<NSScreen *> *screens = NSScreen.screens;
  if (!screens.count) {
    detail::clear_macos_screens();
    _screenFrames.clear();
    return;
  }
  _primaryHeight_ = NSMaxY(screens[0].frame);
  std::vector<detail::MacScreenGeometry> published;
  std::vector<NSRect> frames;
  for (NSScreen *s in screens) {
    NSRect f = s.frame, v = s.visibleFrame;
    frames.push_back(f);
    frames.push_back(v);
    NSNumber *n = s.deviceDescription[@"NSScreenNumber"];
    detail::MacScreenGeometry x;
    x.display_id = n.unsignedIntValue;
    x.frame = quartz_rect(f, _primaryHeight_);
    x.visible_frame = quartz_rect(v, _primaryHeight_);
    published.push_back(x);
  }
  if (frames.size() != _screenFrames.size() ||
      !std::equal(frames.begin(), frames.end(), _screenFrames.begin(),
                  [](NSRect a, NSRect b) { return NSEqualRects(a, b); })) {
    detail::publish_macos_screens(std::move(published));
    _screenFrames = std::move(frames);
  }
}
- (void)publishPickerIDs {
  std::vector<std::uint32_t> ids;
  for (NSPanel *p in _picker)
    if (p.windowNumber > 0)
      ids.push_back((std::uint32_t)p.windowNumber);
  if (ids != _pickerIds) {
    detail::publish_macos_picker_windows(ids);
    _pickerIds = std::move(ids);
  }
}
- (void)buildMain {
  _main = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, kMainSize.width, kMainSize.height)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  _main.title = @"DuckEggs";
  _main.delegate = self;
  _main.contentMinSize = kMainSize;
  _main.backgroundColor = [NSColor colorWithCalibratedRed:.067
                                                    green:.082
                                                     blue:.11
                                                    alpha:1];
  NSView *v = _main.contentView;
  [_main center];
  NSImage *image = app_logo();
  NSImageView *logo =
      [[NSImageView alloc] initWithFrame:NSMakeRect(24, 298, 42, 42)];
  logo.image = image;
  logo.imageScaling = NSImageScaleProportionallyUpOrDown;
  [v addSubview:logo];
  [v addSubview:make_label(kMotto, NSMakeRect(78, 291, 500, 52), 13,
                           [NSColor colorWithWhite:.91 alpha:1])];
  _state = make_label(@"Starting safely…", NSMakeRect(26, 253, 548, 23), 15,
                      [NSColor colorWithCalibratedRed:.45
                                                green:.86
                                                 blue:.77
                                                alpha:1],
                      NSFontWeightSemibold);
  [v addSubview:_state];
  _permissions =
      make_label(@"Checking permissions…", NSMakeRect(26, 230, 550, 18), 11,
                 [NSColor colorWithWhite:.68 alpha:1]);
  [v addSubview:_permissions];
  _startup = [[NSButton alloc] initWithFrame:NSMakeRect(26, 201, 230, 22)];
  _startup.buttonType = NSSwitchButton;
  _startup.title = @"Start with macOS";
  _startup.font = [NSFont systemFontOfSize:12];
  _startup.target = self;
  _startup.action = @selector(startupChanged:);
  [v addSubview:_startup];
  _pause = make_button(@"Resume", NSMakeRect(26, 153, 126, 38), self,
                       @selector(togglePause:));
  [v addSubview:_pause];
  [v addSubview:make_button(@"Choose Window", NSMakeRect(166, 153, 145, 38),
                            self, @selector(chooseWindow:))];
  [v addSubview:make_button(@"Learning maps", NSMakeRect(325, 153, 135, 38),
                            self, @selector(showMaps:))];
  NSButton *d = make_button(@"Donate", NSMakeRect(474, 153, 100, 38), self,
                            @selector(donate:));
  d.contentTintColor = [NSColor colorWithCalibratedRed:.95
                                                 green:.78
                                                  blue:.38
                                                 alpha:1];
  [v addSubview:d];
  [v addSubview:make_button(@"Permissions", NSMakeRect(26, 91, 130, 34), self,
                            @selector(permissions:))];
  [v addSubview:make_button(@"Hide to menu bar", NSMakeRect(166, 91, 170, 34),
                            self, @selector(hide:))];
  _error = make_label(@"", NSMakeRect(26, 39, 548, 40), 11,
                      [NSColor colorWithCalibratedRed:1
                                                green:.57
                                                 blue:.54
                                                alpha:1]);
  _error.lineBreakMode = NSLineBreakByWordWrapping;
  _error.maximumNumberOfLines = 2;
  [v addSubview:_error];
  [v addSubview:make_label(@"Movement is local. Visual learning is optional.",
                           NSMakeRect(26, 13, 548, 18), 10,
                           [NSColor colorWithWhite:.54 alpha:1])];
}
- (void)buildStatus {
  NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@"DuckEggs"];
  NSMenuItem *appItem = [[NSMenuItem alloc] init];
  NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"DuckEggs"];
  NSMenuItem *quitItem = [appMenu addItemWithTitle:@"Quit DuckEggs"
                                            action:@selector(quit)
                                     keyEquivalent:@"q"];
  quitItem.target = self;
  quitItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
  appItem.submenu = appMenu;
  [mainMenu addItem:appItem];
  NSApp.mainMenu = mainMenu;
  _status = [NSStatusBar.systemStatusBar
      statusItemWithLength:NSVariableStatusItemLength];
  _status.button.title = @"";
  _status.button.image = app_logo();
  _status.button.image.size = NSMakeSize(18, 18);
  NSMenu *m = [[NSMenu alloc] init];
  NSMenuItem *i = [m addItemWithTitle:@"Open DuckEggs"
                               action:@selector(restore)
                        keyEquivalent:@""];
  i.target = self;
  i = [m addItemWithTitle:@"Pause / Resume"
                   action:@selector(togglePause:)
            keyEquivalent:@""];
  i.target = self;
  [m addItem:NSMenuItem.separatorItem];
  i = [m addItemWithTitle:@"Quit DuckEggs"
                   action:@selector(quit)
            keyEquivalent:@""];
  i.target = self;
  _status.menu = m;
}
- (void)loadStartupState {
  NSData *d = [NSData dataWithContentsOfFile:login_path()];
  bool enabled = false;
  if (d) {
    NSError *e = nil;
    id o = [NSPropertyListSerialization
        propertyListWithData:d
                     options:NSPropertyListImmutable
                      format:nil
                       error:&e];
    if (!e && [o isKindOfClass:NSDictionary.class]) {
      NSDictionary *x = o;
      NSArray *a = x[@"ProgramArguments"];
      NSString *exe = NSBundle.mainBundle.executableURL.path;
      if (!exe.length)
        exe = NSProcessInfo.processInfo.arguments.firstObject;
      enabled = [x[@"Label"] isEqual:kLoginLabel] &&
                [a isKindOfClass:NSArray.class] && a.count == 1 &&
                [a[0] isEqual:exe.stringByStandardizingPath];
    }
  }
  _startup.state = enabled ? NSControlStateValueOn : NSControlStateValueOff;
}
- (void)tick:(NSTimer *)timer {
  (void)timer;
  if (_closing)
    return;
  if (_app->finished()) {
    [self closeMain];
    return;
  }
  [self publishScreens];
  Json s = _app->snapshot();
  NSString *pauseTitle = s.value("paused", true) ? @"Resume" : @"Pause";
  if (![_lastPause isEqualToString:pauseTitle]) {
    _lastPause = [pauseTitle copy];
    _pause.title = pauseTitle;
  }
  const std::string stateValue = s.value("state", std::string("starting"));
  const int candidates = s.value("candidate_count", 0);
  NSString *badge = s.value("paused", true)
                        ? @"Paused"
                        : (s.value("motion_active", false) ||
                                   stateValue == "moving"
                               ? @"Moving"
                               : (stateValue == "no_pip" || candidates == 0
                                      ? @"No target"
                                      : (stateValue == "starting"
                                             ? @"Starting"
                                             : (stateValue == "error"
                                                    ? @"Needs attention"
                                                    : @"Watching"))));
  NSString *state = [NSString stringWithFormat:
                          @"%@ · Learning updates: %d", badge,
                          s.value("learning_count", 0)];
  if (![_lastState isEqualToString:state]) {
    _lastState = [state copy];
    _state.stringValue = state;
  }
  NSString *permission = [NSString
      stringWithFormat:@"Accessibility %@ · Screen Recording %@",
                       AXIsProcessTrusted() ? @"ready" : @"needed for movement",
                       CGPreflightScreenCaptureAccess()
                           ? @"ready"
                           : @"optional for visual learning"];
  if (![_lastPermission isEqualToString:permission]) {
    _lastPermission = [permission copy];
    _permissions.stringValue = permission;
  }
  Json e = s.value("last_error", Json(nullptr));
  NSString *error = e.is_string() ? ns(e.get<std::string>()) : @"";
  if (![_lastError isEqualToString:error]) {
    _lastError = [error copy];
    _error.stringValue = error;
    _error.hidden = !e.is_string();
  }
  [self refreshPicker];
  if (_mapOpen) {
    if (std::chrono::steady_clock::now() - _lastMapRequest >=
        std::chrono::seconds(1)) {
      _app->request_insights(_profile);
      _lastMapRequest = std::chrono::steady_clock::now();
    }
    [self refreshMaps];
  }
  for (NSPanel *p in _picker)
    p.contentView.needsDisplay = YES;
}
- (void)togglePause:(id)sender {
  (void)sender;
  _app->request_pause();
}
- (void)chooseWindow:(id)sender {
  (void)sender;
  if (_pickerRequested)
    return;
  _pickerRequested = true;
  _app->request_manual_picker();
}
- (void)openPickerPanels {
  if (_picker.count != 0)
    return;
  _restoreMainAfterPicker = _main.isVisible;
  _restoreMapsAfterPicker = _mapOpen && _maps.isVisible;
  if (_restoreMainAfterPicker)
    [_main orderOut:nil];
  if (_restoreMapsAfterPicker)
    [_maps orderOut:nil];
  [self publishScreens];
  for (NSScreen *s in NSScreen.screens) {
    DEPickerPanel *p =
        [[DEPickerPanel alloc] initWithContentRect:s.frame
                                         styleMask:NSWindowStyleMaskBorderless
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    p.backgroundColor = [NSColor colorWithWhite:.015 alpha:.21];
    p.opaque = NO;
    p.hasShadow = NO;
    p.level = NSStatusWindowLevel;
    p.acceptsMouseMovedEvents = YES;
    p.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                           NSWindowCollectionBehaviorFullScreenAuxiliary;
    DEPickerView *v = [[DEPickerView alloc]
        initWithFrame:NSMakeRect(0, 0, NSWidth(s.frame), NSHeight(s.frame))];
    v.controller = self;
    v.screenIndex = _picker.count;
    p.contentView = v;
    [v addSubview:make_label(
                      @"Choose a window · click to select · Escape to cancel",
                      NSMakeRect(22, NSHeight(s.frame) - 52, 440, 28), 14,
                      [NSColor whiteColor], NSFontWeightSemibold)];
    [v addSubview:make_button(@"Cancel",
                              NSMakeRect(NSWidth(s.frame) - 108,
                                         NSHeight(s.frame) - 52, 86, 30),
                              self, @selector(cancelPicker:))];
    [v addSubview:make_button(@"Automatic",
                              NSMakeRect(NSWidth(s.frame) - 214,
                                         NSHeight(s.frame) - 52, 94, 30),
                              self, @selector(automatic:))];
    [p orderFrontRegardless];
    [p makeKeyWindow];
    [p makeFirstResponder:v];
    [_picker addObject:p];
  }
  [self publishPickerIDs];
}
- (void)closePicker:(BOOL)notify {
  for (NSPanel *p in _picker)
    [p orderOut:nil];
  _picker = [NSMutableArray array];
  [self publishPickerIDs];
  if (notify)
    _app->request_manual_picker_closed();
  if (_restoreMainAfterPicker)
    [_main orderFront:nil];
  if (_restoreMapsAfterPicker)
    [_maps orderFront:nil];
  _restoreMainAfterPicker = false;
  _restoreMapsAfterPicker = false;
  _pickerRequested = false;
  _selectionPending = false;
  _resetPending = false;
}
- (void)refreshPicker {
  if (_pickerRequested && _picker.count == 0 &&
      _app->manual_snapshot().value("picker_open", false))
    [self openPickerPanels];
  if (_selectionPending) {
    Json s = _app->manual_snapshot();
    if (s.value("selection_revision", 0LL) > _selectionRevision) {
      Json e = s.value("error", Json(nullptr));
      if (!e.is_string() && s.value("mode", std::string{}) == "manual" &&
          s.contains("selected_id") && s["selected_id"].is_string())
        [self closePicker:YES];
      else {
        _selectionPending = false;
        for (NSPanel *p in _picker)
          p.contentView.needsDisplay = YES;
      }
    }
  }
  if (_resetPending) {
    Json s = _app->manual_snapshot();
    if (s.value("revision", 0LL) > _resetRevision &&
        s.value("mode", std::string{}) == "automatic" &&
        (!s.contains("selected_id") || s["selected_id"].is_null()) &&
        (!s.contains("error") || s["error"].is_null()))
      [self closePicker:YES];
  }
}
- (void)pickerMoved:(NSPoint)p {
  if (_picker.count != 0)
    _app->request_manual_hover(quartz_point(p, _primaryHeight_));
}
- (void)pickerClicked:(NSPoint)p {
  if (!_pickerRequested || _picker.count == 0 || _selectionPending)
    return;
  _selectionRevision = _app->manual_snapshot().value("selection_revision", 0LL);
  _selectionPending = true;
  _app->request_manual_selection(quartz_point(p, _primaryHeight_));
}
- (void)drawPicker:(NSUInteger)i {
  if (i >= _picker.count)
    return;
  Json s = _app->manual_snapshot(), h = s.value("hover", Json::object());
  if (!h.value("found", false) || !h.value("width", 0))
    return;
  schiva::Rect r{h.value("left", 0), h.value("top", 0),
                 h.value("left", 0) + h.value("width", 0),
                 h.value("top", 0) + h.value("height", 0)};
  NSRect global = quartz_rect(r, _primaryHeight_),
         local = NSOffsetRect(global, -_picker[i].frame.origin.x,
                              -_picker[i].frame.origin.y);
  bool eligible = h.value("eligible", false);
  NSColor *c = eligible ? [NSColor colorWithCalibratedRed:.23
                                                    green:.85
                                                     blue:.67
                                                    alpha:.92]
                        : [NSColor colorWithCalibratedRed:.55
                                                    green:.58
                                                     blue:.62
                                                    alpha:.9];
  [c setStroke];
  NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:local
                                                    xRadius:8
                                                    yRadius:8];
  p.lineWidth = 4;
  [p stroke];
  NSString *t = ns(h.value("title", std::string{}));
  NSString *reason = ns(h.value("reason", std::string{}));
  NSString *caption =
      eligible ? t : [NSString stringWithFormat:@"%@ · %@", t, reason];
  [caption drawAtPoint:NSMakePoint(24, NSMaxY(local) + 8)
        withAttributes:@{
          NSFontAttributeName : [NSFont systemFontOfSize:13
                                                  weight:NSFontWeightSemibold],
          NSForegroundColorAttributeName : [NSColor whiteColor]
        }];
}
- (void)cancelPicker:(id)sender {
  (void)sender;
  if (_pickerRequested)
    [self closePicker:YES];
}
- (void)automatic:(id)sender {
  (void)sender;
  _resetRevision = _app->manual_snapshot().value("revision", 0LL);
  _resetPending = true;
  _app->request_manual_reset();
}
- (void)showMaps:(id)sender {
  (void)sender;
  if (!_maps) {
    _maps = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 920, 660)
                                        styleMask:NSWindowStyleMaskTitled |
                                                  NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    _maps.title = @"DuckEggs · Learning maps";
    _maps.contentMinSize = NSMakeSize(860, 620);
    _maps.acceptsMouseMovedEvents = YES;
    _maps.releasedWhenClosed = NO;
    _maps.delegate = self;
    _maps.backgroundColor = [NSColor colorWithCalibratedRed:.067
                                                      green:.082
                                                       blue:.11
                                                      alpha:1];
    _profiles = [[NSPopUpButton alloc]
        initWithFrame:NSMakeRect(22, NSHeight(_maps.contentView.bounds) - 50,
                                 310, 30)
            pullsDown:NO];
    _profiles.target = self;
    _profiles.action = @selector(profileChanged:);
    [_maps.contentView addSubview:_profiles];
    _layers = [[NSSegmentedControl alloc]
        initWithFrame:NSMakeRect(348, NSHeight(_maps.contentView.bounds) - 50,
                                 490, 30)];
    _layers.segmentCount = 3;
    [_layers setLabel:@"Activity" forSegment:0];
    [_layers setLabel:@"Manual preference" forSegment:1];
    [_layers setLabel:@"Placement" forSegment:2];
    _layers.target = self;
    _layers.action = @selector(layerChanged:);
    _layers.selectedSegment = 0;
    [_maps.contentView addSubview:_layers];
    DEHeatmapView *v = [[DEHeatmapView alloc]
        initWithFrame:NSMakeRect(22, 76, 876,
                                 NSHeight(_maps.contentView.bounds) - 148)];
    v.controller = self;
    v.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _mapCanvas = v;
    [_maps.contentView addSubview:v];
    _mapNotice = make_label(
        @"Local learning · Unknown cells have no observation.",
        NSMakeRect(24, 22, NSWidth(_maps.contentView.bounds) - 48, 46), 12,
        [NSColor colorWithWhite:.78 alpha:1]);
    _mapNotice.autoresizingMask = NSViewWidthSizable;
    _mapNotice.lineBreakMode = NSLineBreakByWordWrapping;
    _mapNotice.maximumNumberOfLines = 2;
    [_maps.contentView addSubview:_mapNotice];
    [_maps center];
  }
  _mapOpen = true;
  _app->request_insights(_profile);
  _lastMapRequest = std::chrono::steady_clock::now();
  [self refreshMaps];
  [_maps makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}
- (void)refreshMaps {
  Json w = _app->insights_snapshot(), d = w.value("data", Json(nullptr));
  long long rev = w.value("revision", 0LL);
  if (!heatmap::bridge_data_matches_selection(d, _profile))
    d = Json::object();
  if (rev != _insightsRevision) {
    _insightsRevision = rev;
    if (d.is_object())
      [self rebuildProfiles:d];
    _mapCanvas.needsDisplay = YES;
  }
  NSString *n = @"Unknown cells have no observation.";
  if (d.is_object()) {
    if (auto p = heatmap::projection_notice(d))
      n = ns(*p);
    else if (auto c = heatmap::cold_start_message(d))
      n = ns(*c);
    else if (d.contains("note") && d["note"].is_string())
      n = ns(d["note"].get<std::string>());
    else if (d.contains("learning_count"))
      n = [NSString stringWithFormat:@"Local learning · %@ decisions",
                                     ns(d["learning_count"].dump())];
  }
  const auto &spec = heatmap::layer_spec(_layer);
  NSString *legend =
      [NSString stringWithFormat:@"%@ · %@", ns(std::string(spec.label)),
                                 ns(std::string(spec.meaning))];
  if (n.length && ![n isEqualToString:@"Unknown cells have no observation."])
    legend = [NSString stringWithFormat:@"%@  ·  %@", n, legend];
  Json e = w.value("error", Json(nullptr));
  NSString *notice = e.is_string() ? ns(e.get<std::string>()) : legend;
  if (![_lastMapNotice isEqualToString:notice]) {
    _lastMapNotice = [notice copy];
    _mapNotice.stringValue = notice;
  }
}
- (void)rebuildProfiles:(const Json &)d {
  auto choices = heatmap::profile_choices(d);
  [_profiles removeAllItems];
  for (const auto &c : choices) {
    [_profiles addItemWithTitle:ns(c.label)];
    _profiles.lastItem.representedObject = ns(c.key);
  }
  auto selected = heatmap::resolve_profile_key(d, _profile);
  if (selected) {
    _profile = *selected;
    for (NSMenuItem *i in _profiles.itemArray)
      if ([i.representedObject isEqualToString:ns(*selected)]) {
        [_profiles selectItem:i];
        break;
      }
  }
}
- (void)drawMap {
  NSRect b = _mapCanvas.bounds;
  [[NSColor colorWithCalibratedRed:.067 green:.082 blue:.11 alpha:1] setFill];
  NSRectFill(b);
  NSRect map = NSInsetRect(b, 58, 42);
  Json w = _app->insights_snapshot(), d = w.value("data", Json(nullptr));
  if (!heatmap::bridge_data_matches_selection(d, _profile))
    d = Json::object();
  auto [cols, rows] = heatmap::grid_dimensions(d);
  CGFloat cw = NSWidth(map) / cols, ch = NSHeight(map) / rows;
  const std::string key = _layer.empty() ? "activity" : _layer;
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++) {
      size_t ix = (size_t)(r * cols + c);
      auto value = heatmap::layer_value(d, key, ix);
      NSRect cell = NSMakeRect(NSMinX(map) + c * cw, NSMaxY(map) - (r + 1) * ch,
                               std::max<CGFloat>(1, cw - 1),
                               std::max<CGFloat>(1, ch - 1));
      [color(heatmap::layer_color(key, value ? Json(*value) : Json(nullptr)))
          setFill];
      NSRectFill(cell);
    }
  [[NSColor colorWithWhite:.7 alpha:.4] setStroke];
  [[NSBezierPath bezierPathWithRect:map] stroke];
  auto outline = [&](const char *k, NSColor *c) {
    if (!d.contains(k))
      return;
    auto q = heatmap::normalize_rect_json(d[k]);
    if (!q)
      return;
    [c setStroke];
    NSBezierPath *p = [NSBezierPath bezierPathWithRect:map_rect(*q, map)];
    p.lineWidth = 2;
    [p stroke];
  };
  outline("foreground_rect", [NSColor colorWithCalibratedRed:.71
                                                       green:.58
                                                        blue:1
                                                       alpha:1]);
  outline("pip_rect", [NSColor colorWithCalibratedRed:.94
                                                green:.63
                                                 blue:.36
                                                alpha:1]);
  outline("recommendation_rect", [NSColor colorWithCalibratedRed:.45
                                                           green:.86
                                                            blue:.77
                                                           alpha:1]);
  if (_hoverColumn >= 0 && _hoverRow >= 0) {
    auto size = heatmap::snapshot_pip_size(d);
    auto f = size ? heatmap::candidate_footprint(
                        _hoverColumn, _hoverRow, cols, rows,
                        Json::array({size->first, size->second}))
                  : std::nullopt;
    if (f) {
      [[NSColor whiteColor] setStroke];
      NSBezierPath *p = [NSBezierPath bezierPathWithRect:map_rect(*f, map)];
      p.lineWidth = 2;
      [p stroke];
    }
  }
}
- (void)mapMoved:(NSPoint)p {
  NSRect area = NSInsetRect(_mapCanvas.bounds, 58, 42);
  if (!NSPointInRect(p, area)) {
    _hoverColumn = _hoverRow = -1;
  } else {
    Json d = _app->insights_snapshot().value("data", Json(nullptr));
    if (!heatmap::bridge_data_matches_selection(d, _profile))
      d = Json::object();
    auto [c, r] = heatmap::grid_dimensions(d);
    _hoverColumn =
        std::clamp((int)((p.x - area.origin.x) * c / NSWidth(area)), 0, c - 1);
    _hoverRow =
        std::clamp((int)((NSMaxY(area) - p.y) * r / NSHeight(area)), 0, r - 1);
  }
  _mapCanvas.needsDisplay = YES;
}
- (void)profileChanged:(id)sender {
  (void)sender;
  NSString *k = _profiles.selectedItem.representedObject;
  if (k.length)
    _profile = k.UTF8String;
  else
    _profile.reset();
  _insightsRevision = -1;
  _app->request_insights(_profile);
}
- (void)layerChanged:(id)sender {
  (void)sender;
  NSInteger x = _layers.selectedSegment;
  _layer = x == 1 ? "manual" : x == 2 ? "placement" : "activity";
  _mapCanvas.needsDisplay = YES;
}
- (void)donate:(id)sender {
  NSMenu *m = [[NSMenu alloc] init];
  NSArray *n = @[ @"€2", @"€5", @"€10", @"Choose amount…" ],
          *u = @[ @"/2EUR", @"/5EUR", @"/10EUR", @"" ];
  for (NSUInteger x = 0; x < n.count; x++) {
    NSMenuItem *i = [[NSMenuItem alloc] initWithTitle:n[x]
                                               action:@selector(donateItem:)
                                        keyEquivalent:@""];
    i.target = self;
    i.representedObject = u[x];
    [m addItem:i];
  }
  [m popUpMenuPositioningItem:nil
                   atLocation:NSMakePoint(0,
                                          NSHeight(((NSButton *)sender).bounds))
                       inView:sender];
}
- (void)donateItem:(NSMenuItem *)i {
  if ([i.title isEqualToString:@"Choose amount…"]) {
    NSAlert *a = [[NSAlert alloc] init];
    a.messageText = @"Choose a donation amount";
    a.informativeText =
        @"PayPal opens in your browser; DuckEggs does not process payments.";
    [a addButtonWithTitle:@"Open PayPal"];
    [a addButtonWithTitle:@"Cancel"];
    NSTextField *f =
        [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 180, 24)];
    f.placeholderString = @"Amount in EUR";
    a.accessoryView = f;
    if ([a runModal] == NSAlertFirstButtonReturn &&
        std::isfinite(f.doubleValue) && f.doubleValue >= 1 &&
        f.doubleValue <= 5000)
      [NSWorkspace.sharedWorkspace
          openURL:[NSURL URLWithString:[NSString stringWithFormat:
                                                     @"https://www.paypal.me/"
                                                     @"GiacomoDiCiocco/%.2fEUR",
                                                     f.doubleValue]]];
    return;
  }
  [NSWorkspace.sharedWorkspace
      openURL:[NSURL URLWithString:
                         [@"https://www.paypal.me/GiacomoDiCiocco"
                             stringByAppendingString:i.representedObject]]];
}
- (void)permissions:(id)sender {
  (void)sender;
  NSAlert *a = [[NSAlert alloc] init];
  a.messageText = @"DuckEggs permissions";
  a.informativeText = @"Accessibility is required to move the selected window. "
                      @"Screen Recording is optional for visual learning. "
                      @"DuckEggs never requests permissions automatically.";
  [a addButtonWithTitle:@"Grant Accessibility"];
  [a addButtonWithTitle:@"Enable visual learning"];
  [a addButtonWithTitle:@"Accessibility Settings"];
  [a addButtonWithTitle:@"Screen Recording Settings"];
  [a addButtonWithTitle:@"Close"];
  NSModalResponse r = [a runModal];
  if (r == NSAlertFirstButtonReturn) {
    NSDictionary *o =
        @{(__bridge NSString *)kAXTrustedCheckOptionPrompt : @YES};
    (void)AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)o);
  } else if (r == NSAlertSecondButtonReturn)
    (void)CGRequestScreenCaptureAccess();
  else if (r == NSAlertThirdButtonReturn)
    [NSWorkspace.sharedWorkspace openURL:privacy_url(@"Accessibility")];
  else if (r == NSAlertThirdButtonReturn + 1)
    [NSWorkspace.sharedWorkspace openURL:privacy_url(@"ScreenCapture")];
}
- (void)startupChanged:(id)sender {
  (void)sender;
  NSControlStateValue before = _startup.state;
  try {
    [self setLogin:_startup.state == NSControlStateValueOn];
  } catch (const std::exception &e) {
    _startup.state = before == NSControlStateValueOn ? NSControlStateValueOff
                                                     : NSControlStateValueOn;
    NSString *s = [NSString stringWithUTF8String:e.what()];
    NSAlert *a = [[NSAlert alloc] init];
    a.messageText = @"Could not update Start with macOS";
    a.informativeText = s ? s : @"Login item update failed.";
    [a runModal];
  }
}
- (void)setLogin:(BOOL)enabled {
  NSString *p = login_path();
  if (enabled) {
    NSString *exe = NSBundle.mainBundle.executableURL.path;
    if (!exe.length)
      exe = NSProcessInfo.processInfo.arguments.firstObject;
    exe = exe.stringByStandardizingPath;
    if (!exe.length || ![exe hasPrefix:@"/"])
      throw std::runtime_error("Executable path is not absolute.");
    NSData *existing = [NSData dataWithContentsOfFile:p];
    if (existing) {
      NSError *readError = nil;
      id old = [NSPropertyListSerialization
          propertyListWithData:existing
                       options:NSPropertyListImmutable
                        format:nil
                         error:&readError];
      if (readError || ![old isKindOfClass:NSDictionary.class] ||
          ![old[@"Label"] isEqual:kLoginLabel])
        throw std::runtime_error(
            "Existing login item is not owned by DuckEggs.");
    }
    NSDictionary *d = @{
      @"Label" : kLoginLabel,
      @"ProgramArguments" : @[ exe ],
      @"RunAtLoad" : @YES
    };
    NSError *e = nil;
    NSData *b = [NSPropertyListSerialization
        dataWithPropertyList:d
                      format:NSPropertyListXMLFormat_v1_0
                     options:0
                       error:&e];
    if (!b)
      throw std::runtime_error("Could not serialize login item.");
    NSString *dir = p.stringByDeletingLastPathComponent;
    NSFileManager *f = NSFileManager.defaultManager;
    if (![f createDirectoryAtPath:dir
            withIntermediateDirectories:YES
                             attributes:@{NSFilePosixPermissions : @0700}
                                  error:&e])
      throw std::runtime_error("Could not create LaunchAgents.");
    if (![b writeToFile:p options:NSDataWritingAtomic error:&e])
      throw std::runtime_error("Could not write login item.");
    if (chmod(p.fileSystemRepresentation, 0600) != 0) {
      [f removeItemAtPath:p error:nil];
      throw std::runtime_error("Could not secure login item.");
    }
  } else {
    NSData *b = [NSData dataWithContentsOfFile:p];
    if (!b)
      return;
    NSError *e = nil;
    id d = [NSPropertyListSerialization
        propertyListWithData:b
                     options:NSPropertyListImmutable
                      format:nil
                       error:&e];
    if (e || ![d isKindOfClass:NSDictionary.class] ||
        ![d[@"Label"] isEqual:kLoginLabel])
      throw std::runtime_error("Existing login item is not owned by DuckEggs.");
    if (![NSFileManager.defaultManager removeItemAtPath:p error:&e])
      throw std::runtime_error("Could not remove login item.");
  }
}
- (void)hide:(id)sender {
  (void)sender;
  [_main orderOut:nil];
}
- (void)restore {
  [_main makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}
- (void)quit {
  [self closeMain];
}
- (void)closeMain {
  if (_closing)
    return;
  _closing = true;
  _app->request_quit();
  [_timer invalidate];
  if (_pickerRequested)
    [self closePicker:YES];
  if (_status) {
    [NSStatusBar.systemStatusBar removeStatusItem:_status];
    _status = nil;
  }
  [NSApp stop:nil];
  [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                      location:NSZeroPoint
                                 modifierFlags:0
                                     timestamp:0
                                  windowNumber:0
                                       context:nil
                                       subtype:0
                                         data1:0
                                         data2:0]
           atStart:NO];
}
- (BOOL)windowShouldClose:(NSWindow *)w {
  if (w == _main) {
    [self closeMain];
    return YES;
  }
  if (w == _maps) {
    _mapOpen = false;
    [_maps orderOut:nil];
    return NO;
  }
  return NO;
}
- (void)applicationDidResignActive:(NSNotification *)notification {
  (void)notification;
  if (_pickerRequested)
    [self closePicker:YES];
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender
                    hasVisibleWindows:(BOOL)flag {
  (void)sender;
  if (!flag || !_main.isVisible)
    [self restore];
  return YES;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)a {
  (void)a;
  return NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)a {
  (void)a;
  [self closeMain];
  return NSTerminateCancel;
}
@end

namespace schiva {
bool run_platform_ui(AdaptiveApplication &app) {
  @autoreleasepool {
    NSApplication *native = NSApplication.sharedApplication;
    __attribute__((objc_precise_lifetime)) DuckEggsUI *delegate =
        [[DuckEggsUI alloc] initWithApplication:&app];
    native.delegate = delegate;
    [native run];
    app.request_quit();
    return true;
  }
}
} // namespace schiva
