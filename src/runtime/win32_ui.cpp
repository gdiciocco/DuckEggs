#include "ui.hpp"

#ifdef _WIN32

#include "schiva/heatmap.hpp"
#include "schiva/runtime.hpp"

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace schiva {
namespace {

constexpr wchar_t kPanelClass[] = L"SchivaCppControlPanel";
constexpr wchar_t kHeatmapClass[] = L"SchivaCppHeatmap";
constexpr int kPauseButton = 1001;
constexpr int kLearningButton = 1002;
constexpr int kExitButton = 1003;
constexpr int kProfileCombo = 1101;
constexpr int kActivityLayer = 1111;
constexpr int kManualLayer = 1112;
constexpr int kPlacementLayer = 1113;
constexpr UINT kPanelTimer = 1;
constexpr UINT kHeatmapTimer = 2;

constexpr COLORREF kBackground = RGB(246, 245, 241);
constexpr COLORREF kPanelBackground = RGB(235, 233, 227);
constexpr COLORREF kText = RGB(36, 40, 44);
constexpr COLORREF kMuted = RGB(94, 100, 106);
constexpr COLORREF kError = RGB(161, 40, 31);
constexpr COLORREF kProjection = RGB(255, 240, 199);
constexpr COLORREF kProjectionText = RGB(94, 66, 0);
constexpr COLORREF kForegroundOutline = RGB(154, 85, 180);
constexpr COLORREF kPipOutline = RGB(217, 119, 6);
constexpr COLORREF kRecommendationOutline = RGB(8, 127, 91);
constexpr COLORREF kFootprintOutline = RGB(15, 111, 158);

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

COLORREF parse_color(std::string_view color) {
    auto part = [](std::string_view value) {
        int result = 0;
        for (const char character : value) {
            result *= 16;
            if (character >= '0' && character <= '9') {
                result += character - '0';
            } else if (character >= 'a' && character <= 'f') {
                result += character - 'a' + 10;
            } else if (character >= 'A' && character <= 'F') {
                result += character - 'A' + 10;
            }
        }
        return result;
    };
    if (color.size() != 7) {
        return RGB(212, 215, 218);
    }
    return RGB(
        part(color.substr(1, 2)),
        part(color.substr(3, 2)),
        part(color.substr(5, 2)));
}

void fill(HDC device, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(device, &rect, brush);
    DeleteObject(brush);
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
    HGDIOBJ old = SelectObject(device, font);
    DrawTextW(
        device,
        text.data(),
        static_cast<int>(text.size()),
        &rect,
        format | DT_NOPREFIX);
    SelectObject(device, old);
}

void draw_outline(
    HDC device,
    const RECT& rect,
    COLORREF color,
    int style = PS_SOLID) {
    HPEN pen = CreatePen(style, 2, color);
    HGDIOBJ old = SelectObject(device, pen);
    HGDIOBJ old_brush = SelectObject(
        device, GetStockObject(NULL_BRUSH));
    Rectangle(device, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(device, old_brush);
    SelectObject(device, old);
    DeleteObject(pen);
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

RECT normalized_rect(
    const heatmap::NormalizedRect& value,
    const RECT& grid) {
    const double width =
        static_cast<double>(grid.right - grid.left);
    const double height =
        static_cast<double>(grid.bottom - grid.top);
    return RECT{
        grid.left + static_cast<LONG>(value.left * width),
        grid.top + static_cast<LONG>(value.top * height),
        grid.left + static_cast<LONG>(value.right * width),
        grid.top + static_cast<LONG>(value.bottom * height)};
}

class HeatmapWindow;

class ControlPanel {
public:
    ControlPanel(HWND window, AdaptiveApplication& application)
        : window_(window), application_(application) {
        title_font_ = CreateFontW(
            -19, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        body_font_ = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        small_font_ = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        create_controls();
        SetTimer(window_, kPanelTimer, 150, nullptr);
        refresh();
    }

    ~ControlPanel() {
        if (heatmap_ != nullptr) {
            DestroyWindow(heatmap_->window());
            heatmap_ = nullptr;
        }
        DeleteObject(title_font_);
        DeleteObject(body_font_);
        DeleteObject(small_font_);
    }

    void heatmap_destroyed(HWND window) {
        if (heatmap_ != nullptr && heatmap_->window() == window) {
            heatmap_ = nullptr;
        }
    }

    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam);

private:
    void create_controls() {
        static_button_ = CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            18, 16, 390, 26, window_, nullptr, nullptr, nullptr);
        learning_label_ = CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            18, 49, 390, 22, window_, nullptr, nullptr, nullptr);
        visual_label_ = CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            18, 74, 390, 22, window_, nullptr, nullptr, nullptr);
        state_label_ = CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            18, 99, 390, 22, window_, nullptr, nullptr, nullptr);
        pause_button_ = CreateWindowExW(
            0, L"BUTTON", L"Pausa",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            18, 137, 112, 34, window_,
            reinterpret_cast<HMENU>(kPauseButton), nullptr, nullptr);
        learning_button_ = CreateWindowExW(
            0, L"BUTTON", L"Apprendimento...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            140, 137, 150, 34, window_,
            reinterpret_cast<HMENU>(kLearningButton), nullptr, nullptr);
        exit_button_ = CreateWindowExW(
            0, L"BUTTON", L"Esci",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            300, 137, 108, 34, window_,
            reinterpret_cast<HMENU>(kExitButton), nullptr, nullptr);
        SendMessageW(static_button_, WM_SETFONT,
                     reinterpret_cast<WPARAM>(title_font_), TRUE);
        for (HWND label :
             {learning_label_, visual_label_, state_label_}) {
            SendMessageW(label, WM_SETFONT,
                         reinterpret_cast<WPARAM>(body_font_), TRUE);
        }
        for (HWND button :
             {pause_button_, learning_button_, exit_button_}) {
            SendMessageW(button, WM_SETFONT,
                         reinterpret_cast<WPARAM>(body_font_), TRUE);
        }
    }

    void refresh() {
        const Json status = application_.snapshot();
        const bool paused =
            status.contains("paused") && status["paused"].is_boolean() &&
            status["paused"].get<bool>();
        const std::string state = string_field(status, "state")
            .value_or("avvio");
        const int learning_count = status.contains("learning_count")
            ? status["learning_count"].is_number_integer()
                  ? status["learning_count"].get<int>()
                  : 0
            : 0;
        SetWindowTextW(
            static_button_,
            paused ? L"Schiva adattivo - in pausa"
                   : L"Schiva adattivo");
        SetWindowTextW(
            learning_label_,
            (L"Apprendimento: " + std::to_wstring(learning_count) +
             L" esempi").c_str());
        const bool capture_error =
            status.contains("capture_error") &&
            !status["capture_error"].is_null();
        const bool visual =
            status.contains("visual_available") &&
            status["visual_available"].is_boolean() &&
            status["visual_available"].get<bool>();
        SetWindowTextW(
            visual_label_,
            capture_error
                ? L"Schermo: non disponibile, app ancora attiva"
                : visual ? L"Schermo: disponibile"
                         : L"Schermo: disattivato o in attesa");
        SetWindowTextW(
            state_label_,
            (L"Stato: " + wide(state)).c_str());
        SetWindowTextW(
            pause_button_, paused ? L"Riprendi" : L"Pausa");
        InvalidateRect(window_, nullptr, TRUE);
    }

    void open_heatmap() {
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
                wide(exception.what()).c_str(),
                L"Schiva adattivo",
                MB_OK | MB_ICONERROR);
        }
    }

    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
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
        case WM_CLOSE:
            application_.request_quit();
            DestroyWindow(window_);
            return 0;
        case WM_DESTROY:
            KillTimer(window_, kPanelTimer);
            PostQuitMessage(0);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC device = BeginPaint(window_, &paint);
            RECT client{};
            GetClientRect(window_, &client);
            fill(device, client, kBackground);
            EndPaint(window_, &paint);
            return 0;
        }
        default:
            break;
        }
        return DefWindowProcW(window_, message, wparam, lparam);
    }

    HWND window_{};
    AdaptiveApplication& application_;
    HeatmapWindow* heatmap_{};
    HWND static_button_{};
    HWND learning_label_{};
    HWND visual_label_{};
    HWND state_label_{};
    HWND pause_button_{};
    HWND learning_button_{};
    HWND exit_button_{};
    HFONT title_font_{};
    HFONT body_font_{};
    HFONT small_font_{};

    friend class HeatmapWindow;
};

class HeatmapWindow {
public:
    HeatmapWindow(
        AdaptiveApplication& application,
        HWND owner,
        ControlPanel& panel)
        : application_(application), owner_(owner), panel_(panel) {
        title_font_ = CreateFontW(
            -24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        body_font_ = CreateFontW(
            -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        small_font_ = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        window_ = CreateWindowExW(
            WS_EX_APPWINDOW,
            kHeatmapClass,
            L"Schiva adattivo - Apprendimento",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            960,
            700,
            owner_,
            nullptr,
            GetModuleHandleW(nullptr),
            this);
        if (window_ == nullptr) {
            throw PlatformError("the learning viewer could not start");
        }
    }

    ~HeatmapWindow() {
        if (window_ != nullptr) {
            KillTimer(window_, kHeatmapTimer);
        }
        DeleteObject(title_font_);
        DeleteObject(body_font_);
        DeleteObject(small_font_);
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
    void create_controls() {
        profile_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                WS_VSCROLL,
            16, 126, 238, 220, window_,
            reinterpret_cast<HMENU>(kProfileCombo), nullptr, nullptr);
        activity_ = CreateWindowExW(
            0, L"BUTTON", L"Uso delle aree",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON |
                WS_GROUP,
            16, 174, 220, 24, window_,
            reinterpret_cast<HMENU>(kActivityLayer), nullptr, nullptr);
        manual_ = CreateWindowExW(
            0, L"BUTTON", L"Preferenze manuali",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
            16, 200, 220, 24, window_,
            reinterpret_cast<HMENU>(kManualLayer), nullptr, nullptr);
        placement_ = CreateWindowExW(
            0, L"BUTTON", L"Posizionamento stimato",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
            16, 226, 220, 24, window_,
            reinterpret_cast<HMENU>(kPlacementLayer), nullptr, nullptr);
        SendMessageW(activity_, BM_SETCHECK, BST_CHECKED, 0);
        for (HWND control : {profile_, activity_, manual_, placement_}) {
            SendMessageW(control, WM_SETFONT,
                         reinterpret_cast<WPARAM>(body_font_), TRUE);
        }
        SetTimer(window_, kHeatmapTimer, 1000, nullptr);
        application_.request_insights(selected_profile_);
    }

    void updated_bridge() {
        bridge_ = application_.insights_snapshot();
        const Json data = bridge_.contains("data")
            ? bridge_["data"]
            : Json(nullptr);
        if (data.is_object() &&
            heatmap::bridge_data_matches_selection(
                data, selected_profile_)) {
            data_ = data;
            const auto resolved = heatmap::resolve_profile_key(
                data_, selected_profile_);
            if (!selected_profile_.has_value() &&
                resolved.has_value()) {
                selected_profile_ = resolved;
                application_.request_insights(selected_profile_);
            }
            update_profile_choices();
        } else if (!selected_profile_.has_value() &&
                   data.is_object()) {
            data_ = data;
            const auto resolved = heatmap::resolve_profile_key(
                data_, selected_profile_);
            if (resolved.has_value()) {
                selected_profile_ = resolved;
            }
        } else if (data.is_object()) {
            data_ = Json(nullptr);
        }
        InvalidateRect(window_, nullptr, FALSE);
    }

    void update_profile_choices() {
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
        }
        if (selected >= 0) {
            SendMessageW(profile_, CB_SETCURSEL, selected, 0);
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

    RECT grid_box(const RECT& client) const {
        return RECT{
            std::max(290L, client.left + 290),
            client.top + 130,
            std::max(client.left + 330, client.right - 24),
            std::max(client.top + 180, client.bottom - 64)};
    }

    void update_hover(int x, int y) {
        if (!data_.is_object()) {
            hover_cell_.reset();
            return;
        }
        RECT client{};
        GetClientRect(window_, &client);
        const RECT grid = grid_box(client);
        if (x < grid.left || x >= grid.right ||
            y < grid.top || y >= grid.bottom) {
            if (hover_cell_.has_value()) {
                hover_cell_.reset();
                InvalidateRect(window_, nullptr, FALSE);
            }
            return;
        }
        const auto [columns, rows] =
            heatmap::grid_dimensions(data_);
        const int column = std::min(
            columns - 1,
            (x - grid.left) * columns / (grid.right - grid.left));
        const int row = std::min(
            rows - 1,
            (y - grid.top) * rows / (grid.bottom - grid.top));
        const std::pair<int, int> cell{column, row};
        if (cell != hover_cell_) {
            hover_cell_ = cell;
            InvalidateRect(window_, nullptr, FALSE);
        }
    }

    void paint_contents(HDC device) {
        RECT client{};
        GetClientRect(window_, &client);
        fill(device, client, kBackground);

        RECT title{16, 12, client.right - 16, 42};
        draw_text(
            device,
            L"Apprendimento e posizionamento",
            title,
            kText,
            title_font_);
        RECT subtitle{16, 43, client.right - 16, 64};
        draw_text(
            device,
            L"Mappe locali e relative. Nessuno screenshot viene mostrato "
            L"o salvato da questa vista.",
            subtitle,
            kMuted,
            small_font_,
            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

        int content_top = 72;
        if (data_.is_object() &&
            data_.contains("projection") &&
            data_["projection"].is_boolean() &&
            data_["projection"].get<bool>()) {
            RECT banner{16, content_top, client.right - 16,
                        content_top + 44};
            fill(device, banner, kProjection);
            RECT text = banner;
            text.left += 10;
            text.right -= 10;
            text.top += 5;
            draw_text(
                device,
                L"Proiezione non live - assunzioni: app inattiva estesa "
                L"all'intera area di lavoro; modello globale condiviso; "
                L"geometria e PiP correnti; nessuna vista dal vivo.",
                text,
                kProjectionText,
                small_font_,
                DT_LEFT | DT_TOP | DT_WORDBREAK);
            content_top += 54;
        }

        RECT sidebar{10, content_top + 10, 270, client.bottom - 34};
        fill(device, sidebar, kPanelBackground);
        RECT sidebar_heading{18, content_top + 16, 260,
                             content_top + 38};
        draw_text(
            device,
            L"Applicazione e layout",
            sidebar_heading,
            kText,
            body_font_);

        RECT legend{18, 238, 260, 280};
        const std::string key = layer_key();
        for (int step = 0; step < 24; ++step) {
            RECT block{
                legend.left + step * 10,
                legend.top,
                legend.left + (step + 1) * 10 + 1,
                legend.top + 14};
            fill(
                device,
                block,
                parse_color(heatmap::layer_color(
                    key, static_cast<double>(step) / 23.0)));
        }
        RECT low{legend.left, legend.top + 17, legend.left + 50,
                 legend.top + 34};
        RECT high{legend.right - 50, legend.top + 17, legend.right,
                  legend.top + 34};
        draw_text(device, L"basso", low, kMuted, small_font_);
        draw_text(device, L"alto", high, kMuted, small_font_,
                  DT_RIGHT | DT_TOP | DT_SINGLELINE);
        RECT unknown_block{legend.left, legend.top + 35,
                           legend.left + 14, legend.top + 49};
        fill(device, unknown_block, parse_color(heatmap::kUnknownColor));
        RECT unknown_text{legend.left + 20, legend.top + 32,
                          legend.right, legend.top + 50};
        draw_text(device, L"sconosciuto (grigio)", unknown_text,
                  kMuted, small_font_);
        RECT meaning{18, 280, 260, 378};
        draw_text(
            device,
            wide(heatmap::layer_spec(key).meaning),
            meaning,
            kMuted,
            small_font_,
            DT_LEFT | DT_TOP | DT_WORDBREAK);

        RECT learning{18, 382, 260, 430};
        const std::wstring learning_text = data_.is_object()
            ? wide(heatmap::learning_summary(data_))
            : L"Decisioni profilo: n/d - apprendimenti globali: n/d";
        draw_text(device, learning_text, learning, kMuted, small_font_,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
        RECT outline_help{18, 434, 260, 510};
        draw_text(
            device,
            L"Contorni: viola = finestra in primo piano; arancio = PiP "
            L"attuale; verde = destinazione consigliata; blu tratteggiato = "
            L"PiP completo.",
            outline_help,
            kMuted,
            small_font_,
            DT_LEFT | DT_TOP | DT_WORDBREAK);

        RECT hover_text{286, content_top + 92, client.right - 24,
                        content_top + 116};
        if (hover_cell_.has_value() && data_.is_object()) {
            const auto [columns, rows] =
                heatmap::grid_dimensions(data_);
            const auto [column, row] = *hover_cell_;
            const auto value = heatmap::layer_value(
                data_,
                key,
                static_cast<std::size_t>(row * columns + column));
            std::wstring message =
                L"Posizione " +
                std::to_wstring(
                    static_cast<int>(
                        (column + 0.5) / columns * 100.0)) +
                L"% x " +
                std::to_wstring(
                    static_cast<int>(
                        (row + 0.5) / rows * 100.0)) +
                L"% - valore " +
                (value.has_value()
                     ? std::to_wstring(
                           static_cast<int>(*value * 100.0 + 0.5)) +
                           L"/100"
                     : L"sconosciuto");
            if (key == "placement") {
                const auto pip_size = heatmap::snapshot_pip_size(data_);
                if (pip_size.has_value()) {
                    message +=
                        L" - PiP completo " +
                        std::to_wstring(static_cast<int>(
                            pip_size->first * 100.0 + 0.5)) +
                        L"% x " +
                        std::to_wstring(static_cast<int>(
                            pip_size->second * 100.0 + 0.5)) +
                        L"%";
                }
            }
            draw_text(device, message, hover_text, kText, small_font_,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            draw_text(
                device,
                L"Passa sopra una cella per posizione, valore e impronta "
                L"del PiP.",
                hover_text,
                kText,
                small_font_,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        RECT grid = grid_box(client);
        if (data_.is_object() && grid.right > grid.left &&
            grid.bottom > grid.top) {
            const auto [columns, rows] =
                heatmap::grid_dimensions(data_);
            const double cell_width =
                static_cast<double>(grid.right - grid.left) / columns;
            const double cell_height =
                static_cast<double>(grid.bottom - grid.top) / rows;
            for (int row = 0; row < rows; ++row) {
                for (int column = 0; column < columns; ++column) {
                    const auto value = heatmap::layer_value(
                        data_, key,
                        static_cast<std::size_t>(row * columns + column));
                    RECT cell{
                        grid.left + static_cast<LONG>(column * cell_width),
                        grid.top + static_cast<LONG>(row * cell_height),
                        grid.left +
                            static_cast<LONG>((column + 1) * cell_width),
                        grid.top +
                            static_cast<LONG>((row + 1) * cell_height)};
                    fill(
                        device,
                        cell,
                        parse_color(heatmap::layer_color(
                            key,
                            value.has_value() ? Json(*value)
                                              : Json(nullptr))));
                }
            }
            for (int fraction = 0; fraction <= 1; ++fraction) {
                const int x = grid.left +
                    (grid.right - grid.left) * fraction / 2;
                const int y = grid.top +
                    (grid.bottom - grid.top) * fraction / 2;
                RECT x_label{x - 20, grid.top - 20, x + 20, grid.top - 2};
                RECT y_label{grid.left - 34, y - 8, grid.left - 4, y + 10};
                draw_text(
                    device,
                    std::to_wstring(fraction * 50) + L"%",
                    x_label,
                    kMuted,
                    small_font_,
                    DT_CENTER | DT_TOP | DT_SINGLELINE);
                draw_text(
                    device,
                    std::to_wstring(fraction * 50) + L"%",
                    y_label,
                    kMuted,
                    small_font_,
                    DT_RIGHT | DT_TOP | DT_SINGLELINE);
            }
            draw_geometry(device, grid, columns, rows);
            if (hover_cell_.has_value()) {
                const auto [column, row] = *hover_cell_;
                if (column < columns && row < rows) {
                    RECT cell{
                        grid.left + static_cast<LONG>(column * cell_width),
                        grid.top + static_cast<LONG>(row * cell_height),
                        grid.left +
                            static_cast<LONG>((column + 1) * cell_width),
                        grid.top +
                            static_cast<LONG>((row + 1) * cell_height)};
                    draw_outline(device, cell, RGB(17, 17, 17));
                    if (key == "placement") {
                        const auto pip_size =
                            heatmap::snapshot_pip_size(data_);
                        if (pip_size.has_value()) {
                            const Json raw_size = Json::array(
                                {pip_size->first, pip_size->second});
                            if (const auto footprint =
                                    heatmap::candidate_footprint(
                                        column,
                                        row,
                                        columns,
                                        rows,
                                        raw_size)) {
                                draw_outline(
                                    device,
                                    normalized_rect(*footprint, grid),
                                    kFootprintOutline,
                                    PS_DASH);
                            }
                        }
                    }
                }
            }
        }

        RECT message{286, client.bottom - 42, client.right - 24,
                     client.bottom - 12};
        std::wstring status;
        COLORREF status_color = kMuted;
        if (bridge_.contains("error") &&
            bridge_["error"].is_string()) {
            status = wide(bridge_["error"].get<std::string>());
            status_color = kError;
        } else if (!data_.is_object()) {
            status = L"Preparazione dei dati locali...";
        } else if (selected_profile_.has_value() &&
                   data_.contains("selected_profile") &&
                   data_["selected_profile"].is_string() &&
                   data_["selected_profile"].get<std::string>() !=
                       *selected_profile_) {
            status = L"In attesa dei dati per il profilo selezionato...";
        } else {
            status = wide(heatmap::profile_status_details(data_)) +
                     L" Aggiornamento ogni secondo.";
            if (const auto cold =
                    heatmap::cold_start_message(data_)) {
                status += L" " + wide(*cold);
            }
        }
        draw_text(
            device,
            status,
            message,
            status_color,
            small_font_,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    }

    void draw_geometry(
        HDC device,
        const RECT& grid,
        int columns,
        int rows) {
        (void)columns;
        (void)rows;
        struct Outline {
            const char* key;
            COLORREF color;
            std::wstring label;
            int style;
        };
        const std::array<Outline, 3> outlines{{
            {"foreground_rect", kForegroundOutline, L"primo piano",
             PS_DASH},
            {"pip_rect", kPipOutline, L"PiP", PS_SOLID},
            {"recommendation_rect", kRecommendationOutline,
             L"consigliata", PS_DOT},
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
            const RECT rect = normalized_rect(*normalized, grid);
            draw_outline(device, rect, outline.color, outline.style);
            RECT label{rect.left + 3, rect.top + 2, rect.left + 120,
                       rect.top + 20};
            draw_text(
                device,
                outline.label,
                label,
                outline.color,
                small_font_,
                DT_LEFT | DT_TOP | DT_SINGLELINE);
        }
    }

    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            create_controls();
            return 0;
        case WM_SIZE:
            if (profile_ != nullptr) {
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = 800;
            info->ptMinTrackSize.y = 600;
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wparam) == kProfileCombo &&
                HIWORD(wparam) == CBN_SELCHANGE) {
                const int selected =
                    static_cast<int>(SendMessageW(
                        profile_, CB_GETCURSEL, 0, 0));
                if (selected >= 0 &&
                    static_cast<std::size_t>(selected) < choices_.size()) {
                    selected_profile_ = choices_[selected].key;
                    application_.request_insights(selected_profile_);
                    InvalidateRect(window_, nullptr, FALSE);
                }
                return 0;
            }
            if (LOWORD(wparam) >= kActivityLayer &&
                LOWORD(wparam) <= kPlacementLayer) {
                InvalidateRect(window_, nullptr, FALSE);
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
        case WM_MOUSEMOVE:
            update_hover(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_MOUSELEAVE:
            if (hover_cell_.has_value()) {
                hover_cell_.reset();
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC device = BeginPaint(window_, &paint);
            paint_contents(device);
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
            LRESULT result =
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
    HFONT title_font_{};
    HFONT body_font_{};
    HFONT small_font_{};
    std::optional<std::string> selected_profile_;
    std::vector<heatmap::ProfileChoice> choices_;
    std::string profile_signature_;
    Json bridge_ = Json::object();
    Json data_ = Json(nullptr);
    std::optional<std::pair<int, int>> hover_cell_;
};

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
            window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(panel));
        panel->window_ = window;
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
            window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(heatmap));
        heatmap->window_ = window;
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
        throw PlatformError("the control-panel window class could not register");
    }

    WNDCLASSEXW heatmap_class{};
    heatmap_class.cbSize = sizeof(heatmap_class);
    heatmap_class.style = CS_HREDRAW | CS_VREDRAW;
    heatmap_class.lpfnWndProc = &HeatmapWindow::window_proc;
    heatmap_class.hInstance = GetModuleHandleW(nullptr);
    heatmap_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    heatmap_class.hbrBackground = nullptr;
    heatmap_class.lpszClassName = kHeatmapClass;
    if (RegisterClassExW(&heatmap_class) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw PlatformError("the heatmap window class could not register");
    }
    registered = true;
}

}  // namespace

bool run_platform_ui(AdaptiveApplication& application) {
    HWND window = nullptr;
    try {
        register_classes();
        window = CreateWindowExW(
            0,
            kPanelClass,
            L"Schiva adattivo",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            440,
            225,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (window == nullptr) {
            return false;
        }
        auto panel = std::make_unique<ControlPanel>(window, application);
        SetWindowLongPtrW(
            window, GWLP_USERDATA,
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
