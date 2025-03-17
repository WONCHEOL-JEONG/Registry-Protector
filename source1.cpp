#include <windows.h>
#include <tchar.h>
#include <string.h>
#include <stdio.h>

// �ʼ� ���̺귯�� ����
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "kernel32.lib")

// �ʿ�� ��� ��Ȱ��ȭ (������ �ʴ´ٸ� ����)
#pragma warning(disable:28251)

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

#define REG_PATH _T("SOFTWARE\\TestKey")
#define WM_APP_REG_CHANGE (WM_APP + 1)
#define WM_TRAYICON       (WM_APP + 2)

#define IDC_EXIT  1001    // ���� ��ư ID
#define IDC_TEST  1002    // �׽�Ʈ ��ư ID

// �Լ� ������Ÿ�� ����
void AppendText(HWND hEdit, LPCTSTR text);
void UpdateStatus(LPCTSTR status);
LONG ReadRegistryValue(TCHAR* outBuffer, DWORD bufferSize);
void protect_registry(void);
void simulate_external_change(void);
DWORD WINAPI MonitorRegistryThread(LPVOID lpParam);
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon(HWND hWnd);

// ���� ����
HWND hEdit;       // �α� ��¿� ����Ʈ ��Ʈ�� �ڵ�
HWND hStatus;     // ���� ǥ�ÿ� ���� ��Ʈ�� �ڵ�
HINSTANCE hInst;  // ���ø����̼� �ν��Ͻ�
NOTIFYICONDATA nid = { 0 }; // �ý��� Ʈ���� ������ ������

// ���������� ������ ������Ʈ�� ���� ���� (�ʱⰪ: "Protected Value")
TCHAR g_lastValue[256] = _T("Protected Value");

// ���� �۾� ������ ��Ÿ���� �÷���
volatile BOOL g_bRestoring = FALSE;

// ������Ʈ�� ��ȣ �Լ� (�׻� "Protected Value"�� ����)
void protect_registry(void) {
    HKEY hKey;
    LONG result;

    g_bRestoring = TRUE; // ���� ����

    result = RegCreateKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL);
    if (result != ERROR_SUCCESS) {
        AppendText(hEdit, _T("[Error] ������Ʈ�� Ű ���� ����.\r\n"));
        g_bRestoring = FALSE;
        return;
    }

    const char* data = "Protected Value";
    result = RegSetValueExA(hKey, "SecureEntry", 0, REG_SZ, (const BYTE*)data, (DWORD)(strlen(data) + 1));
    if (result != ERROR_SUCCESS) {
        AppendText(hEdit, _T("[Error] ������Ʈ�� �� ���� ����.\r\n"));
        RegCloseKey(hKey);
        g_bRestoring = FALSE;
        return;
    }

    AppendText(hEdit, _T("[*] ������Ʈ�� ��ȣ ���� �Ϸ�.\r\n"));
    _tcscpy_s(g_lastValue, ARRAYSIZE(g_lastValue), _T("Protected Value"));
    RegCloseKey(hKey);
    g_bRestoring = FALSE; // ���� �Ϸ�
}

// �ܺ� ������ �ùķ��̼��ϴ� �Լ�: ������Ʈ�� ���� "Modified Value"�� ����
void simulate_external_change(void) {
    HKEY hKey;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_SET_VALUE, &hKey);
    if (result != ERROR_SUCCESS) {
        AppendText(hEdit, _T("[Error] ������Ʈ�� �ܺ� ���� �ùķ��̼� ����.\r\n"));
        return;
    }
    const char* data = "Modified Value";
    result = RegSetValueExA(hKey, "SecureEntry", 0, REG_SZ, (const BYTE*)data, (DWORD)(strlen(data) + 1));
    if (result != ERROR_SUCCESS) {
        AppendText(hEdit, _T("[Error] ������Ʈ�� �� ���� ����.\r\n"));
    }
    else {
        AppendText(hEdit, _T("[Test] �ܺ� ���� �ùķ��̼� �Ϸ�: Modified Value\r\n"));
        AppendText(hEdit, _T("[Test] ����� ������Ʈ��: HKEY_CURRENT_USER\\SOFTWARE\\TestKey, Value: SecureEntry\r\n"));
    }
    RegCloseKey(hKey);
}

// ����Ʈ ��Ʈ�ѿ� �ؽ�Ʈ�� �߰��ϴ� �Լ� (�߰� �� ��ũ�ѹ� �� �Ʒ��� �̵�)
void AppendText(HWND hEdit, LPCTSTR text) {
    int len = GetWindowTextLength(hEdit);
    SendMessage(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hEdit, EM_REPLACESEL, 0, (LPARAM)text);
    SendMessage(hEdit, WM_VSCROLL, SB_BOTTOM, 0);
}

// ���� ��Ʈ�� ������Ʈ �Լ�
void UpdateStatus(LPCTSTR status) {
    SetWindowText(hStatus, status);
}

// ������Ʈ�� ���� �о�� wide ���ڿ� ���ۿ� ���� (���� �� ERROR_SUCCESS ��ȯ)
LONG ReadRegistryValue(TCHAR* outBuffer, DWORD bufferSize) {
    HKEY hKey;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        _tcscpy_s(outBuffer, bufferSize, _T("Ű ���� ����"));
        return result;
    }
    char ansiBuffer[256] = { 0 };
    DWORD bufSize = sizeof(ansiBuffer);
    result = RegQueryValueExA(hKey, "SecureEntry", NULL, NULL, (LPBYTE)ansiBuffer, &bufSize);
    if (result == ERROR_SUCCESS) {
        MultiByteToWideChar(CP_ACP, 0, ansiBuffer, -1, outBuffer, bufferSize);
    }
    else {
        _tcscpy_s(outBuffer, bufferSize, _T("�� �б� ����"));
    }
    RegCloseKey(hKey);
    return result;
}

// ������Ʈ�� ����͸� ������ �Լ�
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
            AppendText(hEdit, _T("[Error] RegNotifyChangeKeyValue ����.\r\n"));
            break;
        }

        DWORD dwWaitStatus = WaitForSingleObject(hEvent, INFINITE);
        if (dwWaitStatus == WAIT_OBJECT_0) {
            // ���� ���� �۾� ���̸� ����
            if (g_bRestoring) {
                ResetEvent(hEvent);
                continue;
            }
            TCHAR current[256] = { 0 };
            ReadRegistryValue(current, 256);
            // ���� ����� ��쿡�� �˸� ����
            if (_tcscmp(current, g_lastValue) != 0) {
                _tcscpy_s(g_lastValue, ARRAYSIZE(g_lastValue), current);
                PostMessage(hWnd, WM_APP_REG_CHANGE, 0, 0);
                // ���� 3�ʰ� ������ ���̸� �߰� �˸� ����
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
            AppendText(hEdit, _T("[Error] WaitForSingleObject ����.\r\n"));
            break;
        }
        Sleep(1000);
    }

    CloseHandle(hEvent);
    RegCloseKey(hKey);
    return 0;
}

// �ý��� Ʈ���� ������ �߰�
void AddTrayIcon(HWND hWnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    _tcscpy_s(nid.szTip, ARRAYSIZE(nid.szTip), _T("������Ʈ�� ��ȣ �ý���"));
    Shell_NotifyIcon(NIM_ADD, &nid);
}

// �ý��� Ʈ���� ������ ����
void RemoveTrayIcon(HWND hWnd) {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// ������ ���ν���
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
    {
        // ���� ǥ�ÿ� ���� ��Ʈ�� ���� (â ���)
        hStatus = CreateWindowEx(WS_EX_CLIENTEDGE, _T("STATIC"), _T("����: ���� �۵� ��"),
            WS_CHILD | WS_VISIBLE, 10, 10, 460, 20, hWnd, NULL, hInst, NULL);
        if (!hStatus) {
            MessageBox(hWnd, _T("���� ��Ʈ�� ���� ����!"), _T("Error"), MB_OK | MB_ICONERROR);
        }

        // �α� ��¿� ����Ʈ ��Ʈ�� ���� (���� �Ʒ�)
        hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL | ES_READONLY,
            10, 40, 460, 170, hWnd, NULL, hInst, NULL);
        if (!hEdit) {
            MessageBox(hWnd, _T("����Ʈ ��Ʈ�� ���� ����!"), _T("Error"), MB_OK | MB_ICONERROR);
        }
        AppendText(hEdit, _T("[*] ������Ʈ�� ��ȣ �ý��� ���۵�...\r\n"));

        // ���� ��ư ����
        HWND hButtonExit = CreateWindowEx(0, _T("BUTTON"), _T("����"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            10, 220, 100, 30, hWnd, (HMENU)IDC_EXIT, hInst, NULL);
        if (!hButtonExit) {
            AppendText(hEdit, _T("[Error] ���� ��ư ���� ����.\r\n"));
        }

        // �׽�Ʈ ��ư ���� (�� �� ������ �ܺ� ���� �ùķ��̼� ����)
        HWND hButtonTest = CreateWindowEx(0, _T("BUTTON"), _T("�׽�Ʈ"),
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            120, 220, 100, 30, hWnd, (HMENU)IDC_TEST, hInst, NULL);
        if (!hButtonTest) {
            AppendText(hEdit, _T("[Error] �׽�Ʈ ��ư ���� ����.\r\n"));
        }

        // �ý��� Ʈ���� ������ �߰�
        AddTrayIcon(hWnd);

        // �ʱ� ������Ʈ�� ��ȣ ����
        protect_registry();

        // ������Ʈ�� ����͸� ������ ����
        HANDLE hThread = CreateThread(NULL, 0, MonitorRegistryThread, (LPVOID)hWnd, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        }
        else {
            AppendText(hEdit, _T("[Error] ������Ʈ�� ����͸� ������ ���� ����.\r\n"));
        }
    }
    break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDC_TEST:
            // �׽�Ʈ ��ư Ŭ�� ��, �ܺ� ���� �ùķ��̼� ���� (����͸� �����尡 ����)
            simulate_external_change();
            break;
        default:
            break;
        }
        break;

    case WM_APP_REG_CHANGE:
    {
        if (wParam == 1) {
            AppendText(hEdit, _T("[Error] ������Ʈ�� ����͸� ����.\r\n"));
            break;
        }

        TCHAR currentValue[256] = { 0 };
        ReadRegistryValue(currentValue, 256);
        AppendText(hEdit, _T("[ALERT] ������Ʈ�� ���� ������!\r\n"));
        AppendText(hEdit, _T("���� ������Ʈ�� ��: "));
        AppendText(hEdit, currentValue);
        AppendText(hEdit, _T("\r\n"));

        int ret = MessageBox(hWnd, _T("������Ʈ�� ���� ����Ǿ����ϴ�.\n���� ������ �����Ͻðڽ��ϱ�?"),
            _T("������Ʈ�� ���"), MB_YESNO | MB_ICONQUESTION);
        if (ret == IDYES) {
            AppendText(hEdit, _T("[*] ���� �� ������Ʈ�� ��: "));
            AppendText(hEdit, currentValue);
            AppendText(hEdit, _T("\r\n"));

            protect_registry();

            TCHAR newValue[256] = { 0 };
            ReadRegistryValue(newValue, 256);
            AppendText(hEdit, _T("[*] ���� �� ������Ʈ�� ��: "));
            AppendText(hEdit, newValue);
            AppendText(hEdit, _T("\r\n"));
        }
        else {
            AppendText(hEdit, _T("[*] ������Ʈ�� �� ���� �� ��. ����� �� ������.\r\n"));
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

// _tWinMain �Լ� (������ ���ø����̼� ��Ʈ�� ����Ʈ)
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
        MessageBox(NULL, _T("������ Ŭ���� ��� ����!"), _T("Error"), MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindow(_T("RegistryProtectionClass"), _T("������Ʈ�� ��ȣ �ý���"),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 520, 320,
        NULL, NULL, hInstance, NULL);
    if (!hWnd) {
        MessageBox(NULL, _T("���� ������ ���� ����!"), _T("Error"), MB_OK | MB_ICONERROR);
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