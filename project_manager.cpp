// project_manager.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "project_manager.h"
#include "ProcessManager.h"
#include <CommCtrl.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "Comctl32.lib")

#define ID_BUTTON_REFRESH       1001
#define ID_LIST_PROCESSES       1002
#define ID_BUTTON_TERMINATE     1003
#define ID_TIMER_REFRESH        1

// Global Variables:
HINSTANCE hInst;                                // current instance
const wchar_t* windowTitle = L"Gestor de Procesos";
const wchar_t* windowClass = L"ProcessManagerWindow";
ProcessManager processManager; // Instance of ProcessManager
HWND hProcessList;
std::vector<Process> previousProcesses;
std::vector<Process> currentProcesses; // Global variable to hold the current list of processes
std::uint32_t selectedProcessId = 0;
HWND hProcessNameLabel;


// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
void				RefreshProcessList();
void                CreateTable(HWND);
void                PopulateProcessList(HWND, const std::vector<Process>&);

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
    if (!InitInstance (hInstance, nCmdShow))
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

    return (int) msg.wParam;
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

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PROJECTMANAGER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
	wcex.lpszMenuName   = nullptr;
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszClassName  = windowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

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
static void RefreshProcessList() {
    // Clear the existing items in the list view
    ListView_DeleteAllItems(hProcessList);

    // Here we will handle the refresh button click event.
    currentProcesses = processManager.GetProcesses();

    if (previousProcesses.empty()) {
		previousProcesses = currentProcesses; // Initialize previousProcesses on first run
    }
    else {
        // Compare the new list of processes with the previous one to detect new processes
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
                // Proceso nuevo
                MessageBoxW(
                    hProcessList,
                    current.name.c_str(),
                    L"Nuevo proceso detectado",
                    MB_OK
                );
            }
        }

		// Compare the previous list of processes with the new one to detect terminated processes
        for (const auto& prevState : previousProcesses)
        {
            bool found = false;
            for (const auto& current : currentProcesses)
            {
                if (prevState.pid == current.pid)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                                // Proceso terminado
                MessageBoxW(
                    hProcessList,
                    prevState.name.c_str(),
                    L"Proceso terminado detectado",
                    MB_OK
				);
            }
        }
    }

	// Populate the list view with the current processes
    PopulateProcessList(hProcessList, currentProcesses);

	previousProcesses = currentProcesses; // Update previousProcesses for the next refresh
}

//
// FUNCTION: CreateTable(HWND)
//
// PURPOSE: Creates the list view control to display processes.
//
static void CreateTable(HWND hWnd) {
    // Create a list view to display processes
    hProcessList = CreateWindowW(
        WC_LISTVIEW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT,
        20, 100,
        1024, 400,
        hWnd,
        (HMENU)ID_LIST_PROCESSES,
        hInst,
        nullptr
    );

    // Add a column for the process name
    LVCOLUMN nameColumn{};
    nameColumn.mask = LVCF_TEXT | LVCF_WIDTH;
    nameColumn.cx = 250;
    nameColumn.pszText = const_cast<LPWSTR>(L"Name");
    ListView_InsertColumn(hProcessList, 0, &nameColumn);

    // Add a second column for the PID
    LVCOLUMN pidColumn{};
    pidColumn.mask = LVCF_TEXT | LVCF_WIDTH;
    pidColumn.cx = 100;
    pidColumn.pszText = const_cast<LPWSTR>(L"PID");
    ListView_InsertColumn(hProcessList, 1, &pidColumn);

    // Add a third column for memory usage
    LVCOLUMN memoryColumn{};
    memoryColumn.mask = LVCF_TEXT | LVCF_WIDTH;
    memoryColumn.cx = 100;
    memoryColumn.pszText = const_cast<LPWSTR>(L"Memory");
    ListView_InsertColumn(hProcessList, 2, &memoryColumn);

    // Add a fourth column for thread count
    LVCOLUMN threadsColumn{};
    threadsColumn.mask = LVCF_TEXT | LVCF_WIDTH;
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
    cpuTimeColumn.mask = LVCF_TEXT | LVCF_WIDTH;
    cpuTimeColumn.cx = 100;
    cpuTimeColumn.pszText = const_cast<LPWSTR>(L"CPU Time");
	ListView_InsertColumn(hProcessList, 5, &cpuTimeColumn);
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
		// Create a static text label
        CreateWindowW(
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
        CreateWindowW(L"BUTTON", L"Actualizar", WS_CHILD | WS_VISIBLE, 20, 60, 100, 30, hWnd, (HMENU)ID_BUTTON_REFRESH, hInst, nullptr);

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
        CreateWindowW(
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
        }

		SetTimer(hWnd, ID_TIMER_REFRESH, 1000 * 60 * 5, nullptr); // Set a timer to refresh every 5 minutes
        break;

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
                            PROCESS_TERMINATE,
                            FALSE,
                            selectedProcessId
                        );

                        if (processHandle == nullptr)
                        {
                            DWORD error = GetLastError();
                            std::wstring windowsError =
                                processManager.GetWindowsErrorMessage(error);

                            std::wstring message =
                                L"No se pudo abrir el proceso.\n\n"
                                L"Código: " + std::to_wstring(error) +
                                L"\n" + windowsError;

                            MessageBoxW(
                                hWnd,
                                message.c_str(),
                                L"Error",
                                MB_OK | MB_ICONERROR
                            );
                        }
                        else
                        {
                            if (TerminateProcess(processHandle, 0))
                            {
                                // Éxito
                                selectedProcessId = 0;
                                SetWindowTextW(hProcessNameLabel, L"Selecciona un proceso");
                                RefreshProcessList();
                            }
                            else
                            {
                                DWORD error = GetLastError();
                                std::wstring windowsError =
                                    processManager.GetWindowsErrorMessage(error);
                                std::wstring message =
                                    L"No se pudo finalizar el proceso.\n\n"
                                    L"Código: " + std::to_wstring(error) +
                                    L"\n" + windowsError;

                                MessageBoxW(
                                    hWnd,
                                    message.c_str(),
                                    L"Error al finalizar el proceso",
                                    MB_OK | MB_ICONERROR
                                );
                            }

                            CloseHandle(processHandle);
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
        if (wParam == ID_TIMER_REFRESH) {
            RefreshProcessList();
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
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
        std::uint64_t cpuSeconds = static_cast<double>(processes[i].cpuTime) / 10'000'000.0;
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