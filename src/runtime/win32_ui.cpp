#include "ui.hpp"

#ifdef _WIN32

#include "brand_mark.hpp"
#include "schiva/heatmap.hpp"
#include "schiva/runtime.hpp"

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace schiva {
namespace {

using namespace Gdiplus;

constexpr wchar_t kPanelClass[] = L"DuckEggsControlPanel";
constexpr wchar_t kHeatmapClass[] = L"DuckEggsLearningMaps";
constexpr int kPauseButton = 1001;
constexpr int kLearningButton = 1002;
constexpr int kExitButton = 1003;
constexpr int kDonateButton = 1004;
constexpr int kTrayButton = 1005;
constexpr int kProfileCombo = 1101;
constexpr int kActivityLayer = 1111;
constexpr int kManualLayer = 1112;
constexpr int kPlacementLayer = 1113;
constexpr int kDonateTwoEuros = 1201;
constexpr int kDonateFiveEuros = 1202;
constexpr int kDonateTenEuros = 1203;
constexpr int kDonateOther = 1204;
constexpr int kTrayOpen = 1301;
constexpr int kTrayPauseToggle = 1302;
constexpr int kTrayExit = 1303;
constexpr UINT kPanelTimer = 1;
constexpr UINT kHeatmapTimer = 2;
constexpr UINT kHoverMessage = WM_APP + 41;
constexpr UINT kTrayIconMessage = WM_APP + 42;
constexpr UINT kTrayIconId = 1;
constexpr UINT_PTR kHoverSubclassId = 1;

constexpr COLORREF kBackgroundGdi = RGB(17, 21, 28);
constexpr COLORREF kSurfaceGdi = RGB(27, 34, 45);
constexpr COLORREF kTextGdi = RGB(232, 238, 245);
constexpr COLORREF kSecondaryGdi = RGB(163, 174, 190);
constexpr COLORREF kAccentGdi = RGB(115, 220, 197);
constexpr COLORREF kErrorGdi = RGB(255, 145, 137);
constexpr COLORREF kWarmGdi = RGB(244, 201, 105);

constexpr ARGB kBackground = 0xff11151c;
constexpr ARGB kSurface = 0xff1b222d;
constexpr ARGB kSurfaceRaised = 0xff202936;
constexpr ARGB kBorder = 0xff2d3948;
constexpr ARGB kBorderStrong = 0xff3b4a5c;
constexpr ARGB kAccent = 0xff73dcc5;
constexpr ARGB kAccentSoft = 0x3d73dcc5;
constexpr ARGB kText = 0xffe8eef5;
constexpr ARGB kSecondary = 0xffa3aebe;
constexpr ARGB kError = 0xffff9189;
constexpr ARGB kErrorSoft = 0x24ff9189;
constexpr ARGB kWarm = 0xfff4c969;
constexpr ARGB kWarmSoft = 0x24f4c969;
constexpr ARGB kForegroundOutline = 0xffb594ff;
constexpr ARGB kPipOutline = 0xfff0a15c;
constexpr ARGB kRecommendationOutline = 0xff73dcc5;
constexpr ARGB kFootprintOutline = 0xffe8eef5;

constexpr wchar_t kDonationUrl[] =
    L"https://www.paypal.me/GiacomoDiCiocco";
constexpr wchar_t kMainMotto[] =
    L"I was walking past a farm and a sign said, \u201cDuck, eggs.\u201d\n"
    L"I thought, \u201cThat\u2019s an unnecessary comma\u2026\" "
    L"\u2013 and then it hit me.";
constexpr wchar_t kHoverProperty[] = L"DuckEggs.ButtonHover";

std::wstring wide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (size <= 0) {
        return L"";
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size);
    return result;
}

std::optional<std::string> string_field(
    const Json& object,
    const char* key) {
    if (!object.is_object() || !object.contains(key) ||
        !object[key].is_string()) {
        return std::nullopt;
    }
    return object[key].get<std::string>();
}

bool bool_field(const Json& object, const char* key, bool fallback = false) {
    return object.is_object() && object.contains(key) &&
                   object[key].is_boolean()
               ? object[key].get<bool>()
               : fallback;
}

int integer_field(const Json& object, const char* key, int fallback = 0) {
    return object.is_object() && object.contains(key) &&
                   object[key].is_number_integer()
               ? object[key].get<int>()
               : fallback;
}

std::wstring learning_summary(const Json& snapshot) {
    int decisions = integer_field(snapshot, "decision_count");
    if (snapshot.contains("selected_profile") &&
        snapshot["selected_profile"].is_string() &&
        snapshot.contains("profiles") &&
        snapshot["profiles"].is_array()) {
        const std::string selected =
            snapshot["selected_profile"].get<std::string>();
        for (const Json& profile : snapshot["profiles"]) {
            if (profile.is_object() &&
                profile.contains("profile_key") &&
                profile["profile_key"].is_string() &&
                profile["profile_key"].get<std::string>() == selected) {
                decisions =
                    integer_field(profile, "decision_count", decisions);
                break;
            }
        }
    }
    const int learning =
        integer_field(snapshot, "learning_count");
    return L"Profile decisions: " + std::to_wstring(decisions) +
           L"  \u00b7  Shared learning updates: " +
           std::to_wstring(learning);
}

std::wstring clean_wide(std::string_view value, std::size_t limit = 360) {
    std::string clean;
    clean.reserve(std::min(value.size(), limit));
    bool space = false;
    for (const char character : value) {
        const bool whitespace =
            character == ' ' || character == '\n' ||
            character == '\r' || character == '\t';
        if (whitespace) {
            if (!clean.empty()) {
                space = true;
            }
        } else {
            if (space) {
                clean.push_back(' ');
                space = false;
            }
            clean.push_back(character);
            if (clean.size() >= limit) {
                break;
            }
        }
    }
    return wide(clean);
}

std::wstring actionable_error(std::string_view error) {
    constexpr std::string_view resume_prefix = "resume blocked:";
    if (error.substr(0, resume_prefix.size()) == resume_prefix) {
        return L"Resume is unavailable while the original Schiva app is "
               L"running. Close it before resuming DuckEggs.";
    }
    constexpr std::string_view instance_prefix =
        "another Schiva Adattivo C++ instance";
    if (error.substr(0, instance_prefix.size()) == instance_prefix) {
        return L"Another DuckEggs session is already running.";
    }
    constexpr std::string_view maps_prefix =
        "Impossibile aggiornare le mappe";
    if (error.substr(0, maps_prefix.size()) == maps_prefix) {
        return L"Learning maps could not be refreshed. They will retry "
               L"automatically.";
    }
    return clean_wide(error);
}

Color color(ARGB value) {
    return Color(value);
}

Color with_alpha(ARGB value, BYTE alpha) {
    return Color(
        alpha,
        static_cast<BYTE>((value >> 16U) & 0xffU),
        static_cast<BYTE>((value >> 8U) & 0xffU),
        static_cast<BYTE>(value & 0xffU));
}

RectF rectangle(const RECT& value) {
    return RectF(
        static_cast<REAL>(value.left),
        static_cast<REAL>(value.top),
        static_cast<REAL>(std::max<LONG>(0, value.right - value.left)),
        static_cast<REAL>(std::max<LONG>(0, value.bottom - value.top)));
}

void add_round_rect(
    GraphicsPath& path,
    const RectF& bounds,
    REAL radius) {
    const REAL diameter = std::min(
        radius * 2.0F, std::min(bounds.Width, bounds.Height));
    const REAL arc = diameter / 2.0F;
    path.Reset();
    path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(
        bounds.GetRight() - diameter,
        bounds.Y,
        diameter,
        diameter,
        270.0F,
        90.0F);
    path.AddArc(
        bounds.GetRight() - diameter,
        bounds.GetBottom() - diameter,
        diameter,
        diameter,
        0.0F,
        90.0F);
    path.AddArc(
        bounds.X,
        bounds.GetBottom() - diameter,
        diameter,
        diameter,
        90.0F,
        90.0F);
    path.CloseFigure();
    (void)arc;
}

void fill_round_rect(
    Graphics& graphics,
    const RectF& bounds,
    REAL radius,
    Color fill) {
    GraphicsPath path;
    add_round_rect(path, bounds, radius);
    SolidBrush brush(fill);
    graphics.FillPath(&brush, &path);
}

void stroke_round_rect(
    Graphics& graphics,
    const RectF& bounds,
    REAL radius,
    Color stroke,
    REAL width = 1.0F) {
    GraphicsPath path;
    add_round_rect(path, bounds, radius);
    Pen pen(stroke, width);
    graphics.DrawPath(&pen, &path);
}

void draw_text(
    HDC device,
    std::wstring_view text,
    RECT rect,
    COLORREF color,
    HFONT font,
    UINT format = DT_LEFT | DT_TOP | DT_SINGLELINE) {
    SetBkMode(device, TRANSPARENT);
    SetTextColor(device, color);
    const HGDIOBJ old = SelectObject(device, font);
    DrawTextW(
        device,
        text.data(),
        static_cast<int>(text.size()),
        &rect,
        format | DT_NOPREFIX);
    SelectObject(device, old);
}

int text_width(HDC device, std::wstring_view text, HFONT font) {
    const HGDIOBJ old = SelectObject(device, font);
    SIZE size{};
    GetTextExtentPoint32W(
        device,
        text.data(),
        static_cast<int>(text.size()),
        &size);
    SelectObject(device, old);
    return size.cx;
}

int wrapped_text_height(
    HDC device,
    std::wstring_view text,
    int width,
    HFONT font,
    UINT format = DT_LEFT | DT_TOP | DT_WORDBREAK) {
    RECT measure{0, 0, std::max(1, width), 0};
    const HGDIOBJ old = SelectObject(device, font);
    DrawTextW(
        device,
        text.data(),
        static_cast<int>(text.size()),
        &measure,
        format | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(device, old);
    return measure.bottom - measure.top;
}

bool move_child_if_needed(
    HWND parent,
    HWND child,
    int x,
    int y,
    int width,
    int height) {
    if (child == nullptr) {
        return false;
    }
    POINT target_points[2]{{x, y}, {x + width, y + height}};
    MapWindowPoints(parent, nullptr, target_points, 2);
    const RECT target{
        target_points[0].x,
        target_points[0].y,
        target_points[1].x,
        target_points[1].y};
    RECT current{};
    if (GetWindowRect(child, &current) != FALSE &&
        EqualRect(&current, &target) != FALSE) {
        return false;
    }
    MoveWindow(child, x, y, width, height, TRUE);
    return true;
}

template <typename Draw>
void draw_buffered(
    HDC target,
    const RECT& bounds,
    Draw&& draw) {
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (target == nullptr || width <= 0 || height <= 0) {
        return;
    }
    HDC buffer = CreateCompatibleDC(target);
    if (buffer == nullptr) {
        draw(target, bounds);
        return;
    }
    HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    if (bitmap == nullptr) {
        DeleteDC(buffer);
        draw(target, bounds);
        return;
    }
    const HGDIOBJ previous = SelectObject(buffer, bitmap);
    if (previous == nullptr || previous == HGDI_ERROR) {
        DeleteObject(bitmap);
        DeleteDC(buffer);
        draw(target, bounds);
        return;
    }
    const RECT local{0, 0, width, height};
    draw(buffer, local);
    BitBlt(
        target,
        bounds.left,
        bounds.top,
        width,
        height,
        buffer,
        0,
        0,
        SRCCOPY);
    SelectObject(buffer, previous);
    DeleteObject(bitmap);
    DeleteDC(buffer);
}

HFONT make_font(UINT dpi, int logical_pixels, int weight) {
    return CreateFontW(
        -MulDiv(logical_pixels, static_cast<int>(dpi), 96),
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
}

UINT dpi_for_window(HWND window) {
    const UINT dpi = GetDpiForWindow(window);
    return dpi == 0 ? 96 : dpi;
}

UINT system_dpi() {
    HDC device = GetDC(nullptr);
    if (device == nullptr) {
        return 96;
    }
    const UINT dpi = static_cast<UINT>(
        GetDeviceCaps(device, LOGPIXELSX));
    ReleaseDC(nullptr, device);
    return dpi == 0 ? 96 : dpi;
}

void enable_per_monitor_dpi() {
    using SetDpiContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }
    const auto set_context = reinterpret_cast<SetDpiContext>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (set_context != nullptr) {
        (void)set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

void apply_window_chrome(HWND window) {
    const BOOL dark = TRUE;
    constexpr DWORD kDarkModeAttribute = 20;
    constexpr DWORD kCornerPreferenceAttribute = 33;
    constexpr DWORD kBorderColorAttribute = 34;
    constexpr DWORD kCaptionColorAttribute = 35;
    constexpr DWORD kTextColorAttribute = 36;
    constexpr DWORD kRoundCorner = 2;
    const COLORREF border = kBackgroundGdi;
    const COLORREF caption = kBackgroundGdi;
    const COLORREF text = kTextGdi;
    (void)DwmSetWindowAttribute(
        window,
        kDarkModeAttribute,
        &dark,
        sizeof(dark));
    (void)DwmSetWindowAttribute(
        window,
        kCornerPreferenceAttribute,
        &kRoundCorner,
        sizeof(kRoundCorner));
    (void)DwmSetWindowAttribute(
        window,
        kBorderColorAttribute,
        &border,
        sizeof(border));
    (void)DwmSetWindowAttribute(
        window,
        kCaptionColorAttribute,
        &caption,
        sizeof(caption));
    (void)DwmSetWindowAttribute(
        window,
        kTextColorAttribute,
        &text,
        sizeof(text));
    (void)SetWindowTheme(window, L"DarkMode_Explorer", nullptr);
}

void draw_egg_mark(
    Graphics& graphics,
    const RectF& bounds,
    REAL stroke_width) {
    ui::brand::draw_mark(graphics, bounds, stroke_width);
}

void draw_status_pill(
    Graphics& graphics,
    const RectF& bounds,
    ARGB tint) {
    fill_round_rect(
        graphics,
        bounds,
        bounds.Height / 2.0F,
        with_alpha(tint, 38));
    SolidBrush dot(color(tint));
    graphics.FillEllipse(
        &dot,
        bounds.X + bounds.Height * 0.30F,
        bounds.Y + bounds.Height * 0.39F,
        bounds.Height * 0.22F,
        bounds.Height * 0.22F);
}

void draw_dashed_rect(
    Graphics& graphics,
    const RectF& bounds,
    Color stroke,
    REAL width = 1.6F) {
    Pen pen(stroke, width);
    pen.SetDashStyle(DashStyleDash);
    graphics.DrawRectangle(&pen, bounds);
}

void draw_solid_rect(
    Graphics& graphics,
    const RectF& bounds,
    Color stroke,
    REAL width = 1.8F) {
    Pen pen(stroke, width);
    graphics.DrawRectangle(&pen, bounds);
}

RECT normalized_rect(
    const heatmap::NormalizedRect& value,
    const RECT& grid) {
    const double width =
        static_cast<double>(grid.right - grid.left);
    const double height =
        static_cast<double>(grid.bottom - grid.top);
    return RECT{
        grid.left + static_cast<LONG>(std::lround(value.left * width)),
        grid.top + static_cast<LONG>(std::lround(value.top * height)),
        grid.left + static_cast<LONG>(std::lround(value.right * width)),
        grid.top + static_cast<LONG>(std::lround(value.bottom * height))};
}

LRESULT CALLBACK button_hover_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR,
    DWORD_PTR) {
    if (message == WM_ERASEBKGND) {
        return 1;
    }
    if (message == WM_MOUSEMOVE) {
        if (GetPropW(window, kHoverProperty) == nullptr) {
            TRACKMOUSEEVENT track{
                sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
            SetPropW(
                window,
                kHoverProperty,
                reinterpret_cast<HANDLE>(
                    static_cast<ULONG_PTR>(1)));
            SendMessageW(
                GetParent(window),
                kHoverMessage,
                static_cast<WPARAM>(GetDlgCtrlID(window)),
                1);
        }
    } else if (message == WM_MOUSELEAVE) {
        RemovePropW(window, kHoverProperty);
        SendMessageW(
            GetParent(window),
            kHoverMessage,
            static_cast<WPARAM>(GetDlgCtrlID(window)),
            0);
    } else if (message == WM_NCDESTROY) {
        RemovePropW(window, kHoverProperty);
        RemoveWindowSubclass(
            window,
            &button_hover_proc,
            kHoverSubclassId);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

void install_hover(HWND button) {
    SetWindowSubclass(
        button,
        &button_hover_proc,
        kHoverSubclassId,
        0);
}

struct MainView {
    bool paused{true};
    bool observe_only{};
    std::wstring badge{L"Starting"};
    ARGB badge_color{kAccent};
    std::wstring pip_value{L"Checking"};
    std::wstring pip_detail{L"Looking for a Chrome PiP window."};
    std::wstring learning_value{L"0"};
    std::wstring learning_detail{L"Shared learning updates."};
    std::wstring visual_value{L"Waiting"};
    std::wstring visual_detail{L"Waiting for a usable visual signal."};
    std::wstring error;

    bool operator==(const MainView&) const = default;
};

class HeatmapWindow;

class ControlPanel {
public:
    ControlPanel(HWND window, AdaptiveApplication& application)
        : window_(window), application_(application) {
        dpi_ = dpi_for_window(window_);
        taskbar_created_message_ =
            RegisterWindowMessageW(L"TaskbarCreated");
        create_fonts();
        create_controls();
        SetTimer(window_, kPanelTimer, 200, nullptr);
        refresh();
        InvalidateRect(window_, nullptr, FALSE);
    }

    ~ControlPanel();

    void heatmap_destroyed(HWND window);

    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam);

private:
    int px(int logical) const {
        return MulDiv(logical, static_cast<int>(dpi_), 96);
    }

    REAL rpx(int logical) const {
        return static_cast<REAL>(px(logical));
    }

    REAL rpx(REAL logical) const {
        return logical * static_cast<REAL>(dpi_) / 96.0F;
    }

    void create_fonts() {
        destroy_fonts();
        heading_font_ = make_font(dpi_, 26, FW_SEMIBOLD);
        title_font_ = make_font(dpi_, 16, FW_SEMIBOLD);
        body_font_ = make_font(dpi_, 13, FW_NORMAL);
        semibold_font_ = make_font(dpi_, 13, FW_SEMIBOLD);
        small_font_ = make_font(dpi_, 12, FW_NORMAL);
        badge_font_ = make_font(dpi_, 12, FW_SEMIBOLD);
        for (HWND button :
             {pause_button_, learning_button_, donate_button_,
              tray_button_, exit_button_}) {
            if (button != nullptr) {
                SendMessageW(
                    button,
                    WM_SETFONT,
                    reinterpret_cast<WPARAM>(semibold_font_),
                    TRUE);
            }
        }
        if (window_ != nullptr) {
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void destroy_fonts() {
        std::array<HFONT*, 6> fonts{
            &heading_font_,
            &title_font_,
            &body_font_,
            &semibold_font_,
            &small_font_,
            &badge_font_};
        for (HFONT* font : fonts) {
            if (*font != nullptr) {
                DeleteObject(*font);
                *font = nullptr;
            }
        }
    }

    HWND make_button(
        int identifier,
        const wchar_t* text) {
        HWND button = CreateWindowExW(
            0,
            L"BUTTON",
            text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(identifier)),
            GetModuleHandleW(nullptr),
            nullptr);
        if (button != nullptr) {
            SendMessageW(
                button,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(semibold_font_),
                TRUE);
            install_hover(button);
        }
        return button;
    }

    void create_controls() {
        pause_button_ = make_button(kPauseButton, L"Resume");
        learning_button_ =
            make_button(kLearningButton, L"Learning maps");
        donate_button_ = make_button(kDonateButton, L"Donate");
        tray_button_ = make_button(kTrayButton, L"To tray");
        exit_button_ = make_button(kExitButton, L"Exit");
        background_brush_ = CreateSolidBrush(kBackgroundGdi);
        layout_controls();
    }

    void layout_controls() {
        if (window_ == nullptr) {
            return;
        }
        RECT client{};
        GetClientRect(window_, &client);
        const int margin = px(16);
        const int gap = px(10);
        const int height = px(34);
        const int top = client.bottom - px(14) - height;
        const int pause_width = px(118);
        bool changed = move_child_if_needed(
            window_,
            pause_button_,
            margin,
            top,
            pause_width,
            height);
        int right = client.right - margin;
        const int exit_width = px(58);
        changed |= move_child_if_needed(
            window_,
            exit_button_,
            right - exit_width,
            top,
            exit_width,
            height);
        right -= exit_width + gap;
        const int donate_width = px(86);
        changed |= move_child_if_needed(
            window_,
            donate_button_,
            right - donate_width,
            top,
            donate_width,
            height);
        right -= donate_width + gap;
        const int learning_width = px(128);
        changed |= move_child_if_needed(
            window_,
            learning_button_,
            right - learning_width,
            top,
            learning_width,
            height);
        changed |= move_child_if_needed(
            window_,
            tray_button_,
            client.right - margin - px(78),
            px(12),
            px(78),
            px(30));
        if (changed) {
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    MainView current_view() const {
        const Json status = application_.snapshot();
        MainView view;
        view.paused = bool_field(status, "paused", true);
        view.observe_only = bool_field(status, "observe_only");
        const std::string state =
            string_field(status, "state").value_or("starting");
        const int candidates = integer_field(status, "candidate_count");
        const bool moving = bool_field(status, "motion_active") ||
                            state == "moving";
        const bool visual = bool_field(status, "visual_available");
        const bool capture_error =
            status.contains("capture_error") &&
            !status["capture_error"].is_null();
        const int learning = integer_field(status, "learning_count");

        if (view.paused) {
            view.badge = L"Paused";
            view.badge_color = kSecondary;
        } else if (moving) {
            view.badge = L"Moving";
            view.badge_color = kWarm;
        } else if (state == "no_pip" || candidates == 0) {
            view.badge = L"No PiP";
            view.badge_color = kSecondary;
        } else if (state == "starting") {
            view.badge = L"Starting";
            view.badge_color = kSecondary;
        } else if (state == "error") {
            view.badge = L"Needs attention";
            view.badge_color = kError;
        } else {
            view.badge = L"Watching";
            view.badge_color = kAccent;
        }

        if (candidates > 1) {
            view.pip_value = L"Multiple windows";
            view.pip_detail = L"Close all but one Chrome PiP window.";
        } else if (moving) {
            view.pip_value = L"Moving safely";
            view.pip_detail = L"Easing toward a clear destination.";
        } else if (candidates == 1) {
            view.pip_value =
                view.paused ? L"Ready" : L"Connected";
            view.pip_detail =
                view.paused ? L"Resume when you are ready."
                            : L"DuckEggs is watching for a safe position.";
        } else {
            view.pip_value = L"Not found";
            view.pip_detail = L"Open Picture-in-Picture in Chrome.";
        }

        view.learning_value = std::to_wstring(learning);
        view.learning_detail =
            learning == 1 ? L"Shared learning update."
                          : L"Shared learning updates.";
        if (view.observe_only) {
            view.learning_detail = L"Observe only; the PiP will not move.";
        }
        if (capture_error) {
            view.visual_value = L"Needs access";
            view.visual_detail =
                L"Learning continues when a visual signal returns.";
        } else if (visual) {
            view.visual_value = L"Learning";
            view.visual_detail = L"Visual activity is available.";
        } else {
            view.visual_value = L"Waiting";
            view.visual_detail =
                L"Waiting for a usable visual signal.";
        }

        if (status.contains("last_error") &&
            status["last_error"].is_string()) {
            view.error =
                actionable_error(status["last_error"].get<std::string>());
        }
        return view;
    }

    void refresh() {
        MainView next = current_view();
        if (next == view_) {
            return;
        }
        const bool pause_changed = next.paused != view_.paused;
        view_ = std::move(next);
        if (pause_changed) {
            SetWindowTextW(
                pause_button_, view_.paused ? L"Resume" : L"Pause");
        }
        InvalidateRect(window_, nullptr, FALSE);
    }

    void open_heatmap();

    void open_donation_url(int command) const {
        std::wstring selected_url(kDonationUrl);
        switch (command) {
        case kDonateTwoEuros:
            selected_url += L"/2EUR";
            break;
        case kDonateFiveEuros:
            selected_url += L"/5EUR";
            break;
        case kDonateTenEuros:
            selected_url += L"/10EUR";
            break;
        case kDonateOther:
            break;
        default:
            return;
        }
        const HINSTANCE result = ShellExecuteW(
            window_,
            L"open",
            selected_url.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            const std::wstring message =
                L"DuckEggs could not open its PayPal donation page in the "
                L"default browser.\n\nSelected URL: " +
                selected_url +
                L"\n\nYou can copy the link into a browser.";
            MessageBoxW(
                window_,
                message.c_str(),
                L"DuckEggs donation",
                MB_OK | MB_ICONERROR);
        }
    }

    void open_donation() {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) {
            return;
        }
        AppendMenuW(menu, MF_STRING, kDonateTwoEuros, L"\u20ac2");
        AppendMenuW(menu, MF_STRING, kDonateFiveEuros, L"\u20ac5");
        AppendMenuW(menu, MF_STRING, kDonateTenEuros, L"\u20ac10");
        AppendMenuW(menu, MF_STRING, kDonateOther, L"Other amount");

        RECT anchor{};
        if (GetWindowRect(donate_button_, &anchor) == FALSE) {
            POINT cursor{};
            GetCursorPos(&cursor);
            anchor = RECT{cursor.x, cursor.y, cursor.x, cursor.y};
        }
        SetForegroundWindow(window_);
        const UINT selected = TrackPopupMenu(
            menu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON |
                TPM_BOTTOMALIGN | TPM_LEFTALIGN,
            anchor.left,
            anchor.bottom,
            0,
            window_,
            nullptr);
        DestroyMenu(menu);
        PostMessageW(window_, WM_NULL, 0, 0);
        if (selected != 0) {
            open_donation_url(static_cast<int>(selected));
        }
    }

    void remove_tray_icon() {
        if (!tray_icon_added_) {
            return;
        }
        NOTIFYICONDATAW icon{};
        icon.cbSize = sizeof(icon);
        icon.hWnd = window_;
        icon.uID = kTrayIconId;
        (void)Shell_NotifyIconW(NIM_DELETE, &icon);
        tray_icon_added_ = false;
    }

    bool add_tray_icon() {
        remove_tray_icon();
        NOTIFYICONDATAW icon{};
        icon.cbSize = sizeof(icon);
        icon.hWnd = window_;
        icon.uID = kTrayIconId;
        icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        icon.uCallbackMessage = kTrayIconMessage;
        icon.hIcon = reinterpret_cast<HICON>(LoadImageW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(ui::brand::kAppIconResourceId),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR | LR_SHARED));
        if (icon.hIcon == nullptr) {
            return false;
        }
        lstrcpynW(
            icon.szTip,
            L"DuckEggs",
            static_cast<int>(std::size(icon.szTip)));
        if (Shell_NotifyIconW(NIM_ADD, &icon) == FALSE) {
            return false;
        }
        tray_icon_added_ = true;
        icon.uVersion = NOTIFYICON_VERSION_4;
        if (Shell_NotifyIconW(NIM_SETVERSION, &icon) == FALSE) {
            remove_tray_icon();
            return false;
        }
        return true;
    }

    void hide_to_tray();

    void restore_from_tray(bool re_add_failed = false);

    void show_tray_menu(int x, int y) {
        refresh();
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr) {
            return;
        }
        AppendMenuW(menu, MF_STRING, kTrayOpen, L"Open DuckEggs");
        AppendMenuW(
            menu,
            MF_STRING,
            kTrayPauseToggle,
            view_.paused ? L"Resume" : L"Pause");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kTrayExit, L"Exit");
        SetForegroundWindow(window_);
        const UINT selected = TrackPopupMenu(
            menu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON |
                TPM_LEFTALIGN | TPM_TOPALIGN,
            x,
            y,
            0,
            window_,
            nullptr);
        DestroyMenu(menu);
        PostMessageW(window_, WM_NULL, 0, 0);
        switch (selected) {
        case kTrayOpen:
            restore_from_tray();
            break;
        case kTrayPauseToggle:
            application_.request_pause();
            break;
        case kTrayExit:
            application_.request_quit();
            DestroyWindow(window_);
            break;
        default:
            break;
        }
    }

    void handle_tray_message(WPARAM wparam, LPARAM lparam) {
        const UINT event = LOWORD(lparam);
        const UINT icon_id = HIWORD(lparam);
        if (icon_id != kTrayIconId) {
            return;
        }
        if (event == NIN_SELECT || event == NIN_KEYSELECT) {
            restore_from_tray();
        } else if (event == WM_CONTEXTMENU) {
            int x = GET_X_LPARAM(static_cast<LPARAM>(wparam));
            int y = GET_Y_LPARAM(static_cast<LPARAM>(wparam));
            if (x == -1 && y == -1) {
                POINT cursor{};
                GetCursorPos(&cursor);
                x = cursor.x;
                y = cursor.y;
            }
            show_tray_menu(x, y);
        }
    }

    void paint_main(HDC device) {
        RECT client{};
        GetClientRect(window_, &client);
        if (client.right <= 0 || client.bottom <= 0) {
            return;
        }
        Graphics graphics(device);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        graphics.Clear(color(kBackground));

        const REAL margin = rpx(16);
        const RectF egg{
            margin,
            rpx(10),
            rpx(42),
            rpx(42)};
        draw_egg_mark(graphics, egg, rpx(1.8F));

        HDC measure = device;
        const int badge_width = std::max(
            px(84),
            text_width(measure, view_.badge, badge_font_) + px(38));
        const RectF badge{
            static_cast<REAL>(client.right) - margin -
                rpx(78) - rpx(10) -
                static_cast<REAL>(badge_width),
            rpx(16),
            static_cast<REAL>(badge_width),
            rpx(26)};
        draw_status_pill(graphics, badge, view_.badge_color);
        graphics.Flush();

        const REAL footer_top =
            static_cast<REAL>(client.bottom) - rpx(14) - rpx(34);
        const REAL motto_width =
            std::max<REAL>(0, client.right - margin * 2.0F);
        const int motto_text_width =
            std::max(1, static_cast<int>(motto_width) - px(24));
        const int motto_text_height = wrapped_text_height(
            device,
            kMainMotto,
            motto_text_width,
            small_font_,
            DT_CENTER | DT_TOP | DT_WORDBREAK);
        const REAL motto_height = std::clamp(
            static_cast<REAL>(motto_text_height) + rpx(8),
            rpx(40),
            rpx(54));
        const RectF motto{
            margin,
            footer_top - rpx(6) - motto_height,
            motto_width,
            motto_height};
        const REAL metric_bottom = motto.Y - rpx(6);
        REAL metric_top = rpx(90);
        if (!view_.error.empty()) {
            const REAL banner_top = rpx(61);
            const REAL banner_width =
                std::max<REAL>(0, client.right - margin * 2.0F);
            const std::wstring error_message =
                L"Needs attention: " + view_.error;
            const int text_width_available =
                std::max(1, static_cast<int>(banner_width) - px(28));
            const int measured_height = wrapped_text_height(
                device,
                error_message,
                text_width_available,
                small_font_,
                DT_LEFT | DT_TOP | DT_WORDBREAK);
            const REAL maximum_height = std::min(
                rpx(90),
                std::max<REAL>(
                    0,
                    metric_bottom - rpx(72) - rpx(6) -
                        banner_top));
            const REAL minimum_height =
                std::min(rpx(38), maximum_height);
            const REAL banner_height = std::clamp(
                static_cast<REAL>(measured_height) + rpx(16),
                minimum_height,
                maximum_height);
            const RectF banner{
                margin,
                banner_top,
                banner_width,
                banner_height};
            fill_round_rect(
                graphics, banner, rpx(11), color(kErrorSoft));
            stroke_round_rect(
                graphics,
                banner,
                rpx(11),
                with_alpha(kError, 95));
            graphics.Flush();
            RECT error_text{
                static_cast<LONG>(banner.X + rpx(14)),
                static_cast<LONG>(banner.Y + rpx(6)),
                static_cast<LONG>(banner.GetRight() - rpx(14)),
                static_cast<LONG>(banner.GetBottom() - rpx(6))};
            draw_text(
                device,
                error_message,
                error_text,
                kErrorGdi,
                small_font_,
                DT_LEFT | DT_TOP | DT_WORDBREAK);
            metric_top = banner.GetBottom() + rpx(6);
        }

        const REAL metric_gap = rpx(8);
        const REAL metric_width =
            std::max<REAL>(
                0,
                (client.right - margin * 2.0F -
                 metric_gap * 2.0F) /
                    3.0F);
        const REAL metric_height =
            std::max<REAL>(0, metric_bottom - metric_top);
        for (int index = 0; index < 3; ++index) {
            const RectF metric{
                margin + index * (metric_width + metric_gap),
                metric_top,
                metric_width,
                metric_height};
            fill_round_rect(
                graphics, metric, rpx(12), color(kSurfaceRaised));
            stroke_round_rect(
                graphics, metric, rpx(12), color(kBorder));
            const ARGB dot_color =
                index == 0 ? view_.badge_color
                           : index == 1 ? kAccent : kWarm;
            SolidBrush dot(color(dot_color));
            graphics.FillEllipse(
                &dot,
                metric.X + rpx(11),
                metric.Y + rpx(7),
                rpx(6),
                rpx(6));
        }
        fill_round_rect(
            graphics, motto, rpx(10), color(kSurface));
        stroke_round_rect(
            graphics, motto, rpx(10), color(kBorder));
        graphics.Flush();

        RECT title{
            client.left + px(68),
            px(8),
            static_cast<LONG>(badge.X) - px(10),
            px(40)};
        draw_text(
            device,
            L"DuckEggs",
            title,
            kTextGdi,
            heading_font_);
        RECT subtitle{
            client.left + px(70),
            px(39),
            static_cast<LONG>(badge.X) - px(10),
            px(59)};
        draw_text(
            device,
            L"Adaptive Picture-in-Picture placement",
            subtitle,
            kSecondaryGdi,
            small_font_);

        const RectF badge_text_area = badge;
        RECT badge_text{
            static_cast<LONG>(badge_text_area.X + rpx(20)),
            static_cast<LONG>(badge_text_area.Y),
            static_cast<LONG>(badge_text_area.GetRight() - rpx(10)),
            static_cast<LONG>(badge_text_area.GetBottom())};
        draw_text(
            device,
            view_.badge,
            badge_text,
            kTextGdi,
            badge_font_,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (view_.error.empty()) {
            SolidBrush status_dot(color(view_.badge_color));
            graphics.FillEllipse(
                &status_dot,
                margin,
                rpx(70),
                rpx(6),
                rpx(6));
            graphics.Flush();
            RECT status{
                static_cast<LONG>(margin + rpx(13)),
                px(63),
                client.right - px(16),
                px(84)};
            draw_text(
                device,
                view_.observe_only
                    ? L"Observe only. Learning stays local; the PiP will "
                      L"not move."
                    : view_.paused
                          ? L"Starts paused. Resume when ready; learning "
                            L"stays local."
                          : L"Safety-led placement. Visual evidence and "
                            L"learning stay local.",
                status,
                kSecondaryGdi,
                small_font_,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        const std::array<std::wstring, 3> labels{
            L"Chrome PiP", L"Learning updates", L"Visual signals"};
        const std::array<std::wstring, 3> values{
            view_.pip_value,
            view_.learning_value,
            view_.visual_value};
        const std::array<std::wstring, 3> details{
            view_.pip_detail,
            view_.learning_detail,
            view_.visual_detail};
        for (int index = 0; index < 3; ++index) {
            const REAL left =
                margin + index * (metric_width + metric_gap);
            RECT label{
                static_cast<LONG>(left + rpx(24)),
                static_cast<LONG>(metric_top + rpx(3)),
                static_cast<LONG>(left + metric_width - rpx(10)),
                static_cast<LONG>(metric_top + rpx(17))};
            draw_text(
                device,
                labels[static_cast<std::size_t>(index)],
                label,
                kSecondaryGdi,
                small_font_,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT value{
                static_cast<LONG>(left + rpx(11)),
                static_cast<LONG>(metric_top + rpx(18)),
                static_cast<LONG>(left + metric_width - rpx(10)),
                static_cast<LONG>(metric_top + rpx(39))};
            draw_text(
                device,
                values[static_cast<std::size_t>(index)],
                value,
                kTextGdi,
                title_font_,
                DT_LEFT | DT_TOP | DT_SINGLELINE);
            RECT detail{
                static_cast<LONG>(left + rpx(11)),
                static_cast<LONG>(metric_top + rpx(39)),
                static_cast<LONG>(left + metric_width - rpx(10)),
                static_cast<LONG>(metric_top + metric_height - rpx(3))};
            draw_text(
                device,
                details[static_cast<std::size_t>(index)],
                detail,
                kSecondaryGdi,
                small_font_,
                DT_LEFT | DT_TOP | DT_WORDBREAK);
        }
        RECT motto_text{
            static_cast<LONG>(motto.X + rpx(12)),
            static_cast<LONG>(motto.Y + rpx(5)),
            static_cast<LONG>(motto.GetRight() - rpx(12)),
            static_cast<LONG>(motto.GetBottom() - rpx(3))};
        draw_text(
            device,
            kMainMotto,
            motto_text,
            kSecondaryGdi,
            small_font_,
            DT_CENTER | DT_TOP | DT_WORDBREAK);
    }

    void paint_button(const DRAWITEMSTRUCT& item) {
        const bool selected = (item.itemState & ODS_SELECTED) != 0;
        const bool focused = (item.itemState & ODS_FOCUS) != 0;
        const bool hovered =
            hovered_control_ == static_cast<int>(item.CtlID);
        wchar_t text_buffer[160]{};
        GetWindowTextW(
            item.hwndItem,
            text_buffer,
            static_cast<int>(std::size(text_buffer)));
        const std::wstring label(text_buffer);

        FillRect(item.hDC, &item.rcItem, background_brush_);
        Graphics graphics(item.hDC);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        const RectF bounds = rectangle(item.rcItem);
        ARGB fill = kSurfaceRaised;
        ARGB border = kBorderStrong;
        COLORREF foreground_gdi = kTextGdi;
        if (item.CtlID == kPauseButton) {
            fill = selected ? 0xff5fc9b2 : kAccent;
            border = fill;
            foreground_gdi = RGB(13, 23, 24);
            if (hovered && !selected) {
                fill = 0xff8ce7d2;
            }
        } else if (item.CtlID == kLearningButton) {
            fill = hovered ? 0xff2a3544 : kSurfaceRaised;
            border = hovered ? kAccent : kBorderStrong;
        } else if (item.CtlID == kDonateButton) {
            fill = selected ? 0xffd8ae55
                            : hovered ? 0xffffd77a : kWarm;
            border = fill;
            foreground_gdi = RGB(23, 19, 10);
        } else if (item.CtlID == kTrayButton) {
            fill = hovered ? kSurfaceRaised : kSurface;
            border = hovered ? kBorderStrong : kBorder;
            foreground_gdi = kSecondaryGdi;
        } else if (item.CtlID == kExitButton) {
            fill = hovered ? kSurfaceRaised : kSurface;
            border = hovered ? kBorderStrong : kSurface;
            foreground_gdi = kSecondaryGdi;
        }
        if ((item.itemState & ODS_DISABLED) != 0) {
            foreground_gdi = RGB(118, 131, 149);
        }
        fill_round_rect(graphics, bounds, rpx(11), color(fill));
        stroke_round_rect(graphics, bounds, rpx(11), color(border));
        if (focused) {
            stroke_round_rect(
                graphics,
                RectF(
                    bounds.X + rpx(3),
                    bounds.Y + rpx(3),
                    std::max<REAL>(0, bounds.Width - rpx(6)),
                    std::max<REAL>(0, bounds.Height - rpx(6))),
                rpx(8),
                with_alpha(kText, 150));
        }
        graphics.Flush();
        draw_text(
            item.hDC,
            label,
            item.rcItem,
            foreground_gdi,
            semibold_font_,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    void draw_button(const DRAWITEMSTRUCT& item) {
        draw_buffered(
            item.hDC,
            item.rcItem,
            [&](HDC device, const RECT& bounds) {
                DRAWITEMSTRUCT buffered = item;
                buffered.hDC = device;
                buffered.rcItem = bounds;
                paint_button(buffered);
            });
    }

    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam) {
        if (taskbar_created_message_ != 0 &&
            message == taskbar_created_message_) {
            if (hidden_to_tray_ && !add_tray_icon()) {
                restore_from_tray(true);
            }
            return 0;
        }
        if (message == kTrayIconMessage) {
            handle_tray_message(wparam, lparam);
            return 0;
        }
        switch (message) {
        case WM_SIZE:
            layout_controls();
            return 0;
        case WM_DPICHANGED: {
            dpi_ = static_cast<UINT>(HIWORD(wparam));
            create_fonts();
            const auto* suggested =
                reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(
                window_,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            layout_controls();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = px(560);
            info->ptMinTrackSize.y = px(340);
            return 0;
        }
        case WM_HSCROLL:
        case WM_VSCROLL:
            return 0;
        case kHoverMessage: {
            const int control = static_cast<int>(wparam);
            const bool entered = lparam != 0;
            bool changed = false;
            if (entered) {
                if (hovered_control_ != control) {
                    hovered_control_ = control;
                    changed = true;
                }
            } else if (hovered_control_ == control) {
                hovered_control_ = 0;
                changed = true;
            }
            HWND control_window = GetDlgItem(window_, control);
            if (changed && control_window != nullptr) {
                InvalidateRect(control_window, nullptr, FALSE);
            }
            return 0;
        }
        case WM_DRAWITEM:
            draw_button(
                *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
            case kPauseButton:
                application_.request_pause();
                return 0;
            case kLearningButton:
                open_heatmap();
                return 0;
            case kDonateButton:
                if (HIWORD(wparam) == BN_CLICKED) {
                    open_donation();
                }
                return 0;
            case kTrayButton:
                if (HIWORD(wparam) == BN_CLICKED) {
                    hide_to_tray();
                }
                return 0;
            case kExitButton:
                application_.request_quit();
                DestroyWindow(window_);
                return 0;
            default:
                break;
            }
            break;
        case WM_TIMER:
            if (wparam == kPanelTimer) {
                if (application_.finished()) {
                    DestroyWindow(window_);
                } else {
                    refresh();
                }
                return 0;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC device = reinterpret_cast<HDC>(wparam);
            SetBkMode(device, TRANSPARENT);
            SetTextColor(device, kTextGdi);
            return reinterpret_cast<LRESULT>(background_brush_);
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC target = BeginPaint(window_, &paint);
            if (target != nullptr) {
                RECT client{};
                GetClientRect(window_, &client);
                if (client.right > 0 && client.bottom > 0) {
                    const RECT frame{
                        0, 0, client.right, client.bottom};
                    draw_buffered(
                        target,
                        frame,
                        [&](HDC buffer, const RECT&) {
                            paint_main(buffer);
                        });
                }
            }
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_CLOSE:
            application_.request_quit();
            DestroyWindow(window_);
            return 0;
        case WM_DESTROY:
            KillTimer(window_, kPanelTimer);
            remove_tray_icon();
            hidden_to_tray_ = false;
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window_, message, wparam, lparam);
    }

    HWND window_{};
    AdaptiveApplication& application_;
    HeatmapWindow* heatmap_{};
    HWND pause_button_{};
    HWND learning_button_{};
    HWND donate_button_{};
    HWND tray_button_{};
    HWND exit_button_{};
    HFONT heading_font_{};
    HFONT title_font_{};
    HFONT body_font_{};
    HFONT semibold_font_{};
    HFONT small_font_{};
    HFONT badge_font_{};
    HBRUSH background_brush_{};
    UINT dpi_{96};
    UINT taskbar_created_message_{};
    int hovered_control_{};
    bool tray_icon_added_{};
    bool hidden_to_tray_{};
    bool heatmap_visible_before_tray_{};
    MainView view_;

    friend class HeatmapWindow;
};

struct HeatmapLayout {
    RectF projection;
    bool has_projection{};
    RectF toolbar;
    RectF sidebar;
    RectF canvas;
    RectF selector_label;
    RectF combo;
    RectF layer_label;
    RectF layer_group;
    RectF legend_label;
    RectF legend;
    RectF detail;
    RectF learning;
    RectF outlines;
    RectF grid;
    RectF hover;
    RectF status;
};

class HeatmapWindow {
public:
    HeatmapWindow(
        AdaptiveApplication& application,
        HWND owner,
        ControlPanel& panel)
        : application_(application), owner_(owner), panel_(panel) {
        dpi_ = system_dpi();
        create_fonts();
        window_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            kHeatmapClass,
            L"DuckEggs - Learning maps",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            MulDiv(920, static_cast<int>(dpi_), 96),
            MulDiv(660, static_cast<int>(dpi_), 96),
            owner_,
            nullptr,
            GetModuleHandleW(nullptr),
            this);
        if (window_ == nullptr) {
            destroy_fonts();
            throw PlatformError("the Learning maps window could not start");
        }
    }

    ~HeatmapWindow() {
        if (window_ != nullptr) {
            KillTimer(window_, kHeatmapTimer);
        }
        if (background_brush_ != nullptr) {
            DeleteObject(background_brush_);
        }
        destroy_fonts();
    }

    HWND window() const {
        return window_;
    }

    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam);

private:
    int px(int logical) const {
        return MulDiv(logical, static_cast<int>(dpi_), 96);
    }

    REAL rpx(int logical) const {
        return static_cast<REAL>(px(logical));
    }

    REAL rpx(REAL logical) const {
        return logical * static_cast<REAL>(dpi_) / 96.0F;
    }

    void create_fonts() {
        destroy_fonts();
        heading_font_ = make_font(dpi_, 26, FW_SEMIBOLD);
        title_font_ = make_font(dpi_, 16, FW_SEMIBOLD);
        body_font_ = make_font(dpi_, 14, FW_NORMAL);
        semibold_font_ = make_font(dpi_, 13, FW_SEMIBOLD);
        small_font_ = make_font(dpi_, 12, FW_NORMAL);
        for (HWND control :
             {profile_, activity_, manual_, placement_}) {
            if (control != nullptr) {
                SendMessageW(
                    control,
                    WM_SETFONT,
                    reinterpret_cast<WPARAM>(
                        control == profile_ ? body_font_
                                            : semibold_font_),
                    TRUE);
            }
        }
        if (window_ != nullptr) {
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void destroy_fonts() {
        std::array<HFONT*, 5> fonts{
            &heading_font_,
            &title_font_,
            &body_font_,
            &semibold_font_,
            &small_font_};
        for (HFONT* font : fonts) {
            if (*font != nullptr) {
                DeleteObject(*font);
                *font = nullptr;
            }
        }
    }

    HWND make_layer_button(int identifier, const wchar_t* text) {
        HWND button = CreateWindowExW(
            0,
            L"BUTTON",
            text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(identifier)),
            GetModuleHandleW(nullptr),
            nullptr);
        if (button != nullptr) {
            SendMessageW(
                button,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(semibold_font_),
                TRUE);
            install_hover(button);
        }
        return button;
    }

    void create_controls() {
        profile_ = CreateWindowExW(
            WS_EX_NOPARENTNOTIFY,
            L"COMBOBOX",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
                CBS_HASSTRINGS,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kProfileCombo)),
            GetModuleHandleW(nullptr),
            nullptr);
        activity_ = make_layer_button(kActivityLayer, L"Activity");
        manual_ =
            make_layer_button(kManualLayer, L"Manual preference");
        placement_ =
            make_layer_button(kPlacementLayer, L"Placement");
        background_brush_ = CreateSolidBrush(kSurfaceGdi);
        if (profile_ != nullptr) {
            SendMessageW(
                profile_,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(body_font_),
                TRUE);
            (void)SetWindowTheme(
                profile_, L"DarkMode_Explorer", nullptr);
        }
        layout_controls();
        SetTimer(window_, kHeatmapTimer, 1000, nullptr);
        application_.request_insights(selected_profile_);
        updated_bridge();
    }

    bool projection_visible() const {
        return data_.is_object() &&
               bool_field(data_, "projection");
    }

    HeatmapLayout layout() const {
        RECT client{};
        GetClientRect(window_, &client);
        HeatmapLayout result;
        const REAL margin = rpx(16);
        result.toolbar = RectF(
            margin,
            rpx(70),
            std::max<REAL>(0, client.right - margin * 2.0F),
            rpx(32));
        REAL content_top = result.toolbar.GetBottom() + rpx(12);
        const REAL full_width =
            std::max<REAL>(0, client.right - margin * 2.0F);
        result.has_projection = projection_visible();
        if (result.has_projection) {
            result.projection =
                RectF(margin, rpx(110), full_width, rpx(46));
            content_top = result.projection.GetBottom() + rpx(12);
        }
        const REAL content_bottom =
            static_cast<REAL>(client.bottom) - margin;
        const REAL sidebar_width = rpx(222);
        result.sidebar = RectF(
            margin,
            content_top,
            sidebar_width,
            std::max<REAL>(0, content_bottom - content_top));
        result.canvas = RectF(
            margin + sidebar_width + rpx(12),
            content_top,
            std::max<REAL>(
                0,
                client.right - margin -
                    (margin + sidebar_width + rpx(12))),
            std::max<REAL>(0, content_bottom - content_top));

        result.selector_label = RectF(
            margin + rpx(8),
            result.toolbar.Y,
            rpx(88),
            result.toolbar.Height);
        result.combo = RectF(
            margin + rpx(98),
            result.toolbar.Y + rpx(3),
            rpx(260),
            rpx(26));
        result.layer_label = RectF(
            margin + rpx(370),
            result.toolbar.Y,
            rpx(54),
            result.toolbar.Height);
        result.layer_group = RectF(
            margin + rpx(424),
            result.toolbar.Y + rpx(2),
            std::max<REAL>(
                0, full_width - rpx(424)),
            result.toolbar.Height - rpx(4));
        result.legend_label = RectF(
            result.sidebar.X + rpx(14),
            result.sidebar.Y + rpx(14),
            result.sidebar.Width - rpx(28),
            rpx(16));
        result.legend = RectF(
            result.sidebar.X + rpx(14),
            result.sidebar.Y + rpx(34),
            result.sidebar.Width - rpx(28),
            rpx(10));
        const REAL learning_top =
            result.sidebar.GetBottom() - rpx(58);
        const REAL outlines_height = rpx(88);
        const REAL outline_top =
            learning_top - outlines_height - rpx(10);
        const REAL detail_top = result.sidebar.Y + rpx(112);
        result.detail = RectF(
            result.sidebar.X + rpx(14),
            detail_top,
            result.sidebar.Width - rpx(28),
            std::max<REAL>(0, outline_top - detail_top - rpx(10)));
        result.outlines = RectF(
            result.sidebar.X + rpx(14),
            outline_top,
            result.sidebar.Width - rpx(28),
            outlines_height);
        result.learning = RectF(
            result.sidebar.X + rpx(14),
            learning_top,
            result.sidebar.Width - rpx(28),
            rpx(46));

        result.hover = RectF(
            result.canvas.X + rpx(14),
            result.canvas.Y + rpx(12),
            result.canvas.Width - rpx(28),
            rpx(32));
        result.status = RectF(
            result.canvas.X + rpx(14),
            result.canvas.GetBottom() - rpx(58),
            result.canvas.Width - rpx(28),
            rpx(46));
        REAL grid_left = result.canvas.X + rpx(40);
        REAL grid_top = result.hover.GetBottom() + rpx(12);
        REAL grid_right = result.canvas.GetRight() - rpx(12);
        REAL grid_bottom = result.status.Y - rpx(8);
        const auto [columns, rows] = heatmap::grid_dimensions(data_);
        const REAL available_width =
            std::max<REAL>(1, grid_right - grid_left);
        const REAL available_height =
            std::max<REAL>(1, grid_bottom - grid_top);
        const REAL aspect =
            static_cast<REAL>(columns) / static_cast<REAL>(rows);
        REAL width = available_width;
        REAL height = width / aspect;
        if (height > available_height) {
            height = available_height;
            width = height * aspect;
        }
        grid_left += (available_width - width) / 2.0F;
        grid_top += (available_height - height) / 2.0F;
        result.grid =
            RectF(grid_left, grid_top, width, height);
        return result;
    }

    void layout_controls() {
        if (window_ == nullptr || profile_ == nullptr) {
            return;
        }
        const HeatmapLayout areas = layout();
        bool changed = move_child_if_needed(
            window_,
            profile_,
            static_cast<int>(areas.combo.X),
            static_cast<int>(areas.combo.Y),
            static_cast<int>(areas.combo.Width),
            px(250));
        const REAL segment_gap = rpx(4);
        const REAL activity_width = rpx(100);
        const REAL manual_width = rpx(168);
        const REAL placement_width = std::max<REAL>(
            rpx(112),
            areas.layer_group.Width - activity_width -
                manual_width - segment_gap * 2.0F);
        const std::array<REAL, 3> widths{
            activity_width, manual_width, placement_width};
        REAL left = areas.layer_group.X;
        HWND controls[3]{activity_, manual_, placement_};
        for (int index = 0; index < 3; ++index) {
            changed |= move_child_if_needed(
                window_,
                controls[index],
                static_cast<int>(left),
                static_cast<int>(areas.layer_group.Y),
                static_cast<int>(widths[static_cast<std::size_t>(index)]),
                static_cast<int>(areas.layer_group.Height));
            left +=
                widths[static_cast<std::size_t>(index)] + segment_gap;
        }
        if (changed) {
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void updated_bridge() {
        const bool projection_before = projection_visible();
        const Json next_bridge = application_.insights_snapshot();
        const Json candidate =
            next_bridge.contains("data")
                ? next_bridge["data"]
                : Json(nullptr);
        Json next_data = data_;
        if (candidate.is_object() &&
            heatmap::bridge_data_matches_selection(
                candidate, selected_profile_)) {
            next_data = candidate;
            const auto resolved = heatmap::resolve_profile_key(
                next_data, selected_profile_);
            if (!selected_profile_.has_value() &&
                resolved.has_value()) {
                selected_profile_ = resolved;
                application_.request_insights(selected_profile_);
            }
        } else if (!selected_profile_.has_value() &&
                   candidate.is_object()) {
            next_data = candidate;
            const auto resolved = heatmap::resolve_profile_key(
                next_data, selected_profile_);
            if (resolved.has_value()) {
                selected_profile_ = resolved;
            }
        } else if (candidate.is_object()) {
            next_data = Json(nullptr);
        } else {
            next_data = Json(nullptr);
        }
        const bool changed =
            next_bridge != bridge_ || next_data != data_;
        if (!changed) {
            update_profile_choices();
            return;
        }
        bridge_ = next_bridge;
        data_ = std::move(next_data);
        update_profile_choices();
        if (projection_visible() != projection_before) {
            layout_controls();
        }
        InvalidateRect(window_, nullptr, FALSE);
    }

    void update_profile_choices() {
        if (profile_ == nullptr ||
            SendMessageW(profile_, CB_GETDROPPEDSTATE, 0, 0) != 0) {
            return;
        }
        const auto choices = heatmap::profile_choices(data_);
        std::string signature;
        for (const auto& choice : choices) {
            signature += choice.key;
            signature.push_back('\n');
            signature += choice.label;
            signature.push_back('\n');
        }
        if (signature == profile_signature_) {
            return;
        }
        profile_signature_ = signature;
        choices_ = choices;
        SendMessageW(profile_, CB_RESETCONTENT, 0, 0);
        int selected = -1;
        for (std::size_t index = 0; index < choices_.size(); ++index) {
            const std::wstring label = wide(choices_[index].label);
            SendMessageW(
                profile_,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(label.c_str()));
            if (selected_profile_.has_value() &&
                choices_[index].key == *selected_profile_) {
                selected = static_cast<int>(index);
            }
        }
        if (selected < 0 && !choices_.empty()) {
            selected = 0;
            selected_profile_ = choices_.front().key;
            application_.request_insights(selected_profile_);
        }
        if (selected >= 0) {
            SendMessageW(
                profile_, CB_SETCURSEL, selected, 0);
        }
    }

    std::optional<std::string_view> layer_key_for_control(
        int identifier) const {
        switch (identifier) {
        case kActivityLayer:
            return "activity";
        case kManualLayer:
            return "manual";
        case kPlacementLayer:
            return "placement";
        default:
            return std::nullopt;
        }
    }

    std::string layer_key() const {
        return selected_layer_;
    }

    void select_layer(int identifier) {
        const auto key = layer_key_for_control(identifier);
        if (!key.has_value() || *key == selected_layer_) {
            return;
        }
        selected_layer_ = *key;
        for (HWND control : {activity_, manual_, placement_}) {
            InvalidateRect(control, nullptr, FALSE);
        }
        InvalidateRect(window_, nullptr, FALSE);
    }

    void update_hover(int x, int y) {
        const HeatmapLayout areas = layout();
        if (!data_.is_object() ||
            x < areas.grid.X || x >= areas.grid.GetRight() ||
            y < areas.grid.Y || y >= areas.grid.GetBottom()) {
            if (hover_cell_.has_value()) {
                hover_cell_.reset();
                InvalidateRect(window_, nullptr, FALSE);
            }
            return;
        }
        const auto [columns, rows] = heatmap::grid_dimensions(data_);
        const int column = std::clamp(
            static_cast<int>(
                (x - areas.grid.X) * columns / areas.grid.Width),
            0,
            columns - 1);
        const int row = std::clamp(
            static_cast<int>(
                (y - areas.grid.Y) * rows / areas.grid.Height),
            0,
            rows - 1);
        const std::pair<int, int> cell{column, row};
        if (cell != hover_cell_) {
            hover_cell_ = cell;
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void draw_backgrounds(HDC device) {
        const HeatmapLayout areas = layout();
        RECT client{};
        GetClientRect(window_, &client);
        Graphics graphics(device);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        graphics.Clear(color(kBackground));

        if (areas.has_projection) {
            fill_round_rect(
                graphics, areas.projection, rpx(11), color(kWarmSoft));
            stroke_round_rect(
                graphics,
                areas.projection,
                rpx(11),
                with_alpha(kWarm, 80));
        }
        fill_round_rect(
            graphics, areas.toolbar, rpx(10), color(kSurface));
        stroke_round_rect(
            graphics, areas.toolbar, rpx(10), color(kBorder));
        fill_round_rect(
            graphics, areas.sidebar, rpx(14), color(kSurface));
        stroke_round_rect(
            graphics, areas.sidebar, rpx(14), color(kBorder));
        fill_round_rect(
            graphics, areas.canvas, rpx(14), color(kSurface));
        stroke_round_rect(
            graphics, areas.canvas, rpx(14), color(kBorder));
        fill_round_rect(
            graphics, areas.layer_group, rpx(9), color(kBackground));
        stroke_round_rect(
            graphics, areas.layer_group, rpx(9), color(kBorder));

        const REAL egg_size = rpx(36);
        draw_egg_mark(
            graphics,
            RectF(rpx(16), rpx(12), egg_size, egg_size),
            rpx(1.5F));

        if (data_.is_object() && areas.grid.Width > 2 &&
            areas.grid.Height > 2) {
            const auto [columns, rows] =
                heatmap::grid_dimensions(data_);
            const std::string key = layer_key();
            graphics.SetSmoothingMode(SmoothingModeNone);
            for (int row = 0; row < rows; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const auto layer_index =
                        static_cast<std::size_t>(row * columns + column);
                    const auto value = heatmap::layer_value(
                        data_, key, layer_index);
                    const COLORREF cell_color = RGB(
                        static_cast<BYTE>(
                            std::stoi(heatmap::layer_color(
                                          key,
                                          value.has_value()
                                              ? Json(*value)
                                              : Json(nullptr))
                                          .substr(1, 2),
                                          0,
                                          16)),
                        static_cast<BYTE>(
                            std::stoi(heatmap::layer_color(
                                          key,
                                          value.has_value()
                                              ? Json(*value)
                                              : Json(nullptr))
                                          .substr(3, 2),
                                          0,
                                          16)),
                        static_cast<BYTE>(
                            std::stoi(heatmap::layer_color(
                                          key,
                                          value.has_value()
                                              ? Json(*value)
                                              : Json(nullptr))
                                          .substr(5, 2),
                                          0,
                                          16)));
                    const REAL left =
                        areas.grid.X +
                        areas.grid.Width * column / columns;
                    const REAL top =
                        areas.grid.Y +
                        areas.grid.Height * row / rows;
                    const REAL right =
                        areas.grid.X +
                        areas.grid.Width * (column + 1) / columns;
                    const REAL bottom =
                        areas.grid.Y +
                        areas.grid.Height * (row + 1) / rows;
                    SolidBrush brush(Color(
                        255,
                        GetRValue(cell_color),
                        GetGValue(cell_color),
                        GetBValue(cell_color)));
                    graphics.FillRectangle(
                        &brush,
                        RectF(
                            left,
                            top,
                            right - left,
                            bottom - top));
                }
            }
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            stroke_round_rect(
                graphics,
                areas.grid,
                rpx(5),
                color(kBorderStrong),
                rpx(1));
        } else {
            fill_round_rect(
                graphics, areas.grid, rpx(7), color(kBorder));
        }

        if (hover_cell_.has_value() && data_.is_object()) {
            const auto [columns, rows] =
                heatmap::grid_dimensions(data_);
            const auto [column, row] = *hover_cell_;
            const REAL cell_width = areas.grid.Width / columns;
            const REAL cell_height = areas.grid.Height / rows;
            const RectF cell{
                areas.grid.X + column * cell_width,
                areas.grid.Y + row * cell_height,
                cell_width,
                cell_height};
            draw_solid_rect(graphics, cell, color(kText), rpx(2));
            if (const auto pip_size =
                    heatmap::snapshot_pip_size(data_)) {
                const Json raw_size =
                    Json::array({pip_size->first, pip_size->second});
                if (const auto footprint =
                        heatmap::candidate_footprint(
                            column,
                            row,
                            columns,
                            rows,
                            raw_size)) {
                    RECT normalized_grid{
                        static_cast<LONG>(areas.grid.X),
                        static_cast<LONG>(areas.grid.Y),
                        static_cast<LONG>(areas.grid.GetRight()),
                        static_cast<LONG>(areas.grid.GetBottom())};
                    draw_dashed_rect(
                        graphics,
                        rectangle(
                            normalized_rect(
                                *footprint, normalized_grid)),
                        color(kFootprintOutline),
                        rpx(1.6F));
                }
            }
        }

        struct Outline {
            const char* key;
            ARGB color;
        };
        constexpr std::array<Outline, 3> outlines{{
            {"foreground_rect", kForegroundOutline},
            {"pip_rect", kPipOutline},
            {"recommendation_rect", kRecommendationOutline},
        }};
        for (const auto& outline : outlines) {
            if (!data_.contains(outline.key)) {
                continue;
            }
            const auto normalized =
                heatmap::normalize_rect_json(data_[outline.key]);
            if (!normalized.has_value()) {
                continue;
            }
            RECT grid_rect{
                static_cast<LONG>(areas.grid.X),
                static_cast<LONG>(areas.grid.Y),
                static_cast<LONG>(areas.grid.GetRight()),
                static_cast<LONG>(areas.grid.GetBottom())};
            draw_solid_rect(
                graphics,
                rectangle(normalized_rect(*normalized, grid_rect)),
                color(outline.color),
                rpx(1.8F));
        }
        graphics.Flush();
    }

    void draw_foreground_layer(
        HDC device,
        const HeatmapLayout& areas,
        const std::string& key) {
        const auto spec = heatmap::layer_spec(key);
        const REAL legend_steps = 80.0F;
        Graphics graphics(device);
        graphics.SetSmoothingMode(SmoothingModeNone);
        for (int step = 0; step < 80; ++step) {
            const std::string hex = heatmap::layer_color(
                key, static_cast<double>(step) / 79.0);
            const int red = std::stoi(hex.substr(1, 2), nullptr, 16);
            const int green = std::stoi(hex.substr(3, 2), nullptr, 16);
            const int blue = std::stoi(hex.substr(5, 2), nullptr, 16);
            SolidBrush brush(Color(
                255,
                static_cast<BYTE>(red),
                static_cast<BYTE>(green),
                static_cast<BYTE>(blue)));
            const REAL left =
                areas.legend.X + areas.legend.Width * step / legend_steps;
            const REAL right =
                areas.legend.X +
                areas.legend.Width * (step + 1) / legend_steps;
            graphics.FillRectangle(
                &brush,
                RectF(
                    left,
                    areas.legend.Y,
                    right - left + 1,
                    areas.legend.Height));
        }
        SolidBrush unknown(
            Color(
                255,
                static_cast<BYTE>(std::stoi(
                    std::string(heatmap::kUnknownColor).substr(1, 2),
                    nullptr,
                    16)),
                static_cast<BYTE>(std::stoi(
                    std::string(heatmap::kUnknownColor).substr(3, 2),
                    nullptr,
                    16)),
                static_cast<BYTE>(std::stoi(
                    std::string(heatmap::kUnknownColor).substr(5, 2),
                    nullptr,
                    16))));
        graphics.FillRectangle(
            &unknown,
            RectF(
                areas.legend.X,
                areas.legend.GetBottom() + rpx(27),
                rpx(16),
                rpx(10)));
        graphics.Flush();

        RECT title{
            px(62),
            px(7),
            static_cast<LONG>(static_cast<REAL>(areas.canvas.X) -
                              rpx(8)),
            px(39)};
        draw_text(
            device,
            L"Learning maps",
            title,
            kTextGdi,
            heading_font_);
        RECT subtitle{
            px(64),
            px(37),
            static_cast<LONG>(static_cast<REAL>(areas.canvas.X) +
                              areas.canvas.Width - rpx(20)),
            px(57)};
        draw_text(
            device,
            L"Read-only learning for the selected application and layout.",
            subtitle,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (areas.has_projection) {
            RECT projection{
                static_cast<LONG>(areas.projection.X + rpx(14)),
                static_cast<LONG>(areas.projection.Y + rpx(5)),
                static_cast<LONG>(
                    areas.projection.GetRight() - rpx(14)),
                static_cast<LONG>(
                    areas.projection.GetBottom() - rpx(4))};
            draw_text(
                device,
                wide(heatmap::projection_notice(data_).value_or(
                    "Projected maps are not live data.")),
                projection,
                kWarmGdi,
                small_font_,
                DT_LEFT | DT_VCENTER | DT_WORDBREAK);
        }

        RECT selector_label{
            static_cast<LONG>(areas.selector_label.X),
            static_cast<LONG>(areas.selector_label.Y),
            static_cast<LONG>(areas.selector_label.GetRight()),
            static_cast<LONG>(areas.selector_label.GetBottom())};
        draw_text(
            device,
            L"App / layout",
            selector_label,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT layer_label{
            static_cast<LONG>(areas.layer_label.X),
            static_cast<LONG>(areas.layer_label.Y),
            static_cast<LONG>(areas.layer_label.GetRight()),
            static_cast<LONG>(areas.layer_label.GetBottom())};
        draw_text(
            device,
            L"Layer",
            layer_label,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT legend_label{
            static_cast<LONG>(areas.legend_label.X),
            static_cast<LONG>(areas.legend_label.Y),
            static_cast<LONG>(areas.legend_label.GetRight()),
            static_cast<LONG>(areas.legend_label.GetBottom())};
        draw_text(
            device,
            L"Relative value",
            legend_label,
            kSecondaryGdi,
            small_font_);
        std::wstring low_label = L"Lower";
        std::wstring high_label = L"Higher";
        if (key == "activity") {
            low_label = L"Less activity";
            high_label = L"More activity";
        } else if (key == "manual") {
            low_label = L"Less preferred";
            high_label = L"More preferred";
        } else {
            low_label = L"Lower estimate";
            high_label = L"Higher estimate";
        }
        RECT low{
            static_cast<LONG>(areas.legend.X),
            static_cast<LONG>(areas.legend.GetBottom() + rpx(5)),
            static_cast<LONG>(areas.legend.X + rpx(94)),
            static_cast<LONG>(areas.legend.GetBottom() + rpx(21))};
        RECT high{
            static_cast<LONG>(areas.legend.GetRight() - rpx(94)),
            static_cast<LONG>(areas.legend.GetBottom() + rpx(5)),
            static_cast<LONG>(areas.legend.GetRight()),
            static_cast<LONG>(areas.legend.GetBottom() + rpx(21))};
        draw_text(
            device,
            low_label,
            low,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_TOP | DT_SINGLELINE);
        draw_text(device, high_label, high, kSecondaryGdi, small_font_,
                  DT_RIGHT | DT_TOP | DT_SINGLELINE);
        RECT unknown_text{
            static_cast<LONG>(areas.legend.X + rpx(23)),
            static_cast<LONG>(areas.legend.GetBottom() + rpx(24)),
            static_cast<LONG>(areas.legend.GetRight()),
            static_cast<LONG>(areas.legend.GetBottom() + rpx(42))};
        draw_text(
            device,
            L"Unknown / no data (gray)",
            unknown_text,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_TOP | DT_SINGLELINE);

        RECT detail_title{
            static_cast<LONG>(areas.detail.X),
            static_cast<LONG>(areas.detail.Y),
            static_cast<LONG>(areas.detail.GetRight()),
            static_cast<LONG>(areas.detail.Y + rpx(18))};
        draw_text(
            device,
            L"What this layer shows",
            detail_title,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_TOP | DT_SINGLELINE);
        RECT detail{
            static_cast<LONG>(areas.detail.X),
            static_cast<LONG>(areas.detail.Y + rpx(20)),
            static_cast<LONG>(areas.detail.GetRight()),
            static_cast<LONG>(areas.detail.GetBottom())};
        draw_text(
            device,
            wide(spec.meaning),
            detail,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_TOP | DT_WORDBREAK);

        RECT outlines_title{
            static_cast<LONG>(areas.outlines.X),
            static_cast<LONG>(areas.outlines.Y),
            static_cast<LONG>(areas.outlines.GetRight()),
            static_cast<LONG>(areas.outlines.Y + rpx(18))};
        draw_text(
            device,
            L"Outlines",
            outlines_title,
            kSecondaryGdi,
            small_font_);
        RECT outlines_text{
            static_cast<LONG>(areas.outlines.X),
            static_cast<LONG>(areas.outlines.Y + rpx(21)),
            static_cast<LONG>(areas.outlines.GetRight()),
            static_cast<LONG>(areas.outlines.GetBottom())};
        draw_text(
            device,
            L"Violet: foreground\n"
            L"Amber: current PiP\n"
            L"Teal: recommendation\n"
            L"White dashed: full PiP",
            outlines_text,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_TOP | DT_WORDBREAK);

        if (data_.is_object()) {
            RECT learning{
                static_cast<LONG>(areas.learning.X),
                static_cast<LONG>(areas.learning.Y),
                static_cast<LONG>(areas.learning.GetRight()),
                static_cast<LONG>(areas.learning.GetBottom())};
            draw_text(
                device,
                learning_summary(data_),
                learning,
                kSecondaryGdi,
                small_font_,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        }

        RECT hover_title{
            static_cast<LONG>(areas.hover.X),
            static_cast<LONG>(areas.hover.Y),
            static_cast<LONG>(areas.hover.GetRight()),
            static_cast<LONG>(areas.hover.GetBottom())};
        if (hover_cell_.has_value() && data_.is_object()) {
            const auto [columns, rows] =
                heatmap::grid_dimensions(data_);
            const auto [column, row] = *hover_cell_;
            const auto value = heatmap::layer_value(
                data_,
                key,
                static_cast<std::size_t>(row * columns + column));
            std::wstring message =
                L"Cell " +
                std::to_wstring(
                    static_cast<int>(
                        (column + 0.5) / columns * 100.0)) +
                L"% \u00d7 " +
                std::to_wstring(
                    static_cast<int>(
                        (row + 0.5) / rows * 100.0)) +
                L"%   Value: " +
                (value.has_value()
                     ? std::to_wstring(
                           static_cast<int>(
                               *value * 100.0 + 0.5)) +
                           L" / 100"
                     : L"unknown");
            if (const auto pip_size =
                    heatmap::snapshot_pip_size(data_)) {
                message += L"   Full PiP: " +
                    std::to_wstring(static_cast<int>(
                        pip_size->first * 100.0 + 0.5)) +
                    L"% \u00d7 " +
                    std::to_wstring(static_cast<int>(
                        pip_size->second * 100.0 + 0.5)) +
                    L"%";
            }
            draw_text(
                device,
                message,
                hover_title,
                kTextGdi,
                small_font_,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                    DT_END_ELLIPSIS);
        } else {
            draw_text(
                device,
                L"Hover over a cell to inspect its value and the full PiP "
                L"footprint.",
                hover_title,
                kSecondaryGdi,
                small_font_,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                    DT_END_ELLIPSIS);
        }

        std::wstring status_text;
        COLORREF status_color = kSecondaryGdi;
        if (bridge_.contains("error") &&
            bridge_["error"].is_string()) {
            status_text = actionable_error(
                bridge_["error"].get<std::string>());
            status_color = kErrorGdi;
        } else if (!data_.is_object()) {
            status_text =
                L"Preparing local learning maps. This view is read-only.";
        } else {
            const std::string app = data_.contains("app_id") &&
                                            data_["app_id"].is_string()
                                        ? data_["app_id"].get<std::string>()
                                        : "selected application";
            const std::string layout_name =
                data_.contains("layout_bin") &&
                        data_["layout_bin"].is_string()
                    ? data_["layout_bin"].get<std::string>()
                    : "layout";
            status_text =
                L"Showing " + wide(app) + L" \u00b7 " +
                wide(layout_name) + L". Refreshes about once per second.";
            if (const auto cold =
                    heatmap::cold_start_message(data_)) {
                status_text += L" " + wide(*cold);
            }
        }
        RECT status_rect{
            static_cast<LONG>(areas.status.X),
            static_cast<LONG>(areas.status.Y),
            static_cast<LONG>(areas.status.GetRight()),
            static_cast<LONG>(areas.status.GetBottom())};
        draw_text(
            device,
            status_text,
            status_rect,
            status_color,
            small_font_,
            DT_LEFT | DT_TOP | DT_WORDBREAK);

        if (data_.is_object() &&
            heatmap::profile_choices(data_).empty()) {
            const REAL overlay_width =
                std::min<REAL>(rpx(440), areas.grid.Width - rpx(32));
            const RectF overlay{
                areas.grid.X +
                    (areas.grid.Width - overlay_width) / 2.0F,
                areas.grid.Y + areas.grid.Height * 0.24F,
                overlay_width,
                rpx(128)};
            Graphics overlay_graphics(device);
            fill_round_rect(
                overlay_graphics,
                overlay,
                rpx(14),
                with_alpha(kSurfaceRaised, 244));
            stroke_round_rect(
                overlay_graphics,
                overlay,
                rpx(14),
                color(kBorderStrong));
            overlay_graphics.Flush();
            RECT overlay_title{
                static_cast<LONG>(overlay.X + rpx(20)),
                static_cast<LONG>(overlay.Y + rpx(15)),
                static_cast<LONG>(overlay.GetRight() - rpx(20)),
                static_cast<LONG>(overlay.Y + rpx(39))};
            draw_text(
                device,
                L"No learning data yet",
                overlay_title,
                kTextGdi,
                title_font_);
            RECT overlay_detail{
                static_cast<LONG>(overlay.X + rpx(20)),
                static_cast<LONG>(overlay.Y + rpx(42)),
                static_cast<LONG>(overlay.GetRight() - rpx(20)),
                static_cast<LONG>(overlay.GetBottom() - rpx(12))};
            draw_text(
                device,
                wide(heatmap::cold_start_message(data_).value_or(
                    "Learning maps will appear after DuckEggs observes use.")),
                overlay_detail,
                kSecondaryGdi,
                small_font_,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        }
    }

    void paint_button(const DRAWITEMSTRUCT& item) {
        const auto button_layer =
            layer_key_for_control(static_cast<int>(item.CtlID));
        const bool selected =
            button_layer.has_value() &&
            *button_layer == selected_layer_;
        const bool hovered =
            hovered_control_ == static_cast<int>(item.CtlID);
        const bool focused = (item.itemState & ODS_FOCUS) != 0;
        FillRect(item.hDC, &item.rcItem, background_brush_);
        Graphics graphics(item.hDC);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        const RectF bounds = rectangle(item.rcItem);
        fill_round_rect(
            graphics, bounds, rpx(9), color(kBackground));
        if (selected) {
            fill_round_rect(
                graphics, bounds, rpx(9), color(kAccentSoft));
            stroke_round_rect(
                graphics, bounds, rpx(9), color(kAccent));
        }
        if (hovered && !selected) {
            fill_round_rect(
                graphics,
                bounds,
                rpx(9),
                color(kSurfaceRaised));
        }
        if (focused) {
            stroke_round_rect(
                graphics,
                RectF(
                    bounds.X + rpx(3),
                    bounds.Y + rpx(3),
                    std::max<REAL>(0, bounds.Width - rpx(6)),
                    std::max<REAL>(0, bounds.Height - rpx(6))),
                rpx(7),
                with_alpha(kText, 160));
        }
        graphics.Flush();
        wchar_t text_buffer[160]{};
        GetWindowTextW(
            item.hwndItem,
            text_buffer,
            static_cast<int>(std::size(text_buffer)));
        RECT text_rect = item.rcItem;
        text_rect.left += px(12);
        text_rect.right -= px(12);
        draw_text(
            item.hDC,
            text_buffer,
            text_rect,
            selected ? kTextGdi : kSecondaryGdi,
            semibold_font_,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    void draw_button(const DRAWITEMSTRUCT& item) {
        draw_buffered(
            item.hDC,
            item.rcItem,
            [&](HDC device, const RECT& bounds) {
                DRAWITEMSTRUCT buffered = item;
                buffered.hDC = device;
                buffered.rcItem = bounds;
                paint_button(buffered);
            });
    }

    void paint_combo_item(const DRAWITEMSTRUCT& item) {
        const bool selected =
            (item.itemState & ODS_SELECTED) != 0;
        Graphics graphics(item.hDC);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.Clear(color(kSurfaceRaised));
        SolidBrush fill(
            color(selected ? kAccentSoft : kSurfaceRaised));
        graphics.FillRectangle(&fill, rectangle(item.rcItem));
        graphics.Flush();
        wchar_t text_buffer[512]{};
        if (item.itemID != static_cast<UINT>(-1)) {
            SendMessageW(
                item.hwndItem,
                CB_GETLBTEXT,
                item.itemID,
                reinterpret_cast<LPARAM>(text_buffer));
        } else {
            const int selection = static_cast<int>(
                SendMessageW(item.hwndItem, CB_GETCURSEL, 0, 0));
            if (selection >= 0) {
                SendMessageW(
                    item.hwndItem,
                    CB_GETLBTEXT,
                    selection,
                    reinterpret_cast<LPARAM>(text_buffer));
            }
        }
        RECT text_rect = item.rcItem;
        text_rect.left += px(10);
        text_rect.right -= px(28);
        draw_text(
            item.hDC,
            text_buffer,
            text_rect,
            kTextGdi,
            body_font_,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    void draw_combo_item(const DRAWITEMSTRUCT& item) {
        draw_buffered(
            item.hDC,
            item.rcItem,
            [&](HDC device, const RECT& bounds) {
                DRAWITEMSTRUCT buffered = item;
                buffered.hDC = device;
                buffered.rcItem = bounds;
                paint_combo_item(buffered);
            });
    }

    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            create_controls();
            apply_window_chrome(window_);
            return 0;
        case WM_SIZE:
            layout_controls();
            return 0;
        case WM_DPICHANGED: {
            dpi_ = static_cast<UINT>(HIWORD(wparam));
            create_fonts();
            const auto* suggested =
                reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(
                window_,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            layout_controls();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = px(860);
            info->ptMinTrackSize.y = px(620);
            return 0;
        }
        case WM_MEASUREITEM: {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            if (measure->CtlID == kProfileCombo) {
                measure->itemHeight = static_cast<UINT>(px(30));
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            const auto& item =
                *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
            if (item.CtlID == kProfileCombo) {
                draw_combo_item(item);
            } else {
                draw_button(item);
            }
            return TRUE;
        }
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC device = reinterpret_cast<HDC>(wparam);
            SetBkMode(device, OPAQUE);
            SetBkColor(device, kSurfaceGdi);
            SetTextColor(device, kTextGdi);
            return reinterpret_cast<LRESULT>(background_brush_);
        }
        case kHoverMessage: {
            const int control = static_cast<int>(wparam);
            const bool entered = lparam != 0;
            bool changed = false;
            if (entered) {
                if (hovered_control_ != control) {
                    hovered_control_ = control;
                    changed = true;
                }
            } else if (hovered_control_ == control) {
                hovered_control_ = 0;
                changed = true;
            }
            HWND control_window = GetDlgItem(window_, control);
            if (changed && control_window != nullptr) {
                InvalidateRect(control_window, nullptr, FALSE);
            }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == kProfileCombo &&
                HIWORD(wparam) == CBN_SELCHANGE) {
                const int selected = static_cast<int>(
                    SendMessageW(profile_, CB_GETCURSEL, 0, 0));
                if (selected >= 0 &&
                    static_cast<std::size_t>(selected) <
                        choices_.size()) {
                    selected_profile_ =
                        choices_[static_cast<std::size_t>(selected)].key;
                    application_.request_insights(selected_profile_);
                    InvalidateRect(window_, nullptr, FALSE);
                }
                return 0;
            }
            if (LOWORD(wparam) >= kActivityLayer &&
                LOWORD(wparam) <= kPlacementLayer &&
                HIWORD(wparam) == BN_CLICKED) {
                select_layer(LOWORD(wparam));
                return 0;
            }
            break;
        case WM_TIMER:
            if (wparam == kHeatmapTimer) {
                application_.request_insights(selected_profile_);
                updated_bridge();
                return 0;
            }
            break;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{
                sizeof(TRACKMOUSEEVENT), TME_LEAVE, window_, 0};
            TrackMouseEvent(&track);
            update_hover(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hover_cell_.has_value()) {
                hover_cell_.reset();
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC target = BeginPaint(window_, &paint);
            if (target != nullptr) {
                RECT client{};
                GetClientRect(window_, &client);
                if (client.right > 0 && client.bottom > 0) {
                    const RECT frame{
                        0, 0, client.right, client.bottom};
                    draw_buffered(
                        target,
                        frame,
                        [&](HDC buffer, const RECT&) {
                            draw_backgrounds(buffer);
                            draw_foreground_layer(
                                buffer,
                                layout(),
                                layer_key());
                        });
                }
            }
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;
        case WM_NCDESTROY: {
            KillTimer(window_, kHeatmapTimer);
            const HWND destroyed = window_;
            panel_.heatmap_destroyed(destroyed);
            window_ = nullptr;
            const LRESULT result =
                DefWindowProcW(destroyed, message, wparam, lparam);
            delete this;
            return result;
        }
        default:
            break;
        }
        return DefWindowProcW(window_, message, wparam, lparam);
    }

    HWND window_{};
    HWND owner_{};
    AdaptiveApplication& application_;
    ControlPanel& panel_;
    HWND profile_{};
    HWND activity_{};
    HWND manual_{};
    HWND placement_{};
    HFONT heading_font_{};
    HFONT title_font_{};
    HFONT body_font_{};
    HFONT semibold_font_{};
    HFONT small_font_{};
    HBRUSH background_brush_{};
    UINT dpi_{96};
    int hovered_control_{};
    std::optional<std::string> selected_profile_;
    std::string selected_layer_{"activity"};
    std::vector<heatmap::ProfileChoice> choices_;
    std::string profile_signature_;
    Json bridge_ = Json::object();
    Json data_ = Json(nullptr);
    std::optional<std::pair<int, int>> hover_cell_;
};

ControlPanel::~ControlPanel() {
    remove_tray_icon();
    if (heatmap_ != nullptr) {
        DestroyWindow(heatmap_->window());
        heatmap_ = nullptr;
    }
    if (background_brush_ != nullptr) {
        DeleteObject(background_brush_);
    }
    destroy_fonts();
}

void ControlPanel::hide_to_tray() {
    if (hidden_to_tray_) {
        return;
    }
    if (!add_tray_icon()) {
        MessageBoxW(
            window_,
            L"DuckEggs could not add its notification icon, so the "
            L"window remains open.",
            L"DuckEggs",
            MB_OK | MB_ICONERROR);
        return;
    }
    hidden_to_tray_ = true;
    heatmap_visible_before_tray_ =
        heatmap_ != nullptr &&
        IsWindowVisible(heatmap_->window()) != FALSE;
    if (heatmap_visible_before_tray_) {
        ShowWindow(heatmap_->window(), SW_HIDE);
    }
    ShowWindow(window_, SW_HIDE);
}

void ControlPanel::restore_from_tray(bool re_add_failed) {
    remove_tray_icon();
    hidden_to_tray_ = false;
    if (window_ != nullptr) {
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
    }
    HWND foreground = window_;
    if (heatmap_visible_before_tray_ &&
        heatmap_ != nullptr &&
        IsWindow(heatmap_->window())) {
        ShowWindow(heatmap_->window(), SW_SHOW);
        foreground = heatmap_->window();
    }
    heatmap_visible_before_tray_ = false;
    if (foreground != nullptr) {
        SetForegroundWindow(foreground);
    }
    if (re_add_failed) {
        MessageBoxW(
            window_,
            L"DuckEggs could not restore its notification icon after "
            L"Explorer restarted. The window has been restored.",
            L"DuckEggs",
            MB_OK | MB_ICONERROR);
    }
}

void ControlPanel::heatmap_destroyed(HWND window) {
    if (heatmap_ != nullptr && heatmap_->window() == window) {
        heatmap_ = nullptr;
        heatmap_visible_before_tray_ = false;
    }
}

void ControlPanel::open_heatmap() {
    if (heatmap_ != nullptr && IsWindow(heatmap_->window())) {
        ShowWindow(heatmap_->window(), SW_RESTORE);
        SetForegroundWindow(heatmap_->window());
        return;
    }
    try {
        heatmap_ = new HeatmapWindow(
            application_, window_, *this);
        ShowWindow(heatmap_->window(), SW_SHOWNORMAL);
        UpdateWindow(heatmap_->window());
        SetForegroundWindow(heatmap_->window());
    } catch (const std::exception& exception) {
        MessageBoxW(
            window_,
            clean_wide(exception.what()).c_str(),
            L"DuckEggs",
            MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK ControlPanel::window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
    ControlPanel* panel = reinterpret_cast<ControlPanel*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        panel = static_cast<ControlPanel*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(panel));
        if (panel != nullptr) {
            panel->window_ = window;
        }
    }
    if (panel != nullptr) {
        return panel->handle(message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK HeatmapWindow::window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
    HeatmapWindow* heatmap =
        reinterpret_cast<HeatmapWindow*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        heatmap = static_cast<HeatmapWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(heatmap));
        if (heatmap != nullptr) {
            heatmap->window_ = window;
        }
    }
    if (heatmap != nullptr) {
        return heatmap->handle(message, wparam, lparam);
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void register_classes() {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSEXW panel{};
    panel.cbSize = sizeof(panel);
    panel.style = CS_HREDRAW | CS_VREDRAW;
    panel.lpfnWndProc = &ControlPanel::window_proc;
    panel.hInstance = GetModuleHandleW(nullptr);
    panel.hIcon = reinterpret_cast<HICON>(LoadImageW(
        panel.hInstance,
        MAKEINTRESOURCEW(ui::brand::kAppIconResourceId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    panel.hIconSm = reinterpret_cast<HICON>(LoadImageW(
        panel.hInstance,
        MAKEINTRESOURCEW(ui::brand::kAppIconResourceId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    panel.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    panel.hbrBackground = nullptr;
    panel.lpszClassName = kPanelClass;
    if (RegisterClassExW(&panel) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw PlatformError(
            "the DuckEggs window class could not register");
    }

    WNDCLASSEXW maps{};
    maps.cbSize = sizeof(maps);
    maps.style = CS_HREDRAW | CS_VREDRAW;
    maps.lpfnWndProc = &HeatmapWindow::window_proc;
    maps.hInstance = GetModuleHandleW(nullptr);
    maps.hIcon = reinterpret_cast<HICON>(LoadImageW(
        maps.hInstance,
        MAKEINTRESOURCEW(ui::brand::kAppIconResourceId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    maps.hIconSm = reinterpret_cast<HICON>(LoadImageW(
        maps.hInstance,
        MAKEINTRESOURCEW(ui::brand::kAppIconResourceId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED));
    maps.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    maps.hbrBackground = nullptr;
    maps.lpszClassName = kHeatmapClass;
    if (RegisterClassExW(&maps) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw PlatformError(
            "the Learning maps window class could not register");
    }
    registered = true;
}

class GdiplusSession {
public:
    GdiplusSession() {
        GdiplusStartupInput input;
        started_ =
            GdiplusStartup(&token_, &input, nullptr) == Ok;
    }

    ~GdiplusSession() {
        if (started_) {
            ui::brand::release_mark_cache();
            GdiplusShutdown(token_);
        }
    }

    bool started() const {
        return started_;
    }

private:
    ULONG_PTR token_{};
    bool started_{};
};

}  // namespace

bool run_platform_ui(AdaptiveApplication& application) {
    enable_per_monitor_dpi();
    GdiplusSession gdiplus;
    if (!gdiplus.started()) {
        return false;
    }
    HWND window = nullptr;
    try {
        register_classes();
        const UINT dpi = system_dpi();
        window = CreateWindowExW(
            0,
            kPanelClass,
            L"DuckEggs",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            MulDiv(600, static_cast<int>(dpi), 96),
            MulDiv(360, static_cast<int>(dpi), 96),
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (window == nullptr) {
            return false;
        }
        apply_window_chrome(window);
        auto panel = std::make_unique<ControlPanel>(window, application);
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(panel.get()));
        ShowWindow(window, SW_SHOWNORMAL);
        UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            const HWND root =
                GetAncestor(message.hwnd, GA_ROOT);
            if (root != nullptr &&
                IsDialogMessageW(root, &message)) {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        panel.reset();
        application.request_quit();
        return true;
    } catch (...) {
        if (window != nullptr && IsWindow(window)) {
            DestroyWindow(window);
        }
        return false;
    }
}

}  // namespace schiva

#else

namespace schiva {

bool run_platform_ui(AdaptiveApplication&) {
    return false;
}

}  // namespace schiva

#endif
