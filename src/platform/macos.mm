#include "schiva/platform.hpp"

#include "macos_shared.hpp"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <libproc.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#if __has_include(<ScreenCaptureKit/ScreenCaptureKit.h>)
#include <ScreenCaptureKit/ScreenCaptureKit.h>
#define DUCKEGGS_HAS_SCREEN_CAPTURE_KIT 1
#else
#define DUCKEGGS_HAS_SCREEN_CAPTURE_KIT 0
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace schiva::detail {
namespace {
std::mutex g_ui_mutex;
MacUiSnapshot g_ui_snapshot;
}  // namespace

void publish_macos_screens(std::vector<MacScreenGeometry> screens) {
    std::lock_guard lock(g_ui_mutex);
    const auto same_rect = [](const Rect& a, const Rect& b) {
        return a.left == b.left && a.top == b.top &&
               a.right == b.right && a.bottom == b.bottom;
    };
    const bool same = g_ui_snapshot.screens.size() == screens.size() &&
        std::equal(g_ui_snapshot.screens.begin(), g_ui_snapshot.screens.end(),
            screens.begin(), [&](const MacScreenGeometry& a, const MacScreenGeometry& b) {
                return a.display_id == b.display_id &&
                    same_rect(a.frame, b.frame) &&
                    same_rect(a.visible_frame, b.visible_frame);
            });
    if (same && g_ui_snapshot.screens_valid) return;
    g_ui_snapshot.screens = std::move(screens);
    g_ui_snapshot.screens_valid = !g_ui_snapshot.screens.empty();
    ++g_ui_snapshot.generation;
}

void clear_macos_screens() {
    std::lock_guard lock(g_ui_mutex);
    if (g_ui_snapshot.screens.empty() && !g_ui_snapshot.screens_valid) return;
    g_ui_snapshot.screens.clear();
    g_ui_snapshot.screens_valid = false;
    ++g_ui_snapshot.generation;
}

void publish_macos_picker_windows(std::vector<std::uint32_t> window_ids) {
    std::lock_guard lock(g_ui_mutex);
    if (g_ui_snapshot.picker_window_ids == window_ids) return;
    g_ui_snapshot.picker_window_ids = std::move(window_ids);
    ++g_ui_snapshot.generation;
}

MacUiSnapshot macos_ui_snapshot() {
    std::lock_guard lock(g_ui_mutex);
    return g_ui_snapshot;
}
}  // namespace schiva::detail

namespace schiva {
namespace {

constexpr int kCaptureWidth = 64;
constexpr int kCaptureHeight = 36;
constexpr int kInnerMargin = 20;
constexpr int kSafetyMargin = 28;
constexpr double kIdentityScanSeconds = 0.30;
constexpr double kMotionScanSeconds = 0.10;

template <typename T>
struct CFReleaseDeleter {
    void operator()(T value) const {
        if (value != nullptr) {
            CFRelease(value);
        }
    }
};

template <typename T>
using CFPtr = std::unique_ptr<std::remove_pointer_t<T>, CFReleaseDeleter<T>>;

template <typename T>
CFPtr<T> own_cf(T value) {
    return CFPtr<T>(value);
}

std::optional<std::wstring> wide_from_utf8(const std::string& value) {
    if (value.empty()) {
        return std::wstring{};
    }
    std::wstring result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size();) {
        const unsigned char first = static_cast<unsigned char>(value[i]);
        std::uint32_t code = 0;
        std::size_t count = 0;
        if (first < 0x80) {
            code = first;
            count = 1;
        } else if ((first & 0xe0U) == 0xc0U) {
            code = first & 0x1fU;
            count = 2;
        } else if ((first & 0xf0U) == 0xe0U) {
            code = first & 0x0fU;
            count = 3;
        } else if ((first & 0xf8U) == 0xf0U) {
            code = first & 0x07U;
            count = 4;
        } else {
            return std::nullopt;
        }
        if (i + count > value.size()) {
            return std::nullopt;
        }
        for (std::size_t j = 1; j < count; ++j) {
            const unsigned char next = static_cast<unsigned char>(value[i + j]);
            if ((next & 0xc0U) != 0x80U) {
                return std::nullopt;
            }
            code = (code << 6U) | (next & 0x3fU);
        }
        if ((count == 2 && code < 0x80) ||
            (count == 3 && code < 0x800) ||
            (count == 4 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) {
            return std::nullopt;
        }
        result.push_back(static_cast<wchar_t>(code));
        i += count;
    }
    return result;
}

std::string cf_string(CFStringRef value) {
    if (value == nullptr) {
        return {};
    }
    const CFIndex length = CFStringGetLength(value);
    const CFIndex maximum = CFStringGetMaximumSizeForEncoding(
        length, kCFStringEncodingUTF8);
    if (maximum < 0 || maximum > 16384) {
        return {};
    }
    std::string result(static_cast<std::size_t>(maximum) + 1U, '\0');
    if (!CFStringGetCString(
            value, result.data(), maximum + 1, kCFStringEncodingUTF8)) {
        return {};
    }
    result.resize(std::strlen(result.c_str()));
    return result;
}

std::string cf_attribute_string(AXUIElementRef element, CFStringRef name) {
    CFTypeRef raw = nullptr;
    if (AXUIElementCopyAttributeValue(element, name, &raw) != kAXErrorSuccess ||
        raw == nullptr || CFGetTypeID(raw) != CFStringGetTypeID()) {
        if (raw != nullptr) {
            CFRelease(raw);
        }
        return {};
    }
    const auto value = own_cf(static_cast<CFStringRef>(raw));
    return cf_string(value.get());
}

std::optional<CGRect> cg_rect_from_dictionary(CFDictionaryRef dictionary) {
    CGRect rect{};
    if (dictionary == nullptr ||
        !CGRectMakeWithDictionaryRepresentation(dictionary, &rect) ||
        !std::isfinite(rect.origin.x) || !std::isfinite(rect.origin.y) ||
        !std::isfinite(rect.size.width) || !std::isfinite(rect.size.height) ||
        rect.size.width <= 0 || rect.size.height <= 0) {
        return std::nullopt;
    }
    return rect;
}

Rect integer_rect(CGRect rect) {
    const CGRect integral = CGRectIntegral(rect);
    return {
        static_cast<int>(std::floor(integral.origin.x)),
        static_cast<int>(std::floor(integral.origin.y)),
        static_cast<int>(std::ceil(integral.origin.x + integral.size.width)),
        static_cast<int>(std::ceil(integral.origin.y + integral.size.height))};
}

bool rect_close(const Rect& first, const Rect& second, int tolerance = 2) {
    return motion_rect_matches(first, second, tolerance);
}

std::uint64_t process_start_time(pid_t pid) {
    proc_bsdinfo info{};
    const int result = proc_pidinfo(
        pid, PROC_PIDTBSDINFO, 0, &info, static_cast<int>(sizeof(info)));
    if (result != static_cast<int>(sizeof(info))) {
        return 0;
    }
    return static_cast<std::uint64_t>(info.pbi_start_tvsec) * 1000000ULL +
           static_cast<std::uint64_t>(info.pbi_start_tvusec);
}

std::optional<std::string> process_path(pid_t pid) {
    std::array<char, PROC_PIDPATHINFO_MAXSIZE> buffer{};
    const int size = proc_pidpath(pid, buffer.data(),
                                  static_cast<uint32_t>(buffer.size()));
    if (size <= 0 || size >= static_cast<int>(buffer.size())) {
        return std::nullopt;
    }
    return std::string(buffer.data(), static_cast<std::size_t>(size));
}

std::string bundle_identifier(pid_t pid) {
    const auto path = process_path(pid);
    if (!path.has_value()) {
        return {};
    }
    const std::string marker = ".app/Contents/";
    const auto offset = path->find(marker);
    if (offset == std::string::npos) {
        return {};
    }
    const std::string app_path = path->substr(0, offset + 4);
    auto url = own_cf(CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(app_path.data()),
        static_cast<CFIndex>(app_path.size()),
        true));
    if (!url) {
        return {};
    }
    auto bundle = own_cf(CFBundleCreate(kCFAllocatorDefault, url.get()));
    if (!bundle) {
        return {};
    }
    return cf_string(CFBundleGetIdentifier(bundle.get()));
}

bool ax_rect(AXUIElementRef element, Rect& output) {
    CFTypeRef position_raw = nullptr;
    CFTypeRef size_raw = nullptr;
    if (AXUIElementCopyAttributeValue(
            element, kAXPositionAttribute, &position_raw) != kAXErrorSuccess ||
        AXUIElementCopyAttributeValue(
            element, kAXSizeAttribute, &size_raw) != kAXErrorSuccess ||
        position_raw == nullptr || size_raw == nullptr) {
        if (position_raw != nullptr) CFRelease(position_raw);
        if (size_raw != nullptr) CFRelease(size_raw);
        return false;
    }
    const auto position = own_cf(static_cast<AXValueRef>(position_raw));
    const auto size = own_cf(static_cast<AXValueRef>(size_raw));
    if (CFGetTypeID(position.get()) != AXValueGetTypeID() ||
        CFGetTypeID(size.get()) != AXValueGetTypeID()) {
        return false;
    }
    CGPoint point{};
    CGSize dimensions{};
    if (!AXValueGetValue(position.get(), kAXValueTypeCGPoint, &point) ||
        !AXValueGetValue(size.get(), kAXValueTypeCGSize, &dimensions) ||
        !std::isfinite(point.x) || !std::isfinite(point.y) ||
        !std::isfinite(dimensions.width) || !std::isfinite(dimensions.height) ||
        dimensions.width < 1 || dimensions.height < 1) {
        return false;
    }
    output = integer_rect(CGRectMake(
        point.x, point.y, dimensions.width, dimensions.height));
    return output.width() > 0 && output.height() > 0;
}

bool ax_attribute_settable(AXUIElementRef element, CFStringRef attribute) {
    Boolean settable = false;
    return AXUIElementIsAttributeSettable(element, attribute, &settable) ==
               kAXErrorSuccess &&
           settable;
}

bool ax_full_screen(AXUIElementRef element) {
    CFTypeRef raw = nullptr;
    if (AXUIElementCopyAttributeValue(
            element, CFSTR("AXFullScreen"), &raw) != kAXErrorSuccess || raw == nullptr) {
        if (raw != nullptr) CFRelease(raw);
        return false;
    }
    const auto value = own_cf(raw);
    if (CFGetTypeID(value.get()) == CFBooleanGetTypeID()) {
        return CFBooleanGetValue(static_cast<CFBooleanRef>(value.get()));
    }
    return false;
}

struct AxHolder {
    explicit AxHolder(AXUIElementRef value) : element(value) {
        if (element != nullptr) CFRetain(element);
    }
    ~AxHolder() {
        if (element != nullptr) CFRelease(element);
    }
    AxHolder(const AxHolder&) = delete;
    AxHolder& operator=(const AxHolder&) = delete;
    AXUIElementRef element{};
};

struct Record {
    CGWindowID cg_id{};
    pid_t pid{};
    std::uint64_t start_time{};
    std::uint64_t token{};
    std::string app_id;
    std::string title;
    std::string role;
    int layer{};
    Rect rect;
    bool rect_valid{};
    bool on_screen{};
    bool ax_settable{};
    std::shared_ptr<AxHolder> ax;
};

struct CGRecord {
    CGWindowID cg_id{};
    pid_t pid{};
    std::string title;
    Rect rect;
    bool rect_valid{};
    bool on_screen{};
    CGFloat alpha{1.0};
    int layer{};
};

struct TokenEntry {
    pid_t pid{};
    std::uint64_t start_time{};
    std::shared_ptr<AxHolder> ax;
    std::uint64_t token{};
};

std::vector<CGRecord> copy_cg_records() {
    auto array = own_cf(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID));
    if (!array) {
        throw PlatformError("macOS could not enumerate visible windows");
    }
    std::vector<CGRecord> output;
    const CFIndex count = CFArrayGetCount(array.get());
    output.reserve(static_cast<std::size_t>(std::max<CFIndex>(0, count)));
    for (CFIndex index = 0; index < count; ++index) {
        auto dict = static_cast<CFDictionaryRef>(
            const_cast<void*>(CFArrayGetValueAtIndex(array.get(), index)));
        if (dict == nullptr) continue;
        CGRecord record;
        CFNumberRef number = static_cast<CFNumberRef>(
            CFDictionaryGetValue(dict, kCGWindowNumber));
        int raw_id = 0;
        if (number == nullptr || !CFNumberGetValue(
                number, kCFNumberIntType, &raw_id) || raw_id <= 0) {
            continue;
        }
        record.cg_id = static_cast<CGWindowID>(raw_id);
        number = static_cast<CFNumberRef>(
            CFDictionaryGetValue(dict, kCGWindowOwnerPID));
        int raw_pid = 0;
        if (number == nullptr || !CFNumberGetValue(
                number, kCFNumberIntType, &raw_pid) || raw_pid <= 0) {
            continue;
        }
        record.pid = static_cast<pid_t>(raw_pid);
        number = static_cast<CFNumberRef>(
            CFDictionaryGetValue(dict, kCGWindowLayer));
        if (number != nullptr) {
            (void)CFNumberGetValue(number, kCFNumberIntType, &record.layer);
        }
        CFNumberRef alpha = static_cast<CFNumberRef>(
            CFDictionaryGetValue(dict, kCGWindowAlpha));
        if (alpha != nullptr) {
            (void)CFNumberGetValue(alpha, kCFNumberCGFloatType, &record.alpha);
        }
        CFStringRef title = static_cast<CFStringRef>(
            CFDictionaryGetValue(dict, kCGWindowName));
        record.title = cf_string(title);
        record.on_screen = true;
        auto bounds = static_cast<CFDictionaryRef>(
            CFDictionaryGetValue(dict, kCGWindowBounds));
        if (const auto rect = cg_rect_from_dictionary(bounds)) {
            record.rect = integer_rect(*rect);
            record.rect_valid = true;
        }
        output.push_back(std::move(record));
    }
    return output;
}

std::vector<std::shared_ptr<AxHolder>> ax_windows(pid_t pid) {
    std::vector<std::shared_ptr<AxHolder>> output;
    auto app = own_cf(AXUIElementCreateApplication(pid));
    if (!app) return output;
    if (AXUIElementSetMessagingTimeout(app.get(), 0.05F) != kAXErrorSuccess) {
        return output;
    }
    CFTypeRef raw = nullptr;
    const AXError error = AXUIElementCopyAttributeValue(
        app.get(), kAXWindowsAttribute, &raw);
    if (error != kAXErrorSuccess || raw == nullptr ||
        CFGetTypeID(raw) != CFArrayGetTypeID()) {
        if (raw != nullptr) CFRelease(raw);
        return output;
    }
    const auto array = own_cf(static_cast<CFArrayRef>(raw));
    const CFIndex count = CFArrayGetCount(array.get());
    output.reserve(static_cast<std::size_t>(std::max<CFIndex>(0, count)));
    for (CFIndex index = 0; index < count; ++index) {
        auto element = static_cast<AXUIElementRef>(
            const_cast<void*>(CFArrayGetValueAtIndex(array.get(), index)));
        if (element != nullptr && CFGetTypeID(element) == AXUIElementGetTypeID()) {
            if (AXUIElementSetMessagingTimeout(element, 0.05F) == kAXErrorSuccess) {
                output.push_back(std::make_shared<AxHolder>(element));
            }
        }
    }
    return output;
}


std::optional<Rect> screen_work_area(
    const detail::MacUiSnapshot& ui,
    Point point,
    bool* from_ui = nullptr) {
    if (from_ui != nullptr) *from_ui = false;
    if (ui.screens_valid) {
        for (const auto& screen : ui.screens) {
            if (point.x >= screen.frame.left && point.x < screen.frame.right &&
                point.y >= screen.frame.top && point.y < screen.frame.bottom &&
                screen.visible_frame.width() > 0 &&
                screen.visible_frame.height() > 0) {
                if (from_ui != nullptr) *from_ui = true;
                return screen.visible_frame;
            }
        }
        // The UI cache can lag a display hot-plug. Fall back to current
        // CoreGraphics bounds for observation; move() still requires UI data.
    }
    uint32_t count = 0;
    if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess || count == 0) return std::nullopt;
    std::vector<CGDirectDisplayID> displays(count);
    if (CGGetActiveDisplayList(count, displays.data(), &count) != kCGErrorSuccess) return std::nullopt;
    for (const auto display : displays) {
        const Rect bounds = integer_rect(CGDisplayBounds(display));
        if (point.x >= bounds.left && point.x < bounds.right && point.y >= bounds.top && point.y < bounds.bottom) return bounds;
    }
    return std::nullopt;
}

std::optional<Rect> monitor_area(
    const detail::MacUiSnapshot& ui,
    Point point) {
    if (ui.screens_valid) {
        for (const auto& screen : ui.screens) {
            if (point.x >= screen.frame.left && point.x < screen.frame.right &&
                point.y >= screen.frame.top && point.y < screen.frame.bottom) {
                return screen.frame;
            }
        }
        // Current display bounds remain useful for observation/eligibility.
    }
    uint32_t count = 0;
    if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess || count == 0) return std::nullopt;
    std::vector<CGDirectDisplayID> displays(count);
    if (CGGetActiveDisplayList(count, displays.data(), &count) != kCGErrorSuccess) return std::nullopt;
    for (const auto display : displays) {
        const Rect bounds = integer_rect(CGDisplayBounds(display));
        if (point.x >= bounds.left && point.x < bounds.right && point.y >= bounds.top && point.y < bounds.bottom) return bounds;
    }
    return std::nullopt;
}

Point cursor_location() {
    auto event = own_cf(CGEventCreate(nullptr));
    if (!event) throw PlatformError("macOS pointer location is unavailable");
    const CGPoint location = CGEventGetLocation(event.get());
    if (!std::isfinite(location.x) || !std::isfinite(location.y)) {
        throw PlatformError("macOS returned an invalid pointer location");
    }
    return {static_cast<int>(std::lround(location.x)),
            static_cast<int>(std::lround(location.y))};
}

bool mouse_buttons_down() {
    for (int button = 0; button < 32; ++button) {
        if (CGEventSourceButtonState(
                kCGEventSourceStateCombinedSessionState,
                static_cast<CGMouseButton>(button))) {
            return true;
        }
    }
    return false;
}

std::optional<VisualFrame> capture_legacy(
    const Rect& bounds,
    const std::vector<Rect>& masks) {
    using CaptureFunction = CGImageRef (*)(
        CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
    auto function = reinterpret_cast<CaptureFunction>(
        dlsym(RTLD_DEFAULT, "CGWindowListCreateImage"));
    if (function == nullptr || bounds.width() <= 0 || bounds.height() <= 0) {
        return std::nullopt;
    }
    CGWindowImageOption options = kCGWindowImageDefault;
    CGImageRef raw = function(
        CGRectMake(bounds.left, bounds.top, bounds.width(), bounds.height()),
        kCGWindowListOptionOnScreenOnly,
        kCGNullWindowID,
        options);
    auto image = own_cf(raw);
    if (!image) return std::nullopt;
    const size_t width = kCaptureWidth;
    const size_t height = kCaptureHeight;
    std::vector<std::uint8_t> pixels(width * height, 0);
    auto color = own_cf(CGColorSpaceCreateDeviceGray());
    if (!color) return std::nullopt;
    CGContextRef context_raw = CGBitmapContextCreate(
        pixels.data(), width, height, 8, width, color.get(),
        kCGImageAlphaNone);
    auto context = own_cf(context_raw);
    if (!context) return std::nullopt;
    CGContextSetInterpolationQuality(context.get(), kCGInterpolationLow);
    CGContextTranslateCTM(context.get(), 0, height);
    CGContextScaleCTM(context.get(),
        static_cast<CGFloat>(width) / CGImageGetWidth(image.get()),
        -static_cast<CGFloat>(height) / CGImageGetHeight(image.get()));
    CGContextDrawImage(
        context.get(),
        CGRectMake(0, 0, CGImageGetWidth(image.get()), CGImageGetHeight(image.get())),
        image.get());
    std::vector<Rect> excluded;
    excluded.reserve(masks.size());
    for (const Rect& mask : masks) {
        if (const auto clipped = clipped_intersection(mask, bounds)) {
            excluded.push_back(*clipped);
        }
    }
    return VisualFrame{kCaptureWidth, kCaptureHeight, std::move(pixels),
                       bounds, std::move(excluded)};
}

#if DUCKEGGS_HAS_SCREEN_CAPTURE_KIT
template <typename T>
struct ObjCStrong {
    T value;
    explicit ObjCStrong(T object) : value(object) {}
};

template <typename T, typename Starter>
std::optional<T> wait_capture_result(Starter starter) {
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        bool done{};
        std::optional<T> value;
        std::string error;
    };
    auto state = std::make_shared<State>();
    starter([state](T value, std::string error) {
        {
            std::lock_guard lock(state->mutex);
            state->value = std::move(value);
            state->error = std::move(error);
            state->done = true;
        }
        state->cv.notify_one();
    });
    std::unique_lock lock(state->mutex);
    if (!state->cv.wait_for(lock, std::chrono::milliseconds(450), [&] {
            return state->done;
        })) {
        return std::nullopt;
    }
    return std::move(state->value);
}

std::atomic<bool> g_screenkit_request_pending{};

std::optional<VisualFrame> capture_screenkit(
    const Rect& bounds,
    const std::vector<Rect>& masks) API_AVAILABLE(macos(14.0)) {
    bool expected = false;
    if (!g_screenkit_request_pending.compare_exchange_strong(expected, true)) {
        return std::nullopt;
    }
    auto captured = wait_capture_result<CFPtr<CGImageRef>>(
        [bounds, masks](auto completion) {
            [SCShareableContent getShareableContentWithCompletionHandler:
                ^(SCShareableContent* value, NSError* error) {
                    if (error != nil || value == nil) {
                        g_screenkit_request_pending.store(false);
                        completion(CFPtr<CGImageRef>{}, error == nil ?
                            std::string("screen capture content is unavailable") :
                            std::string(([[error localizedDescription] UTF8String] != nullptr
                                ? [[error localizedDescription] UTF8String]
                                : "screen capture content is unavailable")));
                        return;
                    }
                    SCDisplay* selected = nil;
                    CGRect display_bounds = CGRectNull;
                    for (SCDisplay* display in value.displays) {
                        const Rect rect = integer_rect(CGDisplayBounds(display.displayID));
                        if (bounds.left >= rect.left && bounds.top >= rect.top &&
                            bounds.right <= rect.right && bounds.bottom <= rect.bottom) {
                            selected = display;
                            display_bounds = CGDisplayBounds(display.displayID);
                            break;
                        }
                    }
                    if (selected == nil) {
                        g_screenkit_request_pending.store(false);
                        completion(CFPtr<CGImageRef>{}, "capture bounds do not fit one display");
                        return;
                    }
                    NSMutableArray<SCWindow*>* excluded = [NSMutableArray array];
                    for (SCWindow* window in value.windows) {
                        const Rect rect = integer_rect(window.frame);
                        for (const Rect& mask : masks) {
                            if (rect_close(rect, mask, 2)) {
                                [excluded addObject:window];
                                break;
                            }
                        }
                    }
                    ObjCStrong<SCContentFilter*> filter([[SCContentFilter alloc]
                        initWithDisplay:selected excludingWindows:excluded]);
                    ObjCStrong<SCStreamConfiguration*> configuration(
                        [[SCStreamConfiguration alloc] init]);
                    configuration.value.width = kCaptureWidth;
                    configuration.value.height = kCaptureHeight;
                    configuration.value.showsCursor = NO;
                    configuration.value.sourceRect = CGRectMake(
                        bounds.left - display_bounds.origin.x,
                        bounds.top - display_bounds.origin.y,
                        bounds.width(), bounds.height());
                    [SCScreenshotManager captureImageWithFilter:filter.value
                        configuration:configuration.value
                        completionHandler:^(CGImageRef image, NSError* capture_error) {
                            CFPtr<CGImageRef> owned;
                            if (image != nullptr) {
                                CFRetain(image);
                                owned = own_cf(image);
                            }
                            g_screenkit_request_pending.store(false);
                            completion(std::move(owned), capture_error == nil ?
                                std::string{} :
                                std::string(([[capture_error localizedDescription] UTF8String] != nullptr
                                    ? [[capture_error localizedDescription] UTF8String]
                                    : "screen capture failed")));
                        }];
                }];
        });
    if (!captured || !*captured || !captured->get()) return std::nullopt;
    auto image = std::move(*captured);
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(kCaptureWidth * kCaptureHeight), 0);
    auto color = own_cf(CGColorSpaceCreateDeviceGray());
    if (!color) return std::nullopt;
    auto context = own_cf(CGBitmapContextCreate(
        pixels.data(), kCaptureWidth, kCaptureHeight, 8, kCaptureWidth,
        color.get(), kCGImageAlphaNone));
    if (!context) return std::nullopt;
    CGContextSetInterpolationQuality(context.get(), kCGInterpolationLow);
    CGContextTranslateCTM(context.get(), 0, kCaptureHeight);
    CGContextScaleCTM(context.get(), 1, -1);
    CGContextDrawImage(context.get(),
        CGRectMake(0, 0, kCaptureWidth, kCaptureHeight), image.get());
    std::vector<Rect> excluded_masks;
    for (const Rect& mask : masks) {
        if (const auto clipped = clipped_intersection(mask, bounds)) {
            excluded_masks.push_back(*clipped);
        }
    }
    return VisualFrame{kCaptureWidth, kCaptureHeight, std::move(pixels),
                       bounds, std::move(excluded_masks)};
}
#endif

std::optional<VisualFrame> capture_desktop(
    const Rect& bounds,
    const std::vector<Rect>& masks) {
#if DUCKEGGS_HAS_SCREEN_CAPTURE_KIT
    if (@available(macOS 14.0, *)) {
        return capture_screenkit(bounds, masks);
    }
#endif
    return capture_legacy(bounds, masks);
}

std::string socket_base_path() {
    const char* home = std::getenv("HOME");
    std::uint64_t hash = 1469598103934665603ULL;
    if (home != nullptr) {
        for (const unsigned char c : std::string(home)) {
            hash = (hash ^ c) * 1099511628211ULL;
        }
    }
    std::ostringstream out;
    out << "/tmp/duckeggs-" << static_cast<unsigned long>(getuid()) << '-'
        << std::hex << (hash & 0xffffffffULL);
    return out.str();
}

class MacRuntimeSignals final : public RuntimeSignals {
public:
    MacRuntimeSignals() {
        const std::string base = socket_base_path();
        lock_path_ = base + ".lock";
        socket_path_ = base + ".sock";
        lock_fd_ = open(lock_path_.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (lock_fd_ < 0) {
            error_ = "could not open the per-user instance guard";
            return;
        }
        struct stat lock_stat{};
        if (fstat(lock_fd_, &lock_stat) != 0 || !S_ISREG(lock_stat.st_mode) ||
            lock_stat.st_uid != getuid() || (lock_stat.st_mode & 0077) != 0) {
            error_ = "the per-user instance guard has unsafe ownership";
            close_fd(lock_fd_);
            return;
        }
        if (flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                already_running_ = true;
            } else {
                error_ = "could not acquire the per-user instance guard";
            }
            close_fd(lock_fd_);
            return;
        }
        owner_ = true;
        struct stat old_stat{};
        if (lstat(socket_path_.c_str(), &old_stat) == 0) {
            if (!S_ISSOCK(old_stat.st_mode) || old_stat.st_uid != getuid() ||
                unlink(socket_path_.c_str()) != 0) {
                error_ = "an unsafe stop socket already occupies the path";
                close();
                return;
            }
        } else if (errno != ENOENT) {
            error_ = "could not verify the per-user stop socket";
            close();
            return;
        }
        socket_fd_ = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) {
            error_ = "could not create the per-user stop socket";
            close();
            return;
        }
        const int socket_fd_flags = fcntl(socket_fd_, F_GETFD, 0);
        if (socket_fd_flags < 0 || fcntl(socket_fd_, F_SETFD, socket_fd_flags | FD_CLOEXEC) != 0) {
            error_ = "could not secure the per-user stop socket";
            close();
            return;
        }
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (socket_path_.size() >= sizeof(address.sun_path)) {
            error_ = "the per-user stop socket path is too long";
            close();
            return;
        }
        std::memcpy(address.sun_path, socket_path_.c_str(), socket_path_.size() + 1);
        if (bind(socket_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            chmod(socket_path_.c_str(), 0600) != 0) {
            error_ = "could not bind the per-user stop socket";
            close();
            return;
        }
        const int flags = fcntl(socket_fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) != 0 ||
            lstat(socket_path_.c_str(), &socket_stat_) != 0 ||
            !S_ISSOCK(socket_stat_.st_mode) || socket_stat_.st_uid != getuid()) {
            error_ = "could not secure the per-user stop socket";
            close();
            return;
        }
        ready_ = true;
    }

    ~MacRuntimeSignals() override { close(); }
    bool already_running() const override { return already_running_; }
    bool stop_requested() const override {
        if (!ready_ || !owner_) return false;
        char buffer[16]{};
        while (recv(socket_fd_, buffer, sizeof(buffer), MSG_DONTWAIT) > 0) {
            if (std::strncmp(buffer, "STOP", 4) == 0) requested_.store(true);
        }
        return requested_.load();
    }
    void sign_stop() override {
        if (owner_) {
            requested_.store(true);
            return;
        }
        struct stat value{};
        if (lstat(socket_path_.c_str(), &value) != 0 || !S_ISSOCK(value.st_mode) ||
            value.st_uid != getuid() || (value.st_mode & 0077) != 0) {
            return;
        }
        const int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd < 0) return;
        const int fd_flags = fcntl(fd, F_GETFD, 0);
        if (fd_flags < 0 || fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC) != 0) {
            ::close(fd);
            return;
        }
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (socket_path_.size() < sizeof(address.sun_path)) {
            std::memcpy(address.sun_path, socket_path_.c_str(), socket_path_.size() + 1);
            (void)sendto(fd, "STOP", 4, 0,
                         reinterpret_cast<sockaddr*>(&address), sizeof(address));
        }
        ::close(fd);
    }
    void close() override {
        if (closed_) return;
        closed_ = true;
        if (socket_fd_ >= 0) {
            ::close(socket_fd_);
            socket_fd_ = -1;
        }
        if (owner_) {
            struct stat current{};
            if (lstat(socket_path_.c_str(), &current) == 0 &&
                current.st_dev == socket_stat_.st_dev &&
                current.st_ino == socket_stat_.st_ino &&
                current.st_uid == getuid() && S_ISSOCK(current.st_mode)) {
                (void)unlink(socket_path_.c_str());
            }
        }
        if (lock_fd_ >= 0) {
            if (owner_) (void)flock(lock_fd_, LOCK_UN);
            close_fd(lock_fd_);
        }
        owner_ = false;
        ready_ = false;
    }
    const std::string& error() const { return error_; }

private:
    static void close_fd(int& fd) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
    std::string lock_path_;
    std::string socket_path_;
    std::string error_;
    int lock_fd_{-1};
    int socket_fd_{-1};
    struct stat socket_stat_{};
    bool owner_{};
    bool ready_{};
    bool already_running_{};
    mutable std::atomic<bool> requested_{};
    bool closed_{};
};

class MacBackend final : public Backend {
public:
    MacBackend()
        : capture_worker_([](const Rect& bounds, const std::vector<Rect>& masks) {
              if (!CGPreflightScreenCaptureAccess()) {
                  throw PlatformError("Screen Recording permission is not granted");
              }
              auto result = capture_desktop(bounds, masks);
              if (!result) throw PlatformError("screen capture is unavailable");
              return std::move(*result);
          }) {
        capture_worker_.resume();
    }
    ~MacBackend() override { close(); }

    void set_paused(bool paused) override {
        if (paused_ == paused) return;
        paused_ = paused;
        end_motion();
        invalidate_capture();
        if (paused_) capture_worker_.pause();
        else capture_worker_.resume();
        last_scan_at_ = -1.0e300;
    }

    ManualHoverSnapshot manual_hover(Point point) override {
        ManualHoverSnapshot result;
        result.supported = true;
        result.point = point;
        if (closed_) {
            result.error = "the macOS backend is closed";
            return result;
        }
        try {
            const auto target = manual_target_at(point);
            if (target.has_value()) {
                result.found = true;
                result.title = target->title.empty() ? "(Untitled window)" : target->title;
                if (result.title.size() > 180) result.title.resize(177), result.title += "...";
                result.rect = target->record.rect;
                result.eligible = target->eligible;
                result.reason = target->reason;
            }
        } catch (const std::exception& error) {
            result.error = error.what();
        }
        return result;
    }

    ManualTargetStatus manual_target_status() const override {
        ManualTargetStatus result;
        result.supported = true;
        result.mode = manual_mode_ ? ManualTargetMode::Manual : ManualTargetMode::Automatic;
        result.selected = manual_mode_ && manual_.has_value();
        if (result.selected) result.selected_id = id_for(*manual_);
        result.error = manual_error_;
        return result;
    }

    bool select_manual_at(Point point) override {
        if (closed_) {
            manual_error_ = "the macOS backend is closed";
            return false;
        }
        try {
            const auto target = manual_target_at(point);
            if (!target.has_value()) {
                manual_error_ = "No movable window is under the pointer.";
                return false;
            }
            if (!target->eligible) {
                manual_error_ = target->reason;
                return false;
            }
            if (!target->record.ax || !target->record.ax_settable) {
                manual_error_ = "Accessibility cannot safely move this window.";
                return false;
            }
            manual_ = target->record;
            manual_mode_ = true;
            manual_error_.reset();
            end_motion();
            invalidate_capture();
            return true;
        } catch (const std::exception& error) {
            manual_error_ = error.what();
            return false;
        }
    }

    bool reset_manual_target() override {
        if (closed_) return false;
        manual_.reset();
        manual_mode_ = false;
        manual_error_.reset();
        end_motion();
        invalidate_capture();
        return true;
    }

    bool python_instance_active() const override { return false; }
    void start_hotkeys(std::function<void()>, std::function<void()>) override {}
    void stop_hotkeys() override {}

    Observation observe(double now, bool include_visual) override {
        if (closed_) throw PlatformError("the macOS backend is closed");
        const auto snapshot = detail::macos_ui_snapshot();
        if (snapshot.generation != last_ui_generation_) {
            last_ui_generation_ = snapshot.generation;
            invalidate_capture();
        }
        auto records = records_for_observe(now);
        const Point cursor = cursor_location();
        const bool buttons = mouse_buttons_down();
        std::optional<Record> pip;
        if (manual_mode_) {
            if (manual_.has_value()) {
                const auto current = validate_identity(*manual_, true);
                if (current.has_value()) {
                    pip = *current;
                    manual_error_.reset();
                } else {
                    manual_.reset();
                    manual_error_ = "The selected window is no longer available; choose another window.";
                }
            }
        } else {
            std::vector<Record> matches;
            for (const auto& record : records) {
                if (record.app_id == "com.google.Chrome" && record.ax &&
                    record.ax_settable && record.on_screen &&
                    title_matches(wide_from_utf8(record.title).value_or(L""))) {
                    matches.push_back(record);
                }
            }
            if (matches.size() > 1) {
                if (!motion_) throw PlatformError(
                    "found multiple Chrome PiP windows; close extras so a unique target can be selected");
                end_motion();
                pip.reset();
            } else if (matches.size() == 1) {
                pip = matches.front();
            }
        }

        std::optional<Record> foreground;
        if (const auto focused_pid = focused_application_pid()) {
            for (const auto& record : records) {
                if (record.pid == *focused_pid && record.layer == 0 && record.on_screen) {
                    foreground = record;
                    break;
                }
            }
        }
        if (!foreground.has_value()) {
            for (const auto& record : records) {
                if (record.layer == 0 && record.pid != getpid() &&
                    (!pip.has_value() || record.cg_id != pip->cg_id)) {
                    foreground = record;
                    break;
                }
            }
        }
        Point anchor = cursor;
        if (pip.has_value()) anchor = {pip->rect.left + pip->rect.width()/2,
                                      pip->rect.top + pip->rect.height()/2};
        else if (foreground.has_value()) anchor = {
            foreground->rect.left + foreground->rect.width()/2,
            foreground->rect.top + foreground->rect.height()/2};
        const auto work = screen_work_area(snapshot, anchor);
        if (!work.has_value()) throw PlatformError("no display contains the active window");
        std::vector<WindowInfo> windows;
        std::vector<Rect> masks;
        std::vector<Rect> capture_masks;
        windows.reserve(records.size());
        for (const auto& record : records) {
            windows.push_back(window_info(record));
            if (record.pid == getpid()) masks.push_back(record.rect);
        }
        capture_masks = masks;
        if (pip.has_value()) capture_masks.push_back(pip->rect);
        std::optional<VisualFrame> visual;
        const std::optional<Rect> pip_rect = pip.has_value()
            ? std::optional<Rect>(pip->rect) : std::nullopt;
        const bool can_capture = include_visual && !paused_ && !motion_ &&
            !buttons && CGPreflightScreenCaptureAccess() &&
            visual_capture_eligible(include_visual, paused_, false, buttons,
                                    pip_rect, cursor);
        CaptureContext context;
        if (foreground.has_value()) {
            context.foreground_id = id_for(*foreground);
            context.foreground_rect = foreground->rect;
        }
        if (pip.has_value()) {
            context.pip_id = id_for(*pip);
            context.pip_rect = pip->rect;
        }
        context.work = *work;
        context.masks = masks;
        if (!capture_context_.has_value() || *capture_context_ != context) {
            capture_context_ = std::move(context);
            invalidate_capture();
        }
        if (can_capture) {
            capture_worker_.set_suppressed(false);
            const int generation = capture_generation_;
            if (auto frame = capture_worker_.poll(generation)) visual = std::move(frame);
            (void)capture_worker_.request(generation, *work, capture_masks);
        } else {
            capture_worker_.set_suppressed(true);
        }
        const double age = input_age(now, cursor, buttons);
        Observation result;
        result.cursor = cursor;
        result.buttons_down = buttons;
        if (pip.has_value()) result.pip = window_info(*pip);
        if (foreground.has_value()) result.foreground = window_info(*foreground);
        result.work_area = *work;
        result.windows = std::move(windows);
        result.visual_frame = std::move(visual);
        result.input_active = age <= 0.75;
        result.input_age = age;
        result.interaction_exclusions = std::move(masks);
        return result;
    }

    bool begin_motion(const std::string& pip_id) override {
        if (closed_ || paused_) {
            last_error_ = "movement cannot start while paused or closed";
            return false;
        }
        if (!AXIsProcessTrusted()) {
            last_error_ = "Accessibility permission is not granted";
            return false;
        }
        const auto target = find_last_id(pip_id);
        if (!target.has_value()) {
            last_error_ = "the target window is no longer available";
            return false;
        }
        const auto current = validate_identity(*target, true, !manual_mode_);
        if (!current.has_value() || !current->ax_settable) {
            last_error_ = "the target window identity or position permission changed";
            if (manual_mode_) {
                manual_.reset();
                manual_error_ = last_error_;
            }
            return false;
        }
        motion_ = *current;
        expected_rect_ = current->rect;
        invalidate_capture();
        return true;
    }

    void end_motion() override { motion_.reset(); }

    bool move(const std::string& pip_id, int left, int top) override {
        if (closed_ || paused_ || !AXIsProcessTrusted()) {
            last_error_ = "movement is unavailable while paused or without Accessibility permission";
            return false;
        }
        if (!motion_.has_value() && !begin_motion(pip_id)) return false;
        if (!motion_.has_value() || id_for(*motion_) != pip_id) {
            stop_motion("the target window changed before movement");
            return false;
        }
        const Record lease = *motion_;
        const auto current = validate_identity(lease, true, !manual_mode_);
        if (!current.has_value() || !current->ax_settable) {
            stop_motion("the target window identity could not be validated");
            if (manual_mode_) {
                manual_.reset();
                manual_error_ = last_error_;
            }
            return false;
        }
        if (!motion_rect_matches(expected_rect_, current->rect, 2)) {
            stop_motion("manual window movement cancelled the motion lease");
            return false;
        }
        const Point cursor = cursor_location();
        if (mouse_buttons_down()) {
            stop_motion("mouse-button hold cancelled movement");
            return false;
        }
        if (point_near_rect(cursor, current->rect, kInnerMargin)) {
            stop_motion("the pointer entered the window controls before movement");
            return false;
        }
        if (!segment_is_safe({current->rect.left, current->rect.top}, {left, top},
                {current->rect.width(), current->rect.height()}, cursor, kSafetyMargin)) {
            stop_motion("the pointer entered the window motion corridor");
            return false;
        }
        const auto ui = detail::macos_ui_snapshot();
        bool accurate_work_area = false;
        const Point center{left + current->rect.width()/2,
                           top + current->rect.height()/2};
        const auto work = screen_work_area(ui, center, &accurate_work_area);
        const auto monitor = monitor_area(ui, center);
        if (!work.has_value() || !monitor.has_value() || !accurate_work_area) {
            stop_motion("display work-area information is not available yet");
            return false;
        }
        if (left < work->left || top < work->top ||
            left + current->rect.width() > work->right ||
            top + current->rect.height() > work->bottom) {
            stop_motion("the requested move leaves the display work area");
            return false;
        }
        const auto latest = validate_identity(lease, true, !manual_mode_);
        const Point final_cursor = cursor_location();
        if (!latest.has_value()) {
            stop_motion("the target window identity changed before the move");
            if (manual_mode_) {
                manual_.reset();
                manual_error_ = last_error_;
            }
            return false;
        }
        if (!motion_rect_matches(current->rect, latest->rect, 2) ||
            mouse_buttons_down() || point_near_rect(final_cursor, latest->rect, kInnerMargin) ||
            !segment_is_safe({latest->rect.left, latest->rect.top}, {left, top},
                {latest->rect.width(), latest->rect.height()}, final_cursor, kSafetyMargin)) {
            stop_motion("movement safety changed before applying the position");
            return false;
        }
        CGPoint position = CGPointMake(left, top);
        auto value = own_cf(AXValueCreate(kAXValueTypeCGPoint, &position));
        if (!value || AXUIElementSetAttributeValue(
                latest->ax->element, kAXPositionAttribute, value.get()) != kAXErrorSuccess) {
            stop_motion("Accessibility could not move the selected window");
            return false;
        }
        expected_rect_ = {left, top, left + current->rect.width(), top + current->rect.height()};
        motion_->rect = expected_rect_;
        last_error_.reset();
        return true;
    }

    Json capabilities() const override {
        int candidate_count = 0;
        if (manual_mode_) {
            candidate_count = manual_.has_value() ? 1 : 0;
        } else {
            for (const auto& record : last_records_) {
                if (record.app_id == "com.google.Chrome" && record.ax &&
                    record.ax_settable && record.on_screen &&
                    title_matches(wide_from_utf8(record.title).value_or(L""))) {
                    ++candidate_count;
                }
            }
        }
        const auto capture_error = capture_worker_.capture_error(capture_generation_);
        Json result = {
            {"platform", "macos"},
            {"supported", true},
            {"backend", "Accessibility + CoreGraphics"},
            {"minimum_os", "macOS 10.15 Intel; macOS 11 Apple Silicon"},
            {"window_identity", "Chrome bundle ID + exact localized PiP title; retained Accessibility window"},
            {"manual_selection_supported", true},
            {"candidate_count", candidate_count},
            {"last_error", last_error_.has_value() ? Json(*last_error_) : Json(nullptr)},
            {"capture_error", capture_error.has_value() ? Json(*capture_error) : Json(nullptr)},
            {"accessibility_trusted", AXIsProcessTrusted()},
            {"screen_recording_authorized", CGPreflightScreenCaptureAccess()},
            {"visual_available", CGPreflightScreenCaptureAccess()},
            {"visual_capture", "ScreenCaptureKit on macOS 14+; guarded CoreGraphics fallback on 10.15-13"},
            {"external_topmost", false},
            {"hotkeys", "unavailable; no global keyboard hook is installed"},
            {"input_activity", "pointer and mouse buttons; keyboard activity is not observed"},
            {"caret", "unavailable"},
            {"notes", Json::array({
                "Accessibility permission is required for window movement and is never requested automatically.",
                "Screen Recording permission is required for visual learning and is never requested automatically.",
                "The backend cannot force third-party windows to remain above other applications.",
                "Display work areas must be published by the main-thread UI before movement is enabled."})}};
        return result;
    }

    Json probe() override {
        Json result = capabilities();
        result["capture_performed"] = false;
        result["window_enumeration"] = AXIsProcessTrusted() ? "available" : "limited: Accessibility permission is not granted";
        result["movement"] = AXIsProcessTrusted() ? "available after UI display geometry is published" : "disabled: Accessibility permission is not granted";
        return result;
    }

    void close() override {
        if (closed_) return;
        closed_ = true;
        end_motion();
        manual_.reset();
        capture_worker_.close();
        last_records_.clear();
    }

private:
    struct CaptureContext {
        std::string foreground_id;
        std::optional<Rect> foreground_rect;
        std::string pip_id;
        std::optional<Rect> pip_rect;
        Rect work;
        std::vector<Rect> masks;

        friend bool operator==(const CaptureContext& a, const CaptureContext& b) {
            const auto equal_rect = [](const std::optional<Rect>& x,
                                       const std::optional<Rect>& y) {
                if (x.has_value() != y.has_value()) return false;
                if (!x) return true;
                return x->left == y->left && x->top == y->top &&
                       x->right == y->right && x->bottom == y->bottom;
            };
            if (a.foreground_id != b.foreground_id ||
                !equal_rect(a.foreground_rect, b.foreground_rect) ||
                a.pip_id != b.pip_id || !equal_rect(a.pip_rect, b.pip_rect) ||
                a.work.left != b.work.left || a.work.top != b.work.top ||
                a.work.right != b.work.right || a.work.bottom != b.work.bottom ||
                a.masks.size() != b.masks.size()) return false;
            for (std::size_t i = 0; i < a.masks.size(); ++i) {
                if (a.masks[i].left != b.masks[i].left ||
                    a.masks[i].top != b.masks[i].top ||
                    a.masks[i].right != b.masks[i].right ||
                    a.masks[i].bottom != b.masks[i].bottom) return false;
            }
            return true;
        }
        friend bool operator!=(const CaptureContext& a, const CaptureContext& b) {
            return !(a == b);
        }
    };

    struct ManualTarget {
        Record record;
        std::string title;
        bool eligible{};
        std::string reason;
    };

    static WindowInfo window_info(const Record& record) {
        return {id_for(record), record.app_id, record.role, record.rect};
    }
    static std::string id_for(const Record& record) {
        return "mac-window:" + std::to_string(record.token);
    }

    std::optional<pid_t> focused_application_pid() const {
        auto system = own_cf(AXUIElementCreateSystemWide());
        if (!system) return std::nullopt;
        if (AXUIElementSetMessagingTimeout(system.get(), 0.05F) != kAXErrorSuccess) {
            return std::nullopt;
        }
        CFTypeRef raw = nullptr;
        if (AXUIElementCopyAttributeValue(
                system.get(), kAXFocusedApplicationAttribute, &raw) != kAXErrorSuccess ||
            raw == nullptr || CFGetTypeID(raw) != AXUIElementGetTypeID()) {
            if (raw != nullptr) CFRelease(raw);
            return std::nullopt;
        }
        const auto app = own_cf(static_cast<AXUIElementRef>(raw));
        pid_t pid = 0;
        if (AXUIElementGetPid(app.get(), &pid) != kAXErrorSuccess || pid <= 0) return std::nullopt;
        return pid;
    }

    std::vector<Record> snapshot_records() {
        auto cg = copy_cg_records();
        std::map<pid_t, std::vector<std::shared_ptr<AxHolder>>> ax_by_pid;
        std::map<pid_t, std::string> app_by_pid;
        std::map<pid_t, std::uint64_t> start_by_pid;
        if (AXIsProcessTrusted()) {
            for (const auto& item : cg) {
                if (item.pid == getpid()) continue;
                if (!ax_by_pid.contains(item.pid)) {
                    start_by_pid[item.pid] = process_start_time(item.pid);
                    app_by_pid[item.pid] = bundle_identifier(item.pid);
                    ax_by_pid[item.pid] = ax_windows(item.pid);
                }
            }
        }
        std::vector<Record> result;
        result.reserve(cg.size());
        std::map<CGWindowID, std::uint64_t> observed_ids;
        for (const auto& item : cg) {
            Record record;
            record.cg_id = item.cg_id;
            record.pid = item.pid;
            record.start_time = start_by_pid[item.pid];
            record.title = item.title;
            record.layer = item.layer;
            record.rect = item.rect;
            record.rect_valid = item.rect_valid;
            record.on_screen = item.on_screen && item.alpha > 0.01;
            record.app_id = app_by_pid[item.pid];
            record.role = "AXWindow";
            if (record.rect_valid && ax_by_pid.contains(item.pid)) {
                std::vector<std::shared_ptr<AxHolder>> matches;
                for (const auto& ax : ax_by_pid[item.pid]) {
                    Rect rect;
                    if (ax_rect(ax->element, rect) && rect_close(rect, record.rect)) {
                        matches.push_back(ax);
                    }
                }
                if (matches.size() == 1) {
                    record.ax = matches.front();
                    record.ax_settable = ax_attribute_settable(
                        record.ax->element, kAXPositionAttribute);
                    record.role = cf_attribute_string(record.ax->element, kAXRoleAttribute);
                    if (record.role.empty()) record.role = "AXWindow";
                    const std::string ax_title = cf_attribute_string(
                        record.ax->element, kAXTitleAttribute);
                    if (!ax_title.empty()) record.title = ax_title;
                }
            }
            const auto previous = token_by_cg_.find(item.cg_id);
            const bool same_ax = previous != token_by_cg_.end() &&
                previous->second.ax && record.ax &&
                CFEqual(previous->second.ax->element, record.ax->element);
            const bool both_unavailable = previous != token_by_cg_.end() &&
                !previous->second.ax && !record.ax &&
                previous->second.start_time == record.start_time;
            if (previous != token_by_cg_.end() && previous->second.pid == item.pid &&
                previous->second.start_time == record.start_time &&
                (same_ax || both_unavailable)) {
                record.token = previous->second.token;
            } else {
                record.token = ++next_token_;
            }
            observed_ids[item.cg_id] = record.token;
            token_by_cg_[item.cg_id] = TokenEntry{
                item.pid, record.start_time, record.ax, record.token};
            result.push_back(std::move(record));
        }
        for (auto it = token_by_cg_.begin(); it != token_by_cg_.end();) {
            if (!observed_ids.contains(it->first)) it = token_by_cg_.erase(it);
            else ++it;
        }
        return result;
    }

    std::vector<Record> records_for_observe(double now) {
        const double interval = motion_.has_value() ? kMotionScanSeconds : kIdentityScanSeconds;
        if (last_scan_at_ < 0 || now < last_scan_at_ || now - last_scan_at_ >= interval) {
            last_records_ = snapshot_records();
            last_scan_at_ = now;
        }
        return last_records_;
    }

    std::optional<Record> find_last_id(const std::string& id) const {
        for (const auto& record : last_records_) {
            if (record.ax && id_for(record) == id) return record;
        }
        if (manual_.has_value() && id_for(*manual_) == id) return manual_;
        return std::nullopt;
    }

    std::optional<Record> validate_identity(
        const Record& lease, bool require_settable, bool require_auto_pip = false) {
        if (!lease.ax || lease.pid <= 0 || lease.start_time == 0 ||
            !AXIsProcessTrusted() || process_start_time(lease.pid) != lease.start_time) {
            return std::nullopt;
        }
        std::optional<CGRecord> current_cg;
        for (const auto& current : copy_cg_records()) {
            if (current.cg_id == lease.cg_id && current.pid == lease.pid &&
                current.on_screen && current.alpha > 0.01 && current.rect_valid) {
                if (current_cg.has_value()) return std::nullopt;
                current_cg = current;
            }
        }
        if (!current_cg.has_value()) return std::nullopt;
        std::size_t rect_matches = 0;
        bool same_ax_present = false;
        for (const auto& current_ax : ax_windows(lease.pid)) {
            Rect current_rect;
            if (!ax_rect(current_ax->element, current_rect) ||
                !rect_close(current_rect, current_cg->rect)) continue;
            ++rect_matches;
            if (CFEqual(current_ax->element, lease.ax->element)) same_ax_present = true;
        }
        if (rect_matches != 1 || !same_ax_present ||
            !ax_rect(lease.ax->element, current_cg->rect) ||
            (require_settable && !ax_attribute_settable(
                lease.ax->element, kAXPositionAttribute))) {
            return std::nullopt;
        }
        if (require_auto_pip) {
            if (bundle_identifier(lease.pid) != "com.google.Chrome") return std::nullopt;
            const std::string current_title = cf_attribute_string(
                lease.ax->element, kAXTitleAttribute);
            if (!title_matches(wide_from_utf8(current_title).value_or(L""))) {
                return std::nullopt;
            }
        }
        const Point center{current_cg->rect.left + current_cg->rect.width()/2,
                           current_cg->rect.top + current_cg->rect.height()/2};
        const auto ui = detail::macos_ui_snapshot();
        const auto monitor = monitor_area(ui, center);
        const auto work = screen_work_area(ui, center);
        if (!monitor || !work || !manual_window_eligibility(
                current_cg->rect, *monitor, *work, ax_full_screen(lease.ax->element)).eligible) {
            return std::nullopt;
        }
        Record validated = lease;
        validated.rect = current_cg->rect;
        validated.rect_valid = true;
        validated.on_screen = true;
        validated.ax_settable = ax_attribute_settable(
            lease.ax->element, kAXPositionAttribute);
        return validated;
    }

    std::optional<ManualTarget> manual_target_at(Point point) {
        const auto ui = detail::macos_ui_snapshot();
        const auto cg = copy_cg_records();
        std::vector<ManualWindowHit> hit_entries;
        hit_entries.reserve(cg.size());
        for (const auto& item : cg) {
            ManualWindowHit entry;
            entry.visible = item.on_screen && item.alpha > 0.01;
            // The on-screen CGWindowList already omits desktop elements. Treat
            // every visible window in its front-to-back list as a blocker,
            // including utility/panel layers that may cover a target.
            entry.top_level = true;
            entry.rect_valid = item.rect_valid;
            entry.rect = item.rect;
            entry.own_process = item.pid == getpid();
            entry.picker_overlay = item.pid == getpid() && std::find(
                ui.picker_window_ids.begin(), ui.picker_window_ids.end(),
                item.cg_id) != ui.picker_window_ids.end();
            hit_entries.push_back(entry);
        }
        const auto hit = manual_hit_test(hit_entries, point);
        if (!hit.has_value()) return std::nullopt;
        const auto& item = cg[*hit];
        if (item.pid == getpid()) {
            ManualTarget target;
            target.title = item.title;
            target.record.cg_id = item.cg_id;
            target.record.pid = item.pid;
            target.record.rect = item.rect;
            target.record.rect_valid = item.rect_valid;
            target.reason = "DuckEggs windows cannot be selected.";
            return target;
        }
        auto records = snapshot_records();
        auto found = std::find_if(records.begin(), records.end(), [&](const Record& record) {
            return record.cg_id == item.cg_id && record.pid == item.pid && record.ax;
        });
        if (found == records.end()) {
            ManualTarget target;
            target.title = item.title;
            target.record.cg_id = item.cg_id;
            target.record.pid = item.pid;
            target.record.rect = item.rect;
            target.record.rect_valid = item.rect_valid;
            target.eligible = false;
            target.reason = AXIsProcessTrusted()
                ? "Accessibility could not uniquely identify this window."
                : "Accessibility permission is required to move windows.";
            return target;
        }
        if (found->rect_valid && found->ax) {
            const Point center{found->rect.left + found->rect.width()/2,
                               found->rect.top + found->rect.height()/2};
            const auto work = screen_work_area(ui, center);
            const auto monitor = monitor_area(ui, center);
            if (work && monitor) {
                const auto eligibility = manual_window_eligibility(
                    found->rect, *monitor, *work,
                    ax_full_screen(found->ax->element));
                if (!eligibility.eligible) {
                    ManualTarget target;
                    target.record = *found;
                    target.title = found->title;
                    target.reason = eligibility.reason;
                    return target;
                }
            }
        }
        const auto validated = validate_identity(*found, true);
        if (!validated.has_value() || point.x < validated->rect.left ||
            point.x >= validated->rect.right || point.y < validated->rect.top ||
            point.y >= validated->rect.bottom) {
            ManualTarget target;
            target.record = *found;
            target.title = found->title;
            target.reason = "The window changed before it could be selected.";
            return target;
        }
        // Resolve the click again from a fresh front-to-back list. If another
        // window moved over the point, never fall through to the old target.
        const auto fresh_cg = copy_cg_records();
        std::vector<ManualWindowHit> fresh_hits;
        fresh_hits.reserve(fresh_cg.size());
        for (const auto& current : fresh_cg) {
            ManualWindowHit entry;
            entry.visible = current.on_screen && current.alpha > 0.01;
            entry.top_level = true;
            entry.rect_valid = current.rect_valid;
            entry.rect = current.rect;
            entry.own_process = current.pid == getpid();
            entry.picker_overlay = current.pid == getpid() && std::find(
                ui.picker_window_ids.begin(), ui.picker_window_ids.end(),
                current.cg_id) != ui.picker_window_ids.end();
            fresh_hits.push_back(entry);
        }
        const auto fresh_hit = manual_hit_test(fresh_hits, point);
        if (!fresh_hit || fresh_cg[*fresh_hit].cg_id != item.cg_id ||
            fresh_cg[*fresh_hit].pid != item.pid) {
            ManualTarget target;
            target.record = *found;
            target.title = found->title;
            target.reason = "The window under the pointer changed before selection.";
            return target;
        }
        const Point center{validated->rect.left + validated->rect.width()/2,
                           validated->rect.top + validated->rect.height()/2};
        const auto work = screen_work_area(ui, center);
        const auto monitor = monitor_area(ui, center);
        ManualTarget target;
        target.record = *validated;
        target.title = found->title;
        if (!work || !monitor) {
            target.reason = "Display geometry is not available.";
            return target;
        }
        const auto eligibility = manual_window_eligibility(
            validated->rect, *monitor, *work, ax_full_screen(validated->ax->element));
        target.eligible = eligibility.eligible && validated->ax_settable;
        target.reason = eligibility.eligible
            ? (validated->ax_settable ? std::string{} : "Accessibility cannot move this window.")
            : eligibility.reason;
        return target;
    }

    double input_age(double now, Point cursor, bool buttons) {
        if (!last_input_time_.has_value() || !last_cursor_.has_value() ||
            cursor != *last_cursor_ || buttons != last_buttons_) {
            last_input_time_ = now;
            last_cursor_ = cursor;
            last_buttons_ = buttons;
            return 0.0;
        }
        return std::max(0.0, now - *last_input_time_);
    }

    void stop_motion(std::string message) {
        end_motion();
        last_error_ = std::move(message);
    }

    void invalidate_capture() {
        ++capture_generation_;
        capture_worker_.invalidate(capture_generation_);
    }

    mutable std::optional<Record> manual_;
    bool manual_mode_{};
    mutable std::optional<std::string> manual_error_;
    bool paused_{};
    bool closed_{};
    std::optional<Record> motion_;
    Rect expected_rect_{};
    std::vector<Record> last_records_;
    double last_scan_at_{-1.0e300};
    std::map<CGWindowID, TokenEntry> token_by_cg_;
    std::uint64_t next_token_{};
    std::uint64_t last_ui_generation_{};
    std::optional<CaptureContext> capture_context_;
    std::optional<double> last_input_time_;
    std::optional<Point> last_cursor_;
    bool last_buttons_{};
    int capture_generation_{};
    std::optional<std::string> last_error_;
    VisualCaptureWorker capture_worker_;
};

}  // namespace

std::unique_ptr<Backend> create_backend() {
    return std::make_unique<MacBackend>();
}

bool python_reference_active() { return false; }

std::unique_ptr<RuntimeSignals> create_runtime_signals() {
    auto result = std::make_unique<MacRuntimeSignals>();
    if (!result->already_running() && !result->error().empty()) throw PlatformError(result->error());
    return result;
}

bool request_stop() {
    MacRuntimeSignals signals;
    if (signals.already_running()) {
        signals.sign_stop();
        return true;
    }
    signals.sign_stop();
    return false;
}

}  // namespace schiva
