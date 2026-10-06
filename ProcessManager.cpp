#include "ProcessManager.h"

#include "windows.h"
#include "psapi.h"
#include <TlHelp32.h>

std::vector<Process> ProcessManager::GetProcesses()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPPROCESS,
        0
    );

    std::vector<Process> processes;

    if (snapshot == INVALID_HANDLE_VALUE)
        return {};

    PROCESSENTRY32 processEntry{};
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(snapshot, &processEntry))
    {
        CloseHandle(snapshot);
        return {};
    }

    do
    {
        Process process{
            processEntry.szExeFile,
            processEntry.th32ProcessID,
            0,
            processEntry.cntThreads,
            L"",
            0
        };

        HANDLE processHandle = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processEntry.th32ProcessID
        );

        if (processHandle != nullptr)
        {
            PROCESS_MEMORY_COUNTERS pmc{};
            FILETIME creationTime{}, exitTime{}, kernelTime{}, userTime{};

			// Get process memory information
            if (GetProcessMemoryInfo(
                processHandle,
                &pmc,
                sizeof(pmc)
            ))
            {
                process.memory = pmc.WorkingSetSize;
            }

			// Get process path
            wchar_t path[MAX_PATH];
            DWORD pathSize = MAX_PATH;

            if (QueryFullProcessImageNameW(processHandle, 0, path, &pathSize)) {
                process.path = path;
            }
            else {
				process.path = L"Unknown";
            }

			// Get process times
            if (GetProcessTimes(
                processHandle,
                &creationTime,
                &exitTime,
                &kernelTime,
                &userTime
            )) {
                ULARGE_INTEGER kernel{};
                kernel.HighPart = kernelTime.dwHighDateTime;
                kernel.LowPart = kernelTime.dwLowDateTime;

                ULARGE_INTEGER user{};
                user.HighPart = userTime.dwHighDateTime;
                user.LowPart = userTime.dwLowDateTime;

                process.cpuTime = (kernel.QuadPart + user.QuadPart);
			}

            CloseHandle(processHandle);
        }
        else {
			process.path = L"Unknown";
        }

        processes.push_back(process);

    } while (Process32Next(snapshot, &processEntry));

    CloseHandle(snapshot);

    return processes;
}