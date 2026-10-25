#include "framework.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef SM_KEYBOARDPRESENT
#define SM_KEYBOARDPRESENT 19
#endif

#define IDM_PIPE_CREATE   32771
#define IDM_PIPE_CLOSE    32772
#define ID_BTN_ACTION     2001
#define BUFFER_SIZE       2048

const LPCWSTR PIPE_NAME = L"\\\\.\\pipe\\LOCAL";

// -------------------------------------------------------------
// 1. ЛОГІКА СЕРВЕРА (SERVER)
// -------------------------------------------------------------
static HWND   g_hServerEdit = nullptr;
static HANDLE g_hServerThread = nullptr;
static volatile bool g_bServerRunning = false;
static wchar_t g_szServerLog[8192] = L"Лабораторна робота №7 - Канали обміну даними (Named Pipes).\r\n";

void AppendServerLog(const wchar_t* text)
{
    size_t currentLen = wcslen(g_szServerLog);
    if (currentLen + wcslen(text) + 4 < 8192)
    {
        wcscat_s(g_szServerLog, text);
        SetWindowTextW(g_hServerEdit, g_szServerLog);
        SendMessageW(g_hServerEdit, EM_SETSEL, (WPARAM)wcslen(g_szServerLog), (LPARAM)wcslen(g_szServerLog));
        SendMessageW(g_hServerEdit, EM_SCROLLCARET, 0, 0);
    }
}

// Робочий потік повністю сам володіє дескриптором і безпечно його закриває
DWORD WINAPI ServerPipeWorker(LPVOID lpParam)
{
    HWND hWnd = (HWND)lpParam;

    while (g_bServerRunning)
    {
        HANDLE hPipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFFER_SIZE, BUFFER_SIZE,
            5000,
            NULL
        );

        if (hPipe == INVALID_HANDLE_VALUE)
        {
            if (g_bServerRunning)
            {
                wchar_t errBuf[128];
                swprintf_s(errBuf, L"\r\nПомилка CreateNamedPipe: %d\r\n", GetLastError());
                AppendServerLog(errBuf);
            }
            break;
        }

        BOOL fConnected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        // Якщо сервер зупинили під час очікування — виходимо без читання
        if (!g_bServerRunning)
        {
            CloseHandle(hPipe);
            break;
        }

        if (fConnected)
        {
            wchar_t szReadBuf[1024] = { 0 };
            DWORD cbRead = 0;

            if (ReadFile(hPipe, szReadBuf, sizeof(szReadBuf) - sizeof(wchar_t), &cbRead, NULL))
            {
                szReadBuf[cbRead / sizeof(wchar_t)] = L'\0';
                AppendServerLog(L"\r\n[Отримано через Named Pipe]:\r\n");
                AppendServerLog(szReadBuf);
                AppendServerLog(L"\r\n");

                const wchar_t* ackMsg = L"Сервер: дані успішно прийнято та зареєстровано!";
                DWORD cbWritten = 0;
                WriteFile(hPipe, ackMsg, (DWORD)((wcslen(ackMsg) + 1) * sizeof(wchar_t)), &cbWritten, NULL);
                FlushFileBuffers(hPipe);
            }

            DisconnectNamedPipe(hPipe);
        }

        CloseHandle(hPipe);
    }

    return 0;
}

LRESULT CALLBACK ServerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hServerEdit = CreateWindowW(L"EDIT", g_szServerLog,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            15, 55, 490, 560, hWnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        CreateWindowW(L"BUTTON", L"Створити канал (Create)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            15, 12, 235, 32, hWnd, (HMENU)IDM_PIPE_CREATE, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Закрити канал (Close)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            270, 12, 235, 32, hWnd, (HMENU)IDM_PIPE_CLOSE, NULL, NULL);

        HMENU hMenuBar = CreateMenu();
        HMENU hMenu = CreateMenu();
        AppendMenuW(hMenu, MF_STRING, IDM_PIPE_CREATE, L"Create");
        AppendMenuW(hMenu, MF_STRING, IDM_PIPE_CLOSE, L"Close");
        AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hMenu, L"Pipes");
        SetMenu(hWnd, hMenuBar);
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDM_PIPE_CREATE:
            if (g_bServerRunning)
            {
                AppendServerLog(L"\r\n[Увага] Канал Named Pipe уже функціонує!\r\n");
                break;
            }

            g_bServerRunning = true;
            g_hServerThread = CreateThread(NULL, 0, ServerPipeWorker, hWnd, 0, NULL);
            AppendServerLog(L"\r\nСервер Named Pipe запущено. Очікування клієнтів...\r\n");
            break;

        case IDM_PIPE_CLOSE:
            if (g_bServerRunning)
            {
                g_bServerRunning = false;

                // Безпечно розблоковуємо ConnectNamedPipe фіктивним підключенням
                HANDLE hDummy = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
                if (hDummy != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(hDummy);
                }

                if (g_hServerThread)
                {
                    WaitForSingleObject(g_hServerThread, 1000);
                    CloseHandle(g_hServerThread);
                    g_hServerThread = nullptr;
                }

                AppendServerLog(L"\r\nКанал закрито. Сервер зупинено.\r\n");
            }
            break;
        }
        break;
    }

    case WM_DESTROY:
        if (g_bServerRunning)
        {
            g_bServerRunning = false;
            HANDLE hDummy = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (hDummy != INVALID_HANDLE_VALUE) CloseHandle(hDummy);
            if (g_hServerThread)
            {
                WaitForSingleObject(g_hServerThread, 1000);
                CloseHandle(g_hServerThread);
                g_hServerThread = nullptr;
            }
        }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// -------------------------------------------------------------
// 2. КЛІЄНТСЬКА ФУНКЦІЯ НАДСИЛАННЯ ТА ПРИЙОМУ ДАНИХ
// -------------------------------------------------------------
bool SendViaNamedPipe(HWND hWndParent, const wchar_t* text, HWND hLogEdit)
{
    HANDLE hPipe = CreateFileW(
        PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        wchar_t errMsg[512];
        swprintf_s(errMsg, L"%s\r\n\r\nПомилка підключення до каналу! Код: %d\r\n(Перевірте, чи запущено сервер)", text, err);
        SetWindowTextW(hLogEdit, errMsg);
        return false;
    }

    DWORD bytesToWrite = (DWORD)((wcslen(text) + 1) * sizeof(wchar_t));
    DWORD bytesWritten = 0;
    WriteFile(hPipe, text, bytesToWrite, &bytesWritten, NULL);

    wchar_t replyBuf[256] = { 0 };
    DWORD bytesRead = 0;
    ReadFile(hPipe, replyBuf, sizeof(replyBuf) - sizeof(wchar_t), &bytesRead, NULL);

    CloseHandle(hPipe);

    wchar_t successMsg[1500];
    swprintf_s(successMsg, L"%s\r\n\r\n[Успішно надіслано в канал!]\r\n%s", text, replyBuf);
    SetWindowTextW(hLogEdit, successMsg);
    return true;
}

// -------------------------------------------------------------
// 3. ЛОГІКА КЛІЄНТА #1 (Варіант 2)
// -------------------------------------------------------------
static HWND g_hClient1Edit = nullptr;

void RefreshClient1Data(HWND hWnd)
{
    int captionH = GetSystemMetrics(SM_CYCAPTION);
    int keyboardPresent = GetSystemMetrics(SM_KEYBOARDPRESENT);

    HDC hdc = GetDC(hWnd);
    int screenH = GetDeviceCaps(hdc, VERTRES);
    ReleaseDC(hWnd, hdc);

    wchar_t buf[512];
    swprintf_s(buf, L"Дані Клієнта #1 (Варіант 2):\r\n- Висота рядка заголовка (SM_CYCAPTION) = %d px\r\n- Стан клавіатури (SM_KEYBOARDPRESENT) = %s\r\n- Висота екрану (GetDeviceCaps, VERTRES) = %d px",
        captionH, keyboardPresent ? L"Підключена (1)" : L"Відсутня (0)", screenH);

    SetWindowTextW(g_hClient1Edit, buf);
}

LRESULT CALLBACK Client1WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hClient1Edit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
            12, 10, 440, 160, hWnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        CreateWindowW(L"BUTTON", L"Надіслати дані Клієнта #1 у канал", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 180, 440, 34, hWnd, (HMENU)ID_BTN_ACTION, NULL, NULL);

        RefreshClient1Data(hWnd);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_ACTION)
        {
            wchar_t text[1024];
            GetWindowTextW(g_hClient1Edit, text, 1024);
            SendViaNamedPipe(hWnd, text, g_hClient1Edit);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// -------------------------------------------------------------
// 4. ЛОГІКА КЛІЄНТА #2 (Варіант 2)
// -------------------------------------------------------------
static HWND g_hClient2Edit = nullptr;

void RefreshClient2Data(HWND hWnd)
{
    int fullScreenW = GetSystemMetrics(SM_CXFULLSCREEN);
    int monitorsCount = GetSystemMetrics(SM_CMONITORS);

    HDC hdc = GetDC(hWnd);
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hWnd, hdc);

    wchar_t buf[512];
    swprintf_s(buf, L"Дані Клієнта #2 (Варіант 2):\r\n- Ширина в повноекранному режимі (SM_CXFULLSCREEN) = %d px\r\n- Кількість підключених моніторів (SM_CMONITORS) = %d\r\n- DPI горизонтально (GetDeviceCaps, LOGPIXELSX) = %d",
        fullScreenW, monitorsCount, dpiX);

    SetWindowTextW(g_hClient2Edit, buf);
}

LRESULT CALLBACK Client2WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hClient2Edit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
            12, 10, 440, 160, hWnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        CreateWindowW(L"BUTTON", L"Надіслати дані Клієнта #2 у канал", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 180, 440, 34, hWnd, (HMENU)ID_BTN_ACTION, NULL, NULL);

        RefreshClient2Data(hWnd);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_ACTION)
        {
            wchar_t text[1024];
            GetWindowTextW(g_hClient2Edit, text, 1024);
            SendViaNamedPipe(hWnd, text, g_hClient2Edit);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// -------------------------------------------------------------
// 5. ДОПОМІЖНА ФУНКЦІЯ ЗАПУСКУ КЛІЄНТІВ
// -------------------------------------------------------------
void SpawnProcess(const wchar_t* arg)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    wchar_t cmdLine[MAX_PATH * 2];
    swprintf_s(cmdLine, L"\"%s\" %s", exePath, arg);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

// -------------------------------------------------------------
// 6. ГОЛОВНА ТОЧКА ВХОДУ (wWinMain)
// -------------------------------------------------------------
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    if (wcsstr(lpCmdLine, L"client1"))
    {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.lpfnWndProc = Client1WndProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = L"Client1PipeClass";
        RegisterClassExW(&wcex);

        HWND hWnd = CreateWindowW(L"Client1PipeClass", L"Клієнт #1 (Варіант 2)",
            WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 570, 40, 485, 275, NULL, NULL, hInstance, NULL);

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return (int)msg.wParam;
    }
    else if (wcsstr(lpCmdLine, L"client2"))
    {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.lpfnWndProc = Client2WndProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = L"Client2PipeClass";
        RegisterClassExW(&wcex);

        HWND hWnd = CreateWindowW(L"Client2PipeClass", L"Клієнт #2 (Варіант 2)",
            WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 570, 340, 485, 275, NULL, NULL, hInstance, NULL);

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return (int)msg.wParam;
    }
    else
    {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.lpfnWndProc = ServerWndProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = L"ServerPipeClass";
        RegisterClassExW(&wcex);

        HWND hWnd = CreateWindowW(L"ServerPipeClass", L"Сервер - Named Pipes",
            WS_OVERLAPPEDWINDOW, 30, 40, 535, 680, NULL, NULL, hInstance, NULL);

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        // Автоматичний запуск двох клієнтів
        SpawnProcess(L"client1");
        SpawnProcess(L"client2");

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return (int)msg.wParam;
    }
}