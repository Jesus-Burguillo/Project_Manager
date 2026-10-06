// project_manager.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "project_manager.h"
#include "ProcessManager.h"
#include "SystemInfo.h"
#include <CommCtrl.h>
#include <uxtheme.h>      // UI: tema moderno para el ListView
#include <dwmapi.h>       // UI: barra de título oscura
#include <sstream>
#include <iomanip>
#include <cstdlib>        // UI: wcstod (lectura del porcentaje para las barras)

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "UxTheme.lib")   // UI
#pragma comment(lib, "Dwmapi.lib")    // UI

// UI: activa los estilos visuales modernos (botones, listview, scrollbars).
// Si tu proyecto ya tiene este manifest, borra esta línea.
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define ID_BUTTON_REFRESH       1001
#define ID_LIST_PROCESSES       1002
#define ID_BUTTON_TERMINATE     1003
#define ID_TIMER_REFRESH        1
#define ID_TIMER_TERMINATING    2
#define ID_TIMER_SYSTEM_STATS   3

// Global Variables:
HINSTANCE hInst;                                // current instance
const wchar_t* windowTitle = L"Gestor de Procesos";
const wchar_t* windowClass = L"ProcessManagerWindow";
ProcessManager processManager;  // Instance of ProcessManager
SystemInfo systemInfo;          // Instance of SystemInfo
HWND hProcessList;
std::vector<Process> previousProcesses;
std::vector<Process> currentProcesses; // Global variable to hold the current list of processes
std::uint32_t selectedProcessId = 0;
HWND hProcessNameLabel;
HANDLE terminatingProcessHandle = nullptr;

// UI: handles y fuentes necesarios solo para el layout y el estilo
HWND hTitleLabel = nullptr;
HWND hRefreshButton = nullptr;
HWND hTerminateButton = nullptr;
HWND hCpuLabel = nullptr;
HWND hMemoryLabel = nullptr;
HWND hProcessCountLabel = nullptr;
HWND hThreadCountLabel = nullptr;
HFONT hFontUI = nullptr;
HFONT hFontTitle = nullptr;
HFONT hFontValue = nullptr;   // UI: fuente grande para el valor de cada tarjeta

// UI: modo oscuro - paleta y brushes
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

constexpr COLORREF kBgColor = RGB(32, 32, 32);
constexpr COLORREF kListBgColor = RGB(28, 28, 28);
constexpr COLORREF kHeaderColor = RGB(45, 45, 45);
constexpr COLORREF kSepColor = RGB(70, 70, 70);
constexpr COLORREF kTextColor = RGB(230, 230, 230);
constexpr COLORREF kMutedColor = RGB(160, 160, 160);

HBRUSH hBrushBg = CreateSolidBrush(kBgColor);
HBRUSH hBrushHeader = CreateSolidBrush(kHeaderColor);
HBRUSH hBrushSep = CreateSolidBrush(kSepColor);

// UI: dibuja la cabecera del ListView en oscuro (el tema del sistema la deja clara)
static LRESULT DrawDarkHeader(LPARAM lParam)
{
    NMCUSTOMDRAW* cd = reinterpret_cast<NMCUSTOMDRAW*>(lParam);

    switch (cd->dwDrawStage)
    {
    case CDDS_PREPAINT:
        FillRect(cd->hdc, &cd->rc, hBrushHeader);
        return CDRF_NOTIFYITEMDRAW;

    case CDDS_ITEMPREPAINT:
    {
        wchar_t text[128] = L"";
        HDITEMW hdi{};
        hdi.mask = HDI_TEXT | HDI_FORMAT;
        hdi.pszText = text;
        hdi.cchTextMax = 128;
        SendMessageW(cd->hdr.hwndFrom, HDM_GETITEMW, cd->dwItemSpec, reinterpret_cast<LPARAM>(&hdi));

        RECT rc = cd->rc;
        FillRect(cd->hdc, &rc, hBrushHeader);

        RECT sep = { rc.right - 1, rc.top + 6, rc.right, rc.bottom - 6 };
        FillRect(cd->hdc, &sep, hBrushSep);

        SetBkMode(cd->hdc, TRANSPARENT);
        SetTextColor(cd->hdc, kTextColor);

        RECT tr = rc;
        tr.left += 8;
        tr.right -= 8;
        UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
            ((hdi.fmt & HDF_RIGHT) ? DT_RIGHT : DT_LEFT);
        DrawTextW(cd->hdc, text, -1, &tr, flags);

        return CDRF_SKIPDEFAULT;
    }
    }

    return CDRF_DODEFAULT;
}

// UI: tarjetas de estadísticas (CPU, RAM, procesos, hilos).
// Cada etiqueta es SS_OWNERDRAW: UpdateSystemStats sigue escribiendo su texto con
// SetWindowTextW exactamente igual que antes y aquí solo se DIBUJA ese texto.
// Si el texto lleva un porcentaje ("12.3%" o "(64.3%)") se añade una barra.
constexpr COLORREF kCardColor = RGB(40, 40, 40);
constexpr COLORREF kTrackColor = RGB(64, 64, 64);
constexpr COLORREF kAccentColor = RGB(76, 158, 255);   // < 70 %
constexpr COLORREF kWarnColor = RGB(245, 176, 65);   // >= 70 %
constexpr COLORREF kDangerColor = RGB(239, 83, 80);    // >= 90 %

static void DrawStatCard(const DRAWITEMSTRUCT* dis)
{
    wchar_t buffer[160] = L"";
    GetWindowTextW(dis->hwndItem, buffer, 160);

    // "RAM: 10.2 / 15.9 GB (64.3%)" -> título "RAM", valor "10.2 / 15.9 GB", extra "64.3%"
    std::wstring title = buffer;
    std::wstring value;
    std::wstring extra;
    double percent = -1.0;   // -1 = esta tarjeta no lleva barra

    std::size_t colon = title.find(L": ");
    if (colon != std::wstring::npos)
    {
        value = title.substr(colon + 2);
        title = title.substr(0, colon);
    }

    std::size_t lp = value.find(L'(');
    std::size_t rp = value.find(L')');
    if (lp != std::wstring::npos && rp != std::wstring::npos && rp > lp)
    {
        extra = value.substr(lp + 1, rp - lp - 1);
        value = value.substr(0, lp);

        while (!value.empty() && value.back() == L' ')
        {
            value.pop_back();
        }

        if (!extra.empty() && extra.back() == L'%')
        {
            percent = wcstod(extra.c_str(), nullptr);
        }
    }
    else if (!value.empty() && value.back() == L'%')
    {
        percent = wcstod(value.c_str(), nullptr);
    }

    if (percent > 100.0)
    {
        percent = 100.0;
    }

    const RECT& item = dis->rcItem;
    const int w = item.right - item.left;
    const int h = item.bottom - item.top;

    if (w <= 0 || h <= 0)
    {
        return;
    }

    // Doble buffer para que al actualizar cada segundo no parpadee
    HDC mem = CreateCompatibleDC(dis->hDC);
    HBITMAP bmp = CreateCompatibleBitmap(dis->hDC, w, h);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    RECT all = { 0, 0, w, h };
    FillRect(mem, &all, hBrushBg);

    // Tarjeta redondeada
    HPEN pen = CreatePen(PS_SOLID, 1, kSepColor);
    HBRUSH cardBrush = CreateSolidBrush(kCardColor);
    HGDIOBJ oldPen = SelectObject(mem, pen);
    HGDIOBJ oldBrush = SelectObject(mem, cardBrush);
    RoundRect(mem, 0, 0, w, h, 10, 10);
    SelectObject(mem, oldPen);
    SelectObject(mem, oldBrush);
    DeleteObject(pen);
    DeleteObject(cardBrush);

    const int pad = 14;
    SetBkMode(mem, TRANSPARENT);

    // Título (gris, pequeño) y porcentaje secundario a la derecha (RAM)
    HGDIOBJ oldFont = SelectObject(mem, hFontUI);
    SetTextColor(mem, kMutedColor);

    RECT titleRc = { pad, 8, w - pad, 26 };
    DrawTextW(mem, title.c_str(), -1, &titleRc, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

    if (!extra.empty())
    {
        DrawTextW(mem, extra.c_str(), -1, &titleRc, DT_SINGLELINE | DT_RIGHT | DT_TOP | DT_NOPREFIX);
    }

    // Valor grande
    SelectObject(mem, hFontValue);
    SetTextColor(mem, kTextColor);

    RECT valueRc = { pad, 26, w - pad, 56 };
    DrawTextW(mem, value.c_str(), -1, &valueRc, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(mem, oldFont);

    // Barra porcentual (solo si hay porcentaje)
    if (percent >= 0.0)
    {
        RECT track = { pad, h - 16, w - pad, h - 10 };

        HRGN clip = CreateRoundRectRgn(track.left, track.top, track.right + 1, track.bottom + 1, 6, 6);
        SelectClipRgn(mem, clip);

        HBRUSH trackBrush = CreateSolidBrush(kTrackColor);
        FillRect(mem, &track, trackBrush);
        DeleteObject(trackBrush);

        int fillW = static_cast<int>((track.right - track.left) * percent / 100.0);

        if (fillW > 0)
        {
            COLORREF color =
                percent >= 90.0 ? kDangerColor :
                percent >= 70.0 ? kWarnColor :
                kAccentColor;

            RECT bar = { track.left, track.top, track.left + fillW, track.bottom };
            HBRUSH barBrush = CreateSolidBrush(color);
            FillRect(mem, &bar, barBrush);
            DeleteObject(barBrush);
        }

        SelectClipRgn(mem, nullptr);
        DeleteObject(clip);
    }

    BitBlt(dis->hDC, item.left, item.top, w, h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}


// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void				RefreshProcessList();
void                CreateTable(HWND);
void                PopulateProcessList(HWND, const std::vector<Process>&);
static void         LayoutControls(HWND);   // UI
void                UpdateSystemStats();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    MyRegisterClass(hInstance);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES;

    InitCommonControlsEx(&icc);

    // Perform application initialization:
    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PROJECTMANAGER));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszMenuName = nullptr;
    wcex.hbrBackground = hBrushBg;   // UI: fondo oscuro
    wcex.lpszClassName = windowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable

    HWND hWnd = CreateWindowW(
        windowClass,
        windowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 720,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

// FUNCTION: RefreshProcessList()
//
// PURPOSE: Refreshes the list of processes displayed in the list view.
//
static void RefreshProcessList()
{
    ListView_DeleteAllItems(hProcessList);

    currentProcesses = processManager.GetProcesses();

    if (previousProcesses.empty())
    {
        previousProcesses = currentProcesses;
    }
    else
    {
        // Detectar nuevos procesos
        for (const auto& current : currentProcesses)
        {
            bool found = false;

            for (const auto& previous : previousProcesses)
            {
                if (current.pid == previous.pid)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                // Proceso nuevo detectado
            }
        }

        // Detectar procesos terminados
        for (const auto& previous : previousProcesses)
        {
            bool found = false;

            for (const auto& current : currentProcesses)
            {
                if (previous.pid == current.pid)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                // Proceso terminado detectado
            }
        }
    }

    PopulateProcessList(
        hProcessList,
        currentProcesses
    );

    previousProcesses = currentProcesses;
}

//
// FUNCTION: CreateTable(HWND)
//
// PURPOSE: Creates the list view control to display processes.
//
static void CreateTable(HWND hWnd) {
    // Create a list view to display processes
    // UI: sin WS_BORDER para un aspecto más limpio (el tamaño lo fija LayoutControls)
    hProcessList = CreateWindowW(
        WC_LISTVIEW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT,
        20, 100,
        1024, 400,
        hWnd,
        (HMENU)ID_LIST_PROCESSES,
        hInst,
        nullptr
    );

    // UI: fila completa resaltada, sin parpadeo, y tema "Explorer" (selección suave, scrollbars finas)
    ListView_SetExtendedListViewStyle(
        hProcessList,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER
    );
    SetWindowTheme(hProcessList, L"DarkMode_Explorer", nullptr);   // UI: scrollbars y selección oscuras
    ListView_SetBkColor(hProcessList, kListBgColor);
    ListView_SetTextBkColor(hProcessList, kListBgColor);
    ListView_SetTextColor(hProcessList, kTextColor);

    // Add a column for the process name
    LVCOLUMN nameColumn{};
    nameColumn.mask = LVCF_TEXT | LVCF_WIDTH;
    nameColumn.cx = 250;
    nameColumn.pszText = const_cast<LPWSTR>(L"Name");
    ListView_InsertColumn(hProcessList, 0, &nameColumn);

    // Add a second column for the PID
    // UI: columnas numéricas alineadas a la derecha (LVCF_FMT + LVCFMT_RIGHT)
    LVCOLUMN pidColumn{};
    pidColumn.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    pidColumn.fmt = LVCFMT_RIGHT;
    pidColumn.cx = 100;
    pidColumn.pszText = const_cast<LPWSTR>(L"PID");
    ListView_InsertColumn(hProcessList, 1, &pidColumn);

    // Add a third column for memory usage
    LVCOLUMN memoryColumn{};
    memoryColumn.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    memoryColumn.fmt = LVCFMT_RIGHT;
    memoryColumn.cx = 100;
    memoryColumn.pszText = const_cast<LPWSTR>(L"Memory");
    ListView_InsertColumn(hProcessList, 2, &memoryColumn);

    // Add a fourth column for thread count
    LVCOLUMN threadsColumn{};
    threadsColumn.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    threadsColumn.fmt = LVCFMT_RIGHT;
    threadsColumn.cx = 100;
    threadsColumn.pszText = const_cast<LPWSTR>(L"Threads");
    ListView_InsertColumn(hProcessList, 3, &threadsColumn);

    // Add a fifth column for the executable path
    LVCOLUMN pathColumn{};
    pathColumn.mask = LVCF_TEXT | LVCF_WIDTH;
    pathColumn.cx = 470;
    pathColumn.pszText = const_cast<LPWSTR>(L"Path");
    ListView_InsertColumn(hProcessList, 4, &pathColumn);

    // Add a sixth column for CPU time
    LVCOLUMN cpuTimeColumn{};
    cpuTimeColumn.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    cpuTimeColumn.fmt = LVCFMT_RIGHT;
    cpuTimeColumn.cx = 100;
    cpuTimeColumn.pszText = const_cast<LPWSTR>(L"CPU Time");
    ListView_InsertColumn(hProcessList, 5, &cpuTimeColumn);
}

// UI: aplica la fuente a un control hijo (usado con EnumChildWindows)
static BOOL CALLBACK SetFontProc(HWND hChild, LPARAM lParam)
{
    SendMessageW(hChild, WM_SETFONT, static_cast<WPARAM>(lParam), TRUE);
    return TRUE;
}

// UI: coloca los controles según el tamaño de la ventana (solo posiciones y tamaños)
static void LayoutControls(HWND hWnd)
{
    if (!hProcessList || !hTitleLabel || !hRefreshButton ||
        !hTerminateButton || !hProcessNameLabel ||
        !hCpuLabel || !hMemoryLabel ||
        !hProcessCountLabel || !hThreadCountLabel)
    {
        return;
    }

    RECT rc;
    GetClientRect(hWnd, &rc);

    const int margin = 24;
    const int gap = 8;

    const int buttonH = 32;
    const int refreshW = 110;
    const int terminateW = 150;

    const int headerY = 20;

    const int statsY = headerY + buttonH + 12;
    const int statsH = 76;   // UI: altura de las tarjetas de estadísticas

    const int listY = statsY + statsH + 12;

    const int detailsH = 90;

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    int terminateX = w - margin - terminateW;
    int refreshX = terminateX - gap - refreshW;

    // Cabecera
    MoveWindow(
        hTitleLabel,
        margin,
        headerY,
        (std::max)(0, refreshX - margin - gap),
        buttonH,
        TRUE
    );

    MoveWindow(
        hRefreshButton,
        refreshX,
        headerY,
        refreshW,
        buttonH,
        TRUE
    );

    MoveWindow(
        hTerminateButton,
        terminateX,
        headerY,
        terminateW,
        buttonH,
        TRUE
    );

    // Estadísticas del sistema
    // UI: 4 tarjetas que se reparten el ancho disponible
    int statsWidth = (std::max)(0, w - 2 * margin);

    const int cardGap = 12;
    int cardW = (std::max)(0, (statsWidth - 3 * cardGap) / 4);

    HWND cards[4] = { hCpuLabel, hMemoryLabel, hProcessCountLabel, hThreadCountLabel };

    for (int i = 0; i < 4; i++)
    {
        MoveWindow(
            cards[i],
            margin + i * (cardW + cardGap),
            statsY,
            cardW,
            statsH,
            TRUE
        );
    }

    // Lista de procesos
    int listH =
        (std::max)(
            0,
            h - listY - detailsH - margin - gap
            );

    MoveWindow(
        hProcessList,
        margin,
        listY,
        statsWidth,
        listH,
        TRUE
    );

    // Detalles del proceso seleccionado
    MoveWindow(
        hProcessNameLabel,
        margin,
        h - margin - detailsH,
        statsWidth,
        detailsH,
        TRUE
    );
}


//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        // UI: fuentes Segoe UI (cuerpo, valor de tarjeta y título), escaladas según DPI
        UINT dpi = GetDpiForWindow(hWnd);
        if (dpi == 0) dpi = 96;

        hFontUI = CreateFontW(
            -MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        hFontValue = CreateFontW(
            -MulDiv(13, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        hFontTitle = CreateFontW(
            -MulDiv(15, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Create a static text label
        hTitleLabel = CreateWindowW(
            L"STATIC",
            L"Procesos en ejecución",
            WS_CHILD | WS_VISIBLE,
            20, 20,
            200, 25,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        // Create the button to refresh the process list
        hRefreshButton = CreateWindowW(L"BUTTON", L"Actualizar", WS_CHILD | WS_VISIBLE, 20, 60, 100, 30, hWnd, (HMENU)ID_BUTTON_REFRESH, hInst, nullptr);

        // Create the list view to display processes
        CreateTable(hWnd);

        // Create the section to display the process info
        hProcessNameLabel = CreateWindowW(
            L"STATIC",
            L"Selecciona un proceso",
            WS_CHILD | WS_VISIBLE,
            20, 540,
            220, 80,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        // Create the button to terminate a process
        hTerminateButton = CreateWindowW(
            L"BUTTON",
            L"Finalizar proceso",
            WS_CHILD | WS_VISIBLE,
            150, 60,
            150, 30,
            hWnd,
            (HMENU)ID_BUTTON_TERMINATE,
            hInst,
            nullptr
        );

        // UI: las 4 etiquetas de estadísticas son SS_OWNERDRAW (se dibujan como tarjetas)
        hCpuLabel = CreateWindowW(
            L"STATIC",
            L"CPU: --",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0,
            150, 25,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        hMemoryLabel = CreateWindowW(
            L"STATIC",
            L"RAM: --",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0,
            200, 25,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        hProcessCountLabel = CreateWindowW(
            L"STATIC",
            L"Procesos: --",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0,
            150, 25,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        hThreadCountLabel = CreateWindowW(
            L"STATIC",
            L"Hilos: --",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0,
            150, 25,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        // UI: aplicar fuentes a todos los controles y colocarlos
        EnumChildWindows(hWnd, SetFontProc, reinterpret_cast<LPARAM>(hFontUI));
        SendMessageW(hTitleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(hFontTitle), TRUE);
        SendMessageW(hCpuLabel, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUI), TRUE);
        SendMessageW(hMemoryLabel, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUI), TRUE);
        SendMessageW(hProcessCountLabel, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUI), TRUE);
        SendMessageW(hThreadCountLabel, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUI), TRUE);

        // UI: barra de título y botones en oscuro
        BOOL useDark = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
        SetWindowTheme(hRefreshButton, L"DarkMode_Explorer", nullptr);
        SetWindowTheme(hTerminateButton, L"DarkMode_Explorer", nullptr);
        LayoutControls(hWnd);
    }

    SetTimer(hWnd, ID_TIMER_REFRESH, 1000 * 60, nullptr); // Set a timer to refresh every minute
    SetTimer(
        hWnd,
        ID_TIMER_SYSTEM_STATS,
        1000,
        nullptr
    );
    break;

    // UI: reajustar el layout al cambiar el tamaño de la ventana
    case WM_SIZE:
        LayoutControls(hWnd);
        break;

        // UI: los textos estáticos usan fondo oscuro; el detalle del proceso va en gris suave
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND hCtl = reinterpret_cast<HWND>(lParam);

        SetBkColor(hdc, kBgColor);
        SetTextColor(hdc, hCtl == hProcessNameLabel ? kMutedColor : kTextColor);

        return reinterpret_cast<LRESULT>(hBrushBg);
    }

    // UI: dibujo de las tarjetas de estadísticas (solo presentación)
    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);

        if (dis->CtlType == ODT_STATIC &&
            (dis->hwndItem == hCpuLabel ||
                dis->hwndItem == hMemoryLabel ||
                dis->hwndItem == hProcessCountLabel ||
                dis->hwndItem == hThreadCountLabel))
        {
            DrawStatCard(dis);
            return TRUE;
        }

        break;
    }

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);

        switch (wmId) {
        case ID_BUTTON_REFRESH:
            RefreshProcessList();
            break;
        case ID_BUTTON_TERMINATE:
        {
            const Process* selectedProcess = nullptr;

            if (selectedProcessId == 0)
            {
                MessageBoxW(
                    hWnd,
                    L"No hay ningún proceso seleccionado.",
                    L"Finalizar proceso",
                    MB_OK | MB_ICONINFORMATION
                );

                break;
            }

            for (const auto& process : currentProcesses)
            {
                if (process.pid == selectedProcessId)
                {
                    selectedProcess = &process;
                    break;
                }
            }

            if (selectedProcess == nullptr)
            {
                MessageBoxW(
                    hWnd,
                    L"El proceso seleccionado ya no está disponible.",
                    L"Finalizar proceso",
                    MB_OK | MB_ICONWARNING
                );

                break;
            }

            std::wstring message =
                L"¿Seguro que quieres finalizar el proceso?\n\n" +
                selectedProcess->name;

            int result = MessageBoxW(
                hWnd,
                message.c_str(),
                L"Confirmar finalización",
                MB_YESNO | MB_ICONWARNING
            );

            if (result == IDYES)
            {
                HANDLE processHandle = OpenProcess(
                    PROCESS_TERMINATE | SYNCHRONIZE,
                    FALSE,
                    selectedProcessId
                );

                if (processHandle == nullptr)
                {
                    DWORD error = GetLastError();

                    ProcessManager processManager;

                    std::wstring windowsError =
                        processManager.GetWindowsErrorMessage(error);

                    std::wstring errorMessage =
                        L"No se pudo abrir el proceso.\n\n"
                        L"Código: " + std::to_wstring(error) +
                        L"\n" + windowsError;

                    MessageBoxW(
                        hWnd,
                        errorMessage.c_str(),
                        L"Error al abrir el proceso",
                        MB_OK | MB_ICONERROR
                    );
                }
                else
                {
                    if (TerminateProcess(processHandle, 0))
                    {
                        selectedProcessId = 0;

                        SetWindowTextW(
                            hProcessNameLabel,
                            L"Selecciona un proceso"
                        );

                        terminatingProcessHandle = processHandle;

                        SetTimer(
                            hWnd,
                            ID_TIMER_TERMINATING,
                            50,
                            nullptr
                        );
                    }
                    else
                    {
                        DWORD error = GetLastError();

                        ProcessManager processManager;

                        std::wstring windowsError =
                            processManager.GetWindowsErrorMessage(error);

                        std::wstring errorMessage =
                            L"No se pudo finalizar el proceso.\n\n"
                            L"Código: " + std::to_wstring(error) +
                            L"\n" + windowsError;

                        MessageBoxW(
                            hWnd,
                            errorMessage.c_str(),
                            L"Error al finalizar el proceso",
                            MB_OK | MB_ICONERROR
                        );

                        CloseHandle(processHandle);
                    }
                }
            }

            break;
        }
        }
    }
    break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // Here you can add any painting code if needed.
        EndPaint(hWnd, &ps);
    }
    break;
    case WM_NOTIFY:
    {
        NMHDR* header = reinterpret_cast<NMHDR*>(lParam);

        // UI: cabecera del ListView en oscuro (solo dibujo)
        if (hProcessList != nullptr &&
            header->hwndFrom == ListView_GetHeader(hProcessList) &&
            header->code == NM_CUSTOMDRAW)
        {
            return DrawDarkHeader(lParam);
        }

        if (header->idFrom == ID_LIST_PROCESSES)
        {
            // Handle notifications from the list view if needed
            if (header->code == LVN_ITEMCHANGED)
            {
                NMLISTVIEW* listView =
                    reinterpret_cast<NMLISTVIEW*>(lParam);

                if ((listView->uNewState & LVIS_SELECTED) != 0)
                {
                    int selectedIndex = listView->iItem;

                    selectedProcessId =
                        currentProcesses[selectedIndex].pid;

                    const Process* selectedProcess = nullptr;

                    for (const auto& process : currentProcesses)
                    {
                        if (process.pid == selectedProcessId)
                        {
                            selectedProcess = &process;
                            break;
                        }
                    }

                    if (selectedProcess != nullptr)
                    {
                        std::wstring details =
                            L"Nombre: " + selectedProcess->name +
                            L"\nPID: " + std::to_wstring(selectedProcess->pid) +
                            L"\nMemoria: " +
                            std::to_wstring(selectedProcess->memory / (1024 * 1024)) +
                            L" MB" +
                            L"\nHilos: " +
                            std::to_wstring(selectedProcess->threads);

                        SetWindowTextW(hProcessNameLabel, details.c_str());
                    }
                }
            }
        }
    }
    break;
    case WM_TIMER:
    {
        if (wParam == ID_TIMER_REFRESH)
        {
            RefreshProcessList();
        }
        else if (wParam == ID_TIMER_TERMINATING)
        {
            if (terminatingProcessHandle != nullptr)
            {
                DWORD result = WaitForSingleObject(
                    terminatingProcessHandle,
                    0
                );

                if (result == WAIT_OBJECT_0)
                {
                    KillTimer(hWnd, ID_TIMER_TERMINATING);

                    CloseHandle(terminatingProcessHandle);

                    terminatingProcessHandle = nullptr;

                    RefreshProcessList();
                }
            }
        }
        else if (wParam == ID_TIMER_SYSTEM_STATS)
        {
            UpdateSystemStats();
        }

        break;
    }
    case WM_DESTROY:
    {
        KillTimer(hWnd, ID_TIMER_REFRESH);
        KillTimer(hWnd, ID_TIMER_TERMINATING);
        KillTimer(hWnd, ID_TIMER_SYSTEM_STATS);

        if (terminatingProcessHandle != nullptr)
        {
            CloseHandle(terminatingProcessHandle);
            terminatingProcessHandle = nullptr;
        }

        // UI: liberar las fuentes creadas en WM_CREATE
        if (hFontUI) { DeleteObject(hFontUI); hFontUI = nullptr; }
        if (hFontTitle) { DeleteObject(hFontTitle); hFontTitle = nullptr; }
        if (hFontValue) { DeleteObject(hFontValue); hFontValue = nullptr; }

        PostQuitMessage(0);

        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

//
// FUNCTION: PopulateProcessList(HWND, const std::vector<Process>&)
//
// PURPOSE: Populates the list view with the given processes.
//
//
static void PopulateProcessList(HWND hWnd, const std::vector<Process>& processes) {
    for (std::size_t i = 0; i < processes.size(); i++) {
        // Add the process name column
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0; // First column
        item.pszText = const_cast<LPWSTR>(processes[i].name.c_str());
        ListView_InsertItem(hProcessList, &item);

        // Add the PID column
        LVITEMW pidItem{};
        pidItem.mask = LVIF_TEXT;
        pidItem.iItem = static_cast<int>(i);
        pidItem.iSubItem = 1; // Second column
        std::wstring pidText = std::to_wstring(processes[i].pid);
        pidItem.pszText = const_cast<LPWSTR>(pidText.c_str());
        ListView_SetItem(hProcessList, &pidItem);

        // Add the memory usage column
        LVITEMW memoryItem{};
        memoryItem.mask = LVIF_TEXT;
        memoryItem.iItem = static_cast<int>(i);
        memoryItem.iSubItem = 2; // Third column
        std::wstring memoryText = std::to_wstring(processes[i].memory / (1024 * 1024)) + L" MB";
        memoryItem.pszText = const_cast<LPWSTR>(memoryText.c_str());
        ListView_SetItem(hProcessList, &memoryItem);

        // Add the threads column
        LVITEMW threadsItem{};
        threadsItem.mask = LVIF_TEXT;
        threadsItem.iItem = static_cast<int>(i);
        threadsItem.iSubItem = 3; // Fourth column
        std::wstring threadsText = std::to_wstring(processes[i].threads);
        threadsItem.pszText = const_cast<LPWSTR>(threadsText.c_str());
        ListView_SetItem(hProcessList, &threadsItem);

        // Add the path column
        std::wstring pathText = processes[i].path;
        LVITEMW pathItem{};
        pathItem.mask = LVIF_TEXT;
        pathItem.iItem = static_cast<int>(i);
        pathItem.iSubItem = 4; // Fifth column
        pathItem.pszText = const_cast<LPWSTR>(pathText.c_str());
        ListView_SetItem(hProcessList, &pathItem);

        // Convert CPU time from 100-nanosecond intervals to seconds
        double cpuSeconds = static_cast<double>(processes[i].cpuTime) / 10'000'000.0;
        std::wstringstream cpuStream;
        cpuStream << std::fixed << std::setprecision(2) << cpuSeconds << L" s";
        std::wstring cpuText = cpuStream.str();

        LVITEMW cpuTimeItem{};
        cpuTimeItem.mask = LVIF_TEXT;
        cpuTimeItem.iItem = static_cast<int>(i);
        cpuTimeItem.iSubItem = 5; // Sixth column
        std::wstring cpuTimeText = const_cast<LPWSTR>(cpuText.c_str());
        cpuTimeItem.pszText = const_cast<LPWSTR>(cpuTimeText.c_str());
        ListView_SetItem(hProcessList, &cpuTimeItem);
    }
}

static void UpdateSystemStats()
{
    double cpuUsage = systemInfo.GetCpuUsage();
    SystemMemory memory = systemInfo.GetMemoryInfo();

    double totalGB =
        static_cast<double>(memory.total) /
        (1024.0 * 1024.0 * 1024.0);

    double availableGB =
        static_cast<double>(memory.available) /
        (1024.0 * 1024.0 * 1024.0);

    double usedGB = totalGB - availableGB;

    std::uint32_t totalThreads = 0;

    for (const auto& process : currentProcesses)
    {
        totalThreads += process.threads;
    }

    // CPU
    std::wstringstream cpuText;

    cpuText << L"CPU: "
        << std::fixed
        << std::setprecision(1)
        << cpuUsage
        << L"%";

    SetWindowTextW(
        hCpuLabel,
        cpuText.str().c_str()
    );

    // RAM
    std::wstringstream memoryText;

    memoryText << L"RAM: "
        << std::fixed
        << std::setprecision(1)
        << usedGB
        << L" / "
        << totalGB
        << L" GB ("
        << memory.usagePercent
        << L"%)";

    SetWindowTextW(
        hMemoryLabel,
        memoryText.str().c_str()
    );

    // Procesos
    std::wstringstream processText;

    processText << L"Procesos: "
        << currentProcesses.size();

    SetWindowTextW(
        hProcessCountLabel,
        processText.str().c_str()
    );

    // Hilos
    std::wstringstream threadText;

    threadText << L"Hilos: "
        << totalThreads;

    SetWindowTextW(
        hThreadCountLabel,
        threadText.str().c_str()
    );
}