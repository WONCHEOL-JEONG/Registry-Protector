#include <windows.h>
#include <tchar.h>
#include <string.h>
#include <stdio.h>

// 필수 라이브러리 연결
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "kernel32.lib")

// 필요시 경고 비활성화 (원하지 않는다면 제거)
#pragma warning(disable:28251)

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

#define REG_PATH _T("SOFTWARE\\TestKey")
#define WM_APP_REG_CHANGE (WM_APP + 1)
#define WM_TRAYICON       (WM_APP + 2)

#define IDC_EXIT  1001    // 종료 버튼 ID
#define IDC_TEST  1002    // 테스트 버튼 ID

// 함수 프로토타입 선언
void AppendText(HWND hEdit, LPCTSTR text);
void UpdateStatus(LPCTSTR status);
LONG ReadRegistryValue(TCHAR* outBuffer, DWORD bufferSize);
void protect_registry(void);
void simulate_external_change(void);
DWORD WINAPI MonitorRegistryThread(LPVOID lpParam);
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon(HWND hWnd);

// 전역 변수
HWND hEdit;       // 로그 출력용 에디트 컨트롤 핸들
HWND hStatus;     // 상태 표시용 정적 컨트롤 핸들
HINSTANCE hInst;  // 애플리케이션 인스턴스
NOTIFYICONDATA nid = { 0 }; // 시스템 트레이 아이콘 데이터

// 마지막으로 감지한 레지스트리 값을 저장 (초기값: "Protected Value")
TCHAR g_lastValue[256] = _T("Protected Value");

// 복원 작업 중임을 나타내는 플래그
volatile BOOL g_bRestoring = FALSE;

// 레지스트리 보호 함수 (항상 "Protected Value"로 설정)
void protect_registry(void) {
    HKEY hKey;
    LONG result;

    g_bRestoring = TRUE; // 복원 시작

    result = RegCreateKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL);
    if (result != ERROR_SUCCESS) {
        AppendText(hEdit, _T("[Error] 레지스트리 키 생성 실패.\r\n"));
        g_bRestoring = FALSE;
        return;
    }

    const char* data = "Protected Value";
    result = RegSetValueExA(hKey, "SecureEntry", 0, REG_SZ, (const BYTE*)data, (DWORD)(strlen(data) + 1));
    if (result != ERROR_SUCCESS) {
        AppendText(hEdit, _T("[Error] 레지스트리 값 설정 실패.\r\n"));
        RegCloseKey(hKey);
        g_bRestoring = FALSE;
        return;
    }

    AppendText(hEdit, _T("[*] 레지스트리 보호 설정 완료.\r\n"));
    _tcscpy_s(g_lastValue, ARRAYSIZE(g_lastValue), _T("Protected Value"));
    RegCloseKey(hKey);
    g_bRestoring = FALSE; // 복원 완료
}

// 외부 변경을 시뮬레이션하는 함수: 레지스트리 값을 "Modified Value"로 변경
void simulate_external_change(void) {
    HKEY hKey;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_SET_VALUE, &hKey);
    if (result != ERROR_SUCCESS) {
        AppendText(hEdit, _T("[Error] 레지스트리 외부 변경 시뮬레이션 실패.\r\n"));
        return;
    }
    const char* data = "Modified Value";
    result = RegSetValueExA(hKey, "SecureEntry", 0, REG_SZ, (const BYTE*)data, (DWORD)(strlen(data) + 1));
    if (result != ERROR_SUCCESS) {
        AppendText(hEdit, _T("[Error] 레지스트리 값 변경 실패.\r\n"));
    }
    else {
        AppendText(hEdit, _T("[Test] 외부 변경 시뮬레이션 완료: Modified Value\r\n"));
        AppendText(hEdit, _T("[Test] 변경된 레지스트리: HKEY_CURRENT_USER\\SOFTWARE\\TestKey, Value: SecureEntry\r\n"));
    }
    RegCloseKey(hKey);
}

// 에디트 컨트롤에 텍스트를 추가하는 함수 (추가 후 스크롤바 맨 아래로 이동)
void AppendText(HWND hEdit, LPCTSTR text) {
    int len = GetWindowTextLength(hEdit);
    SendMessage(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEdit, EM_REPLACESEL, 0, (LPARAM)text);
    SendMessage(hEdit, WM_VSCROLL, SB_BOTTOM, 0);
}

// 상태 컨트롤 업데이트 함수
void UpdateStatus(LPCTSTR status) {
    SetWindowText(hStatus, status);
}

// 레지스트리 값을 읽어와 wide 문자열 버퍼에 복사 (성공 시 ERROR_SUCCESS 반환)
LONG ReadRegistryValue(TCHAR* outBuffer, DWORD bufferSize) {
    HKEY hKey;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        _tcscpy_s(outBuffer, bufferSize, _T("키 열기 실패"));
        return result;
    }
    char ansiBuffer[256] = { 0 };
    DWORD bufSize = sizeof(ansiBuffer);
    result = RegQueryValueExA(hKey, "SecureEntry", NULL, NULL, (LPBYTE)ansiBuffer, &bufSize);
    if (result == ERROR_SUCCESS) {
        MultiByteToWideChar(CP_ACP, 0, ansiBuffer, -1, outBuffer, bufferSize);
    }
    else {
        _tcscpy_s(outBuffer, bufferSize, _T("값 읽기 실패"));
    }
    RegCloseKey(hKey);
    return result;
}

// 레지스트리 모니터링 쓰레드 함수
DWORD WINAPI MonitorRegistryThread(LPVOID lpParam) {
    HWND hWnd = (HWND)lpParam;
    HKEY hKey;
    HANDLE hEvent;
    LONG result;

    result = RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_NOTIFY, &hKey);
    if (result != ERROR_SUCCESS) {
        PostMessage(hWnd, WM_APP_REG_CHANGE, 1, 0);
        return 1;
    }

    hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (hEvent == NULL) {
        RegCloseKey(hKey);
        PostMessage(hWnd, WM_APP_REG_CHANGE, 1, 0);
        return 1;
    }

    while (1) {
        result = RegNotifyChangeKeyValue(hKey, TRUE, REG_NOTIFY_CHANGE_LAST_SET, hEvent, TRUE);
        if (result != ERROR_SUCCESS) {
            AppendText(hEdit, _T("[Error] RegNotifyChangeKeyValue 실패.\r\n"));
            break;
        }

        DWORD dwWaitStatus = WaitForSingleObject(hEvent, INFINITE);
        if (dwWaitStatus == WAIT_OBJECT_0) {
            // 만약 복원 작업 중이면 무시
            if (g_bRestoring) {
                ResetEvent(hEvent);
                continue;
            }
            TCHAR current[256] = { 0 };
            ReadRegistryValue(current, 256);
            // 값이 변경된 경우에만 알림 전송
            if (_tcscmp(current, g_lastValue) != 0) {
                _tcscpy_s(g_lastValue, ARRAYSIZE(g_lastValue), current);
                PostMessage(hWnd, WM_APP_REG_CHANGE, 0, 0);
                // 이후 3초간 동일한 값이면 추가 알림 방지
                DWORD startTime = GetTickCount();
                while (GetTickCount() - startTime < 3000) {
                    TCHAR temp[256] = { 0 };
                    ReadRegistryValue(temp, 256);
                    if (_tcscmp(temp, current) != 0)
                        break;
                    Sleep(500);
                }
            }
            ResetEvent(hEvent);
        }
        else {
            AppendText(hEdit, _T("[Error] WaitForSingleObject 실패.\r\n"));
            break;
        }
        Sleep(1000);
    }

    CloseHandle(hEvent);
    RegCloseKey(hKey);
    return 0;
}

// 시스템 트레이 아이콘 추가
void AddTrayIcon(HWND hWnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    _tcscpy_s(nid.szTip, ARRAYSIZE(nid.szTip), _T("레지스트리 보호 시스템"));
    Shell_NotifyIcon(NIM_ADD, &nid);
}

// 시스템 트레이 아이콘 제거
void RemoveTrayIcon(HWND hWnd) {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// 윈도우 프로시저
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
    {
        // 상태 표시용 정적 컨트롤 생성 (창 상단)
        hStatus = CreateWindowEx(WS_EX_CLIENTEDGE, _T("STATIC"), _T("상태: 정상 작동 중"),
            WS_CHILD | WS_VISIBLE, 10, 10, 460, 20, hWnd, NULL, hInst, NULL);
        if (!hStatus) {
            MessageBox(hWnd, _T("상태 컨트롤 생성 실패!"), _T("Error"), MB_OK | MB_ICONERROR);
        }

        // 로그 출력용 에디트 컨트롤 생성 (상태 아래)
        hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL | ES_READONLY,
            10, 40, 460, 170, hWnd, NULL, hInst, NULL);
        if (!hEdit) {
            MessageBox(hWnd, _T("에디트 컨트롤 생성 실패!"), _T("Error"), MB_OK | MB_ICONERROR);
        }
        AppendText(hEdit, _T("[*] 레지스트리 보호 시스템 시작됨...\r\n"));

        // 종료 버튼 생성
        HWND hButtonExit = CreateWindowEx(0, _T("BUTTON"), _T("종료"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            10, 220, 100, 30, hWnd, (HMENU)IDC_EXIT, hInst, NULL);
        if (!hButtonExit) {
            AppendText(hEdit, _T("[Error] 종료 버튼 생성 실패.\r\n"));
        }

        // 테스트 버튼 생성 (한 번 누르면 외부 변경 시뮬레이션 실행)
        HWND hButtonTest = CreateWindowEx(0, _T("BUTTON"), _T("테스트"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            120, 220, 100, 30, hWnd, (HMENU)IDC_TEST, hInst, NULL);
        if (!hButtonTest) {
            AppendText(hEdit, _T("[Error] 테스트 버튼 생성 실패.\r\n"));
        }

        // 시스템 트레이 아이콘 추가
        AddTrayIcon(hWnd);

        // 초기 레지스트리 보호 설정
        protect_registry();

        // 레지스트리 모니터링 쓰레드 생성
        HANDLE hThread = CreateThread(NULL, 0, MonitorRegistryThread, (LPVOID)hWnd, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        }
        else {
            AppendText(hEdit, _T("[Error] 레지스트리 모니터링 쓰레드 생성 실패.\r\n"));
        }
    }
    break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDC_TEST:
            // 테스트 버튼 클릭 시, 외부 변경 시뮬레이션 실행 (모니터링 쓰레드가 감지)
            simulate_external_change();
            break;
        default:
            break;
        }
        break;

    case WM_APP_REG_CHANGE:
    {
        if (wParam == 1) {
            AppendText(hEdit, _T("[Error] 레지스트리 모니터링 실패.\r\n"));
            break;
        }

        TCHAR currentValue[256] = { 0 };
        ReadRegistryValue(currentValue, 256);
        AppendText(hEdit, _T("[ALERT] 레지스트리 변경 감지됨!\r\n"));
        AppendText(hEdit, _T("현재 레지스트리 값: "));
        AppendText(hEdit, currentValue);
        AppendText(hEdit, _T("\r\n"));

        int ret = MessageBox(hWnd, _T("레지스트리 값이 변경되었습니다.\n원래 값으로 복원하시겠습니까?"),
            _T("레지스트리 경고"), MB_YESNO | MB_ICONQUESTION);
        if (ret == IDYES) {
            AppendText(hEdit, _T("[*] 복원 전 레지스트리 값: "));
            AppendText(hEdit, currentValue);
            AppendText(hEdit, _T("\r\n"));

            protect_registry();

            TCHAR newValue[256] = { 0 };
            ReadRegistryValue(newValue, 256);
            AppendText(hEdit, _T("[*] 복원 후 레지스트리 값: "));
            AppendText(hEdit, newValue);
            AppendText(hEdit, _T("\r\n"));
        }
        else {
            AppendText(hEdit, _T("[*] 레지스트리 값 복원 안 함. 변경된 값 유지됨.\r\n"));
        }
    }
    break;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
        }
        break;

    case WM_DESTROY:
        RemoveTrayIcon(hWnd);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// _tWinMain 함수 (윈도우 애플리케이션 엔트리 포인트)
int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPTSTR lpCmdLine, int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    hInst = hInstance;
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = _T("RegistryProtectionClass");
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClass(&wc)) {
        MessageBox(NULL, _T("윈도우 클래스 등록 실패!"), _T("Error"), MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindow(_T("RegistryProtectionClass"), _T("레지스트리 보호 시스템"),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 520, 320,
        NULL, NULL, hInstance, NULL);
    if (!hWnd) {
        MessageBox(NULL, _T("메인 윈도우 생성 실패!"), _T("Error"), MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}