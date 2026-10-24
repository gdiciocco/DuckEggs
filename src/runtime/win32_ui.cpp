#include "ui.hpp"

#ifdef _WIN32

#include "schiva/heatmap.hpp"
#include "schiva/runtime.hpp"

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
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
constexpr int kProfileCombo = 1101;
constexpr int kActivityLayer = 1111;
constexpr int kManualLayer = 1112;
constexpr int kPlacementLayer = 1113;
constexpr UINT kPanelTimer = 1;
constexpr UINT kHeatmapTimer = 2;
constexpr UINT kHoverMessage = WM_APP + 41;

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
constexpr ARGB kMuted = 0xff768395;
constexpr ARGB kError = 0xffff9189;
constexpr ARGB kErrorSoft = 0x24ff9189;
constexpr ARGB kWarm = 0xfff4c969;
constexpr ARGB kWarmSoft = 0x24f4c969;
constexpr ARGB kForegroundOutline = 0xffb594ff;
constexpr ARGB kPipOutline = 0xfff0a15c;
constexpr ARGB kRecommendationOutline = 0xff73dcc5;
constexpr ARGB kFootprintOutline = 0xffe8eef5;

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
    GraphicsPath egg;
    egg.StartFigure();
    egg.AddBezier(
        bounds.X + bounds.Width * 0.5F,
        bounds.Y,
        bounds.X + bounds.Width * 0.78F,
        bounds.Y,
        bounds.GetRight(),
        bounds.Y + bounds.Height * 0.42F,
        bounds.GetRight(),
        bounds.Y + bounds.Height * 0.65F);
    egg.AddBezier(
        bounds.GetRight(),
        bounds.Y + bounds.Height * 0.88F,
        bounds.X + bounds.Width * 0.76F,
        bounds.GetBottom(),
        bounds.X + bounds.Width * 0.5F,
        bounds.GetBottom());
    egg.AddBezier(
        bounds.X + bounds.Width * 0.24F,
        bounds.GetBottom(),
        bounds.X,
        bounds.Y + bounds.Height * 0.88F,
        bounds.X,
        bounds.Y + bounds.Height * 0.65F);
    egg.AddBezier(
        bounds.X,
        bounds.Y + bounds.Height * 0.42F,
        bounds.X + bounds.Width * 0.22F,
        bounds.Y,
        bounds.X + bounds.Width * 0.5F,
        bounds.Y);
    egg.CloseFigure();
    SolidBrush egg_fill(with_alpha(kAccent, 24));
    Pen egg_pen(color(kAccent), stroke_width);
    graphics.FillPath(&egg_fill, &egg);
    graphics.DrawPath(&egg_pen, &egg);
    SolidBrush yolk(color(kWarm));
    const REAL yolk_size = bounds.Width * 0.24F;
    graphics.FillEllipse(
        &yolk,
        bounds.X + (bounds.Width - yolk_size) / 2.0F,
        bounds.Y + bounds.Height * 0.49F,
        yolk_size,
        yolk_size);
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
    if (message == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track{
            sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
        TrackMouseEvent(&track);
        SendMessageW(
            GetParent(window),
            kHoverMessage,
            static_cast<WPARAM>(GetDlgCtrlID(window)),
            1);
    } else if (message == WM_MOUSELEAVE) {
        SendMessageW(
            GetParent(window),
            kHoverMessage,
            static_cast<WPARAM>(GetDlgCtrlID(window)),
            0);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

void install_hover(HWND button) {
    SetWindowSubclass(
        button,
        &button_hover_proc,
        static_cast<UINT_PTR>(GetDlgCtrlID(button)),
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
};

class HeatmapWindow;

class ControlPanel {
public:
    ControlPanel(HWND window, AdaptiveApplication& application)
        : window_(window), application_(application) {
        dpi_ = dpi_for_window(window_);
        create_fonts();
        create_controls();
        SetTimer(window_, kPanelTimer, 200, nullptr);
        refresh();
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

    void create_fonts() {
        destroy_fonts();
        heading_font_ = make_font(dpi_, 33, FW_SEMIBOLD);
        title_font_ = make_font(dpi_, 16, FW_SEMIBOLD);
        body_font_ = make_font(dpi_, 14, FW_NORMAL);
        semibold_font_ = make_font(dpi_, 14, FW_SEMIBOLD);
        small_font_ = make_font(dpi_, 12, FW_NORMAL);
        badge_font_ = make_font(dpi_, 12, FW_SEMIBOLD);
        for (HWND button :
             {pause_button_, learning_button_, exit_button_}) {
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
        const int margin = px(30);
        const int gap = px(12);
        const int height = px(44);
        const int bottom = client.bottom - px(28);
        int right = client.right - margin;
        const int exit_width = px(86);
        MoveWindow(
            exit_button_,
            right - exit_width,
            bottom - height,
            exit_width,
            height,
            TRUE);
        right -= exit_width + gap;
        const int learning_width = px(164);
        MoveWindow(
            learning_button_,
            right - learning_width,
            bottom - height,
            learning_width,
            height,
            TRUE);
        right -= learning_width + gap;
        const int pause_width = px(142);
        MoveWindow(
            pause_button_,
            right - pause_width,
            bottom - height,
            pause_width,
            height,
            TRUE);
        InvalidateRect(window_, nullptr, FALSE);
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
        view_ = current_view();
        SetWindowTextW(
            pause_button_, view_.paused ? L"Resume" : L"Pause");
        InvalidateRect(window_, nullptr, FALSE);
    }

    void open_heatmap();

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

        const REAL margin = rpx(30);
        const REAL card_top = rpx(112);
        const REAL card_bottom = static_cast<REAL>(client.bottom) - rpx(92);
        const RectF card{
            margin,
            card_top,
            std::max<REAL>(0, client.right - margin * 2.0F),
            std::max<REAL>(0, card_bottom - card_top)};
        fill_round_rect(graphics, card, rpx(18), color(kSurface));
        stroke_round_rect(graphics, card, rpx(18), color(kBorder));

        const RectF egg{
            margin + rpx(2),
            rpx(28),
            rpx(56),
            rpx(58)};
        draw_egg_mark(graphics, egg, rpx(2));

        HDC measure = device;
        const int badge_width =
            text_width(measure, view_.badge, badge_font_) + px(44);
        const RectF badge{
            static_cast<REAL>(client.right) - margin -
                static_cast<REAL>(badge_width),
            rpx(34),
            static_cast<REAL>(badge_width),
            rpx(30)};
        draw_status_pill(graphics, badge, view_.badge_color);

        const REAL inner = rpx(24);
        const REAL metric_top = card.Y + rpx(84);
        const REAL error_height = view_.error.empty() ? 0.0F : rpx(58);
        const REAL metric_bottom =
            card.GetBottom() - inner - error_height -
            (view_.error.empty() ? 0.0F : rpx(14));
        const REAL metric_gap = rpx(12);
        const REAL metric_width =
            (card.Width - inner * 2.0F - metric_gap * 2.0F) / 3.0F;
        for (int index = 0; index < 3; ++index) {
            const RectF metric{
                card.X + inner + index * (metric_width + metric_gap),
                metric_top,
                metric_width,
                std::max<REAL>(rpx(96), metric_bottom - metric_top)};
            fill_round_rect(
                graphics, metric, rpx(13), color(kSurfaceRaised));
            stroke_round_rect(
                graphics, metric, rpx(13), color(kBorder));
            const ARGB dot_color =
                index == 0 ? view_.badge_color
                           : index == 1 ? kAccent : kWarm;
            SolidBrush dot(color(dot_color));
            graphics.FillEllipse(
                &dot,
                metric.X + rpx(17),
                metric.Y + rpx(20),
                rpx(8),
                rpx(8));
        }

        if (!view_.error.empty()) {
            const RectF error_card{
                card.X + inner,
                card.GetBottom() - inner - rpx(58),
                card.Width - inner * 2.0F,
                rpx(58)};
            fill_round_rect(
                graphics, error_card, rpx(12), color(kErrorSoft));
            stroke_round_rect(
                graphics,
                error_card,
                rpx(12),
                with_alpha(kError, 95));
        }
        graphics.Flush();

        RECT title{
            client.left + px(92),
            px(29),
            client.right - px(220),
            px(73)};
        draw_text(
            device,
            L"DuckEggs",
            title,
            kTextGdi,
            heading_font_);
        RECT subtitle{
            client.left + px(94),
            px(72),
            client.right - px(170),
            px(96)};
        draw_text(
            device,
            L"Adaptive Picture-in-Picture placement",
            subtitle,
            kSecondaryGdi,
            small_font_);

        const RectF badge_text_area = badge;
        RECT badge_text{
            static_cast<LONG>(badge_text_area.X + rpx(22)),
            static_cast<LONG>(badge_text_area.Y),
            static_cast<LONG>(badge_text_area.GetRight() - rpx(10)),
            static_cast<LONG>(badge_text_area.GetBottom())};
        draw_text(
            device,
            view_.badge,
            badge_text,
            kTextGdi,
            badge_font_,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT card_title{
            static_cast<LONG>(card.X + inner),
            static_cast<LONG>(card.Y + rpx(20)),
            static_cast<LONG>(card.GetRight() - inner),
            static_cast<LONG>(card.Y + rpx(44))};
        draw_text(
            device,
            L"Current status",
            card_title,
            kTextGdi,
            title_font_);
        RECT card_subtitle{
            static_cast<LONG>(card.X + inner),
            static_cast<LONG>(card.Y + rpx(47)),
            static_cast<LONG>(card.GetRight() - inner),
            static_cast<LONG>(card.Y + rpx(68))};
        draw_text(
            device,
            view_.paused
                ? L"DuckEggs starts paused. Resume when you are ready."
                : L"Learning stays local; PiP movement is always safety-led.",
            card_subtitle,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

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
                card.X + inner + index * (metric_width + metric_gap);
            RECT label{
                static_cast<LONG>(left + rpx(34)),
                static_cast<LONG>(metric_top + rpx(12)),
                static_cast<LONG>(left + metric_width - rpx(14)),
                static_cast<LONG>(metric_top + rpx(31))};
            draw_text(
                device,
                labels[static_cast<std::size_t>(index)],
                label,
                kSecondaryGdi,
                small_font_,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT value{
                static_cast<LONG>(left + rpx(16)),
                static_cast<LONG>(metric_top + rpx(39)),
                static_cast<LONG>(left + metric_width - rpx(14)),
                static_cast<LONG>(metric_top + rpx(66))};
            draw_text(
                device,
                values[static_cast<std::size_t>(index)],
                value,
                kTextGdi,
                title_font_,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            RECT detail{
                static_cast<LONG>(left + rpx(16)),
                static_cast<LONG>(metric_top + rpx(69)),
                static_cast<LONG>(left + metric_width - rpx(14)),
                static_cast<LONG>(metric_bottom - rpx(12))};
            draw_text(
                device,
                details[static_cast<std::size_t>(index)],
                detail,
                kSecondaryGdi,
                small_font_,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        }

        if (!view_.error.empty()) {
            RECT error_text{
                static_cast<LONG>(card.X + inner + rpx(16)),
                static_cast<LONG>(card.GetBottom() - inner - rpx(52)),
                static_cast<LONG>(card.GetRight() - inner - rpx(16)),
                static_cast<LONG>(card.GetBottom() - inner - rpx(8))};
            draw_text(
                device,
                view_.error,
                error_text,
                kErrorGdi,
                small_font_,
                DT_LEFT | DT_VCENTER | DT_WORDBREAK);
        }
    }

    void draw_button(const DRAWITEMSTRUCT& item) {
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

        Graphics graphics(item.hDC);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        const RectF bounds = rectangle(item.rcItem);
        ARGB fill = kSurfaceRaised;
        ARGB border = kBorderStrong;
        ARGB foreground = kText;
        COLORREF foreground_gdi = kTextGdi;
        if (item.CtlID == kPauseButton) {
            fill = selected ? 0xff5fc9b2 : kAccent;
            border = fill;
            foreground = 0xff0d1718;
            foreground_gdi = RGB(13, 23, 24);
            if (hovered && !selected) {
                fill = 0xff8ce7d2;
            }
        } else if (item.CtlID == kLearningButton) {
            fill = hovered ? 0xff2a3544 : kSurfaceRaised;
            border = hovered ? kAccent : kBorderStrong;
        } else if (item.CtlID == kExitButton) {
            fill = hovered ? kSurfaceRaised : kSurface;
            border = hovered ? kBorderStrong : kSurface;
            foreground = kSecondary;
            foreground_gdi = kSecondaryGdi;
        }
        if ((item.itemState & ODS_DISABLED) != 0) {
            foreground = kMuted;
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

    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam) {
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
            info->ptMinTrackSize.x = px(620);
            info->ptMinTrackSize.y = px(520);
            return 0;
        }
        case WM_HSCROLL:
        case WM_VSCROLL:
            return 0;
        case kHoverMessage: {
            const int control = static_cast<int>(wparam);
            const bool entered = lparam != 0;
            if (entered) {
                hovered_control_ = control;
            } else if (hovered_control_ == control) {
                hovered_control_ = 0;
            }
            HWND control_window = GetDlgItem(window_, control);
            if (control_window != nullptr) {
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
            paint_main(target);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_CLOSE:
            application_.request_quit();
            DestroyWindow(window_);
            return 0;
        case WM_DESTROY:
            KillTimer(window_, kPanelTimer);
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
    HWND exit_button_{};
    HFONT heading_font_{};
    HFONT title_font_{};
    HFONT body_font_{};
    HFONT semibold_font_{};
    HFONT small_font_{};
    HFONT badge_font_{};
    HBRUSH background_brush_{};
    UINT dpi_{96};
    int hovered_control_{};
    MainView view_;

    friend class HeatmapWindow;
};

struct HeatmapLayout {
    RectF projection;
    bool has_projection{};
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
            MulDiv(1120, static_cast<int>(dpi_), 96),
            MulDiv(820, static_cast<int>(dpi_), 96),
            owner_,
            nullptr,
            GetModuleHandleW(nullptr),
            this);
        if (window_ == nullptr) {
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

    void create_fonts() {
        destroy_fonts();
        heading_font_ = make_font(dpi_, 29, FW_SEMIBOLD);
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
        SendMessageW(activity_, BM_SETCHECK, BST_CHECKED, 0);
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
        const REAL margin = rpx(28);
        REAL content_top = rpx(104);
        const REAL full_width =
            std::max<REAL>(0, client.right - margin * 2.0F);
        result.has_projection = projection_visible();
        if (result.has_projection) {
            result.projection =
                RectF(margin, content_top, full_width, rpx(58));
            content_top += rpx(74);
        }
        const REAL content_bottom =
            static_cast<REAL>(client.bottom) - margin;
        const REAL sidebar_width = rpx(286);
        result.sidebar = RectF(
            margin,
            content_top,
            sidebar_width,
            std::max<REAL>(0, content_bottom - content_top));
        result.canvas = RectF(
            margin + sidebar_width + rpx(18),
            content_top,
            std::max<REAL>(
                0,
                client.right - margin -
                    (margin + sidebar_width + rpx(18))),
            std::max<REAL>(0, content_bottom - content_top));

        result.selector_label = RectF(
            result.sidebar.X + rpx(20),
            result.sidebar.Y + rpx(18),
            result.sidebar.Width - rpx(40),
            rpx(20));
        result.combo = RectF(
            result.sidebar.X + rpx(20),
            result.sidebar.Y + rpx(43),
            result.sidebar.Width - rpx(40),
            rpx(34));
        result.layer_label = RectF(
            result.sidebar.X + rpx(20),
            result.sidebar.Y + rpx(91),
            result.sidebar.Width - rpx(40),
            rpx(20));
        result.layer_group = RectF(
            result.sidebar.X + rpx(16),
            result.sidebar.Y + rpx(114),
            result.sidebar.Width - rpx(32),
            rpx(128));
        result.legend_label = RectF(
            result.sidebar.X + rpx(20),
            result.sidebar.Y + rpx(258),
            result.sidebar.Width - rpx(40),
            rpx(20));
        result.legend = RectF(
            result.sidebar.X + rpx(20),
            result.sidebar.Y + rpx(282),
            result.sidebar.Width - rpx(40),
            rpx(12));
        const REAL outline_top =
            result.sidebar.GetBottom() - rpx(126);
        result.detail = RectF(
            result.sidebar.X + rpx(20),
            result.sidebar.Y + rpx(332),
            result.sidebar.Width - rpx(40),
            std::max<REAL>(
                rpx(58),
                outline_top -
                    (result.sidebar.Y + rpx(332)) - rpx(16)));
        result.outlines = RectF(
            result.sidebar.X + rpx(20),
            outline_top,
            result.sidebar.Width - rpx(40),
            rpx(68));
        result.learning = RectF(
            result.sidebar.X + rpx(20),
            result.sidebar.GetBottom() - rpx(50),
            result.sidebar.Width - rpx(40),
            rpx(32));

        result.hover = RectF(
            result.canvas.X + rpx(20),
            result.canvas.Y + rpx(18),
            result.canvas.Width - rpx(40),
            rpx(44));
        result.status = RectF(
            result.canvas.X + rpx(20),
            result.canvas.GetBottom() - rpx(38),
            result.canvas.Width - rpx(40),
            rpx(24));
        REAL grid_left = result.canvas.X + rpx(58);
        REAL grid_top = result.hover.GetBottom() + rpx(22);
        REAL grid_right = result.canvas.GetRight() - rpx(24);
        REAL grid_bottom = result.status.Y - rpx(16);
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
        MoveWindow(
            profile_,
            static_cast<int>(areas.combo.X),
            static_cast<int>(areas.combo.Y),
            static_cast<int>(areas.combo.Width),
            px(250),
            TRUE);
        const REAL segment_gap = rpx(4);
        const REAL segment_height =
            (areas.layer_group.Height - segment_gap * 2.0F) / 3.0F;
        HWND controls[3]{activity_, manual_, placement_};
        for (int index = 0; index < 3; ++index) {
            MoveWindow(
                controls[index],
                static_cast<int>(areas.layer_group.X) + px(4),
                static_cast<int>(
                    areas.layer_group.Y + rpx(4) +
                    index * (segment_height + segment_gap)),
                static_cast<int>(areas.layer_group.Width) - px(8),
                static_cast<int>(segment_height),
                TRUE);
        }
        InvalidateRect(window_, nullptr, FALSE);
    }

    void updated_bridge() {
        bridge_ = application_.insights_snapshot();
        const Json candidate =
            bridge_.contains("data") ? bridge_["data"] : Json(nullptr);
        if (candidate.is_object() &&
            heatmap::bridge_data_matches_selection(
                candidate, selected_profile_)) {
            data_ = candidate;
            const auto resolved = heatmap::resolve_profile_key(
                data_, selected_profile_);
            if (!selected_profile_.has_value() &&
                resolved.has_value()) {
                selected_profile_ = resolved;
                application_.request_insights(selected_profile_);
            }
            update_profile_choices();
        } else if (!selected_profile_.has_value() &&
                   candidate.is_object()) {
            data_ = candidate;
            const auto resolved = heatmap::resolve_profile_key(
                data_, selected_profile_);
            if (resolved.has_value()) {
                selected_profile_ = resolved;
                update_profile_choices();
            }
        } else if (candidate.is_object()) {
            data_ = Json(nullptr);
        }
        layout_controls();
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

    std::string layer_key() const {
        if (placement_ != nullptr &&
            SendMessageW(placement_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            return "placement";
        }
        if (manual_ != nullptr &&
            SendMessageW(manual_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            return "manual";
        }
        return "activity";
    }

    void select_layer(int identifier) {
        for (const auto [control, id] :
             {std::pair<HWND, int>{activity_, kActivityLayer},
              std::pair<HWND, int>{manual_, kManualLayer},
              std::pair<HWND, int>{placement_, kPlacementLayer}}) {
            SendMessageW(
                control,
                BM_SETCHECK,
                id == identifier ? BST_CHECKED : BST_UNCHECKED,
                0);
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
                graphics, areas.projection, rpx(15), color(kWarmSoft));
            stroke_round_rect(
                graphics,
                areas.projection,
                rpx(15),
                with_alpha(kWarm, 80));
        }
        fill_round_rect(
            graphics, areas.sidebar, rpx(17), color(kSurface));
        stroke_round_rect(
            graphics, areas.sidebar, rpx(17), color(kBorder));
        fill_round_rect(
            graphics, areas.canvas, rpx(17), color(kSurface));
        stroke_round_rect(
            graphics, areas.canvas, rpx(17), color(kBorder));
        fill_round_rect(
            graphics, areas.layer_group, rpx(12), color(kBackground));
        stroke_round_rect(
            graphics, areas.layer_group, rpx(12), color(kBorder));

        const REAL egg_size = rpx(40);
        draw_egg_mark(
            graphics,
            RectF(rpx(28), rpx(24), egg_size, egg_size + rpx(4)),
            rpx(1.6));

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
                        rpx(1.6));
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
                rpx(1.8));
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
            SolidBrush brush(Color(255, red, green, blue));
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
                std::stoi(
                    std::string(heatmap::kUnknownColor).substr(1, 2),
                    nullptr,
                    16),
                std::stoi(
                    std::string(heatmap::kUnknownColor).substr(3, 2),
                    nullptr,
                    16),
                std::stoi(
                    std::string(heatmap::kUnknownColor).substr(5, 2),
                    nullptr,
                    16)));
        graphics.FillRectangle(
            &unknown,
            RectF(
                areas.legend.X,
                areas.legend.GetBottom() + rpx(16),
                rpx(16),
                rpx(12)));
        graphics.Flush();

        RECT title{
            px(82),
            px(22),
            static_cast<LONG>(static_cast<REAL>(areas.canvas.X) -
                              rpx(8)),
            px(61)};
        draw_text(
            device,
            L"Learning maps",
            title,
            kTextGdi,
            heading_font_);
        RECT subtitle{
            px(84),
            px(62),
            static_cast<LONG>(static_cast<REAL>(areas.canvas.X) +
                              areas.canvas.Width - rpx(20)),
            px(86)};
        draw_text(
            device,
            L"Read-only learning for the selected application and layout.",
            subtitle,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (areas.has_projection) {
            RECT projection{
                static_cast<LONG>(areas.projection.X + rpx(18)),
                static_cast<LONG>(areas.projection.Y + rpx(9)),
                static_cast<LONG>(
                    areas.projection.GetRight() - rpx(18)),
                static_cast<LONG>(
                    areas.projection.GetBottom() - rpx(7))};
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
            L"Application / layout",
            selector_label,
            kSecondaryGdi,
            small_font_);
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
            small_font_);

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
            static_cast<LONG>(areas.legend.GetBottom() + rpx(17)),
            static_cast<LONG>(areas.legend.X + rpx(120)),
            static_cast<LONG>(areas.legend.GetBottom() + rpx(34))};
        RECT high{
            static_cast<LONG>(areas.legend.GetRight() - rpx(120)),
            static_cast<LONG>(areas.legend.GetBottom() + rpx(17)),
            static_cast<LONG>(areas.legend.GetRight()),
            static_cast<LONG>(areas.legend.GetBottom() + rpx(34))};
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
            static_cast<LONG>(areas.legend.GetBottom() + rpx(13)),
            static_cast<LONG>(areas.legend.GetRight()),
            static_cast<LONG>(areas.legend.GetBottom() + rpx(34))};
        draw_text(
            device,
            L"Unknown / unavailable (gray)",
            unknown_text,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT detail{
            static_cast<LONG>(areas.detail.X),
            static_cast<LONG>(areas.detail.Y),
            static_cast<LONG>(areas.detail.GetRight()),
            static_cast<LONG>(areas.detail.GetBottom())};
        draw_text(
            device,
            wide(spec.meaning),
            detail,
            kSecondaryGdi,
            small_font_,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

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
            L"Violet: foreground  \u00b7  amber: current PiP  \u00b7  "
            L"teal: recommendation  \u00b7  white dashed: full PiP",
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
            DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                DT_END_ELLIPSIS);

        if (data_.is_object() &&
            heatmap::profile_choices(data_).empty()) {
            const REAL overlay_width =
                std::min<REAL>(rpx(440), areas.grid.Width - rpx(32));
            const RectF overlay{
                areas.grid.X +
                    (areas.grid.Width - overlay_width) / 2.0F,
                areas.grid.Y + areas.grid.Height * 0.31F,
                overlay_width,
                rpx(106)};
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

    void draw_button(const DRAWITEMSTRUCT& item) {
        const bool selected =
            SendMessageW(
                item.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const bool hovered =
            hovered_control_ == static_cast<int>(item.CtlID);
        const bool focused = (item.itemState & ODS_FOCUS) != 0;
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
        draw_text(
            item.hDC,
            text_buffer,
            item.rcItem,
            selected ? kTextGdi : kSecondaryGdi,
            semibold_font_,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    void draw_combo_item(const DRAWITEMSTRUCT& item) {
        const bool selected =
            (item.itemState & ODS_SELECTED) != 0;
        Graphics graphics(item.hDC);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
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
            info->ptMinTrackSize.x = px(960);
            info->ptMinTrackSize.y = px(780);
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
            if (entered) {
                hovered_control_ = control;
            } else if (hovered_control_ == control) {
                hovered_control_ = 0;
            }
            HWND control_window = GetDlgItem(window_, control);
            if (control_window != nullptr) {
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
            RECT client{};
            GetClientRect(window_, &client);
            HDC buffer = CreateCompatibleDC(target);
            HBITMAP bitmap = CreateCompatibleBitmap(
                target, client.right, client.bottom);
            const HGDIOBJ previous =
                SelectObject(buffer, bitmap);
            draw_backgrounds(buffer);
            draw_foreground_layer(buffer, layout(), layer_key());
            BitBlt(
                target,
                0,
                0,
                client.right,
                client.bottom,
                buffer,
                0,
                0,
                SRCCOPY);
            SelectObject(buffer, previous);
            DeleteObject(bitmap);
            DeleteDC(buffer);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;
        case WM_NCDESTROY: {
            KillTimer(window_, kHeatmapTimer);
            const HWND destroyed = window_;
            window_ = nullptr;
            panel_.heatmap_destroyed(destroyed);
            const LRESULT result =
                DefWindowProcW(window, message, wparam, lparam);
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
    std::vector<heatmap::ProfileChoice> choices_;
    std::string profile_signature_;
    Json bridge_ = Json::object();
    Json data_ = Json(nullptr);
    std::optional<std::pair<int, int>> hover_cell_;
};

ControlPanel::~ControlPanel() {
    if (heatmap_ != nullptr) {
        DestroyWindow(heatmap_->window());
        heatmap_ = nullptr;
    }
    if (background_brush_ != nullptr) {
        DeleteObject(background_brush_);
    }
    destroy_fonts();
}

void ControlPanel::heatmap_destroyed(HWND window) {
    if (heatmap_ != nullptr && heatmap_->window() == window) {
        heatmap_ = nullptr;
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
            MulDiv(780, static_cast<int>(dpi), 96),
            MulDiv(560, static_cast<int>(dpi), 96),
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
