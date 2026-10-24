#ifdef _WIN32

#include <windows.h>

#include <cwctype>
#include <string>
#include <string_view>

namespace {

constexpr wchar_t kWindowClass[] = L"DuckEggsQaFloatingWindow";
constexpr wchar_t kWindowTitle[] = L"DuckEggs PiP selection test";
constexpr UINT_PTR kAutoExitTimer = 1;
constexpr UINT_PTR kTopmostLabelTimer = 2;
constexpr UINT kAutoExitMilliseconds = 300U * 1000U;
constexpr UINT kTopmostLabelMilliseconds = 100U;
constexpr int kTopmostLabelId = 100;

std::wstring_view topmost_text(HWND window) {
  const LONG_PTR extendedStyle =
      GetWindowLongPtrW(window, GWL_EXSTYLE);
  return (extendedStyle & WS_EX_TOPMOST) != 0
             ? L"WS_EX_TOPMOST: yes"
             : L"WS_EX_TOPMOST: no";
}

void refresh_topmost_label(HWND window) {
  HWND label = GetDlgItem(window, kTopmostLabelId);
  if (label != nullptr) {
    const std::wstring_view text = topmost_text(window);
    SetWindowTextW(label, std::wstring(text).c_str());
  }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      if (SetTimer(window, kAutoExitTimer, kAutoExitMilliseconds, nullptr) == 0) {
        return -1;
      }
      if (SetTimer(window, kTopmostLabelTimer, kTopmostLabelMilliseconds,
                   nullptr) == 0) {
        return -1;
      }
      if (CreateWindowExW(
              0, L"STATIC", L"",
              WS_CHILD | WS_VISIBLE | SS_CENTER,
              24, 164, 312, 24, window, reinterpret_cast<HMENU>(
                  static_cast<INT_PTR>(kTopmostLabelId)),
              reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(
                  window, GWLP_HINSTANCE)),
              nullptr) == nullptr) {
        return -1;
      }
      refresh_topmost_label(window);
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      if (dc != nullptr) {
        RECT client{};
        GetClientRect(window, &client);
        InflateRect(&client, -24, -24);

        HGDIOBJ previousFont =
            SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc,
                  L"Manual selection test window\r\n"
                  L"No video or user data.",
                  -1, &client, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        if (previousFont != nullptr && previousFont != HGDI_ERROR) {
          SelectObject(dc, previousFont);
        }
        EndPaint(window, &paint);
      }
      return 0;
    }

    case WM_TIMER:
      if (wParam == kAutoExitTimer) {
        KillTimer(window, kAutoExitTimer);
        DestroyWindow(window);
      } else if (wParam == kTopmostLabelTimer) {
        refresh_topmost_label(window);
      }
      return 0;

    case WM_CLOSE:
      DestroyWindow(window);
      return 0;

    case WM_DESTROY:
      KillTimer(window, kAutoExitTimer);
      KillTimer(window, kTopmostLabelTimer);
      PostQuitMessage(0);
      return 0;

    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

bool has_topmost_argument(std::wstring_view commandLine) {
  while (!commandLine.empty()) {
    while (!commandLine.empty() &&
           std::iswspace(commandLine.front()) != 0) {
      commandLine.remove_prefix(1);
    }
    std::size_t length = 0;
    while (length < commandLine.size() &&
           std::iswspace(commandLine[length]) == 0) {
      ++length;
    }
    if (commandLine.substr(0, length) == L"--topmost") {
      return true;
    }
    commandLine.remove_prefix(length);
  }
  return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = WindowProc;
  windowClass.hInstance = instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  windowClass.lpszClassName = kWindowClass;

  if (RegisterClassW(&windowClass) == 0) {
    return 1;
  }

  HWND window = CreateWindowExW(
      has_topmost_argument(commandLine) ? WS_EX_TOPMOST : 0,
      kWindowClass, kWindowTitle,
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, 360, 220, nullptr, nullptr, instance,
      nullptr);
  if (window == nullptr) {
    UnregisterClassW(kWindowClass, instance);
    return 1;
  }

  ShowWindow(window, SW_SHOWNORMAL);
  if (UpdateWindow(window) == FALSE) {
    DestroyWindow(window);
    UnregisterClassW(kWindowClass, instance);
    return 1;
  }

  MSG message{};
  BOOL messageResult = 0;
  while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  if (IsWindow(window) != FALSE) {
    DestroyWindow(window);
  }
  UnregisterClassW(kWindowClass, instance);
  return messageResult == -1 ? 1 : static_cast<int>(message.wParam);
}

#else

int main() { return 0; }

#endif
