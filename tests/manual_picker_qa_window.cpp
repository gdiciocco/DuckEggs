#ifdef _WIN32

#include <windows.h>

namespace {

constexpr wchar_t kWindowClass[] = L"DuckEggsQaFloatingWindow";
constexpr wchar_t kWindowTitle[] = L"DuckEggs PiP selection test";
constexpr UINT_PTR kAutoExitTimer = 1;
constexpr UINT kAutoExitMilliseconds = 300U * 1000U;

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      if (SetTimer(window, kAutoExitTimer, kAutoExitMilliseconds, nullptr) == 0) {
        return -1;
      }
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
      }
      return 0;

    case WM_CLOSE:
      DestroyWindow(window);
      return 0;

    case WM_DESTROY:
      KillTimer(window, kAutoExitTimer);
      PostQuitMessage(0);
      return 0;

    default:
      return DefWindowProcW(window, message, wParam, lParam);
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
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
      WS_EX_TOPMOST, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
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
