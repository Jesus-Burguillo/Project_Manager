#include "SystemInfo.h"
#include "windows.h"

SystemMemory SystemInfo::GetMemoryInfo()
{
	MEMORYSTATUSEX memoryStatus{};
	memoryStatus.dwLength = sizeof(MEMORYSTATUSEX);

	if (!GlobalMemoryStatusEx(&memoryStatus))
	{
		// Error
		return SystemMemory{ 0, 0, 0 };
	}

	return SystemMemory{
		memoryStatus.ullTotalPhys,
		memoryStatus.ullAvailPhys,
		memoryStatus.dwMemoryLoad
	};
}

double SystemInfo::GetCpuUsage()
{
	FILETIME idleTime{};
	FILETIME kernelTime{};
	FILETIME userTime{};

	if (!GetSystemTimes(
		&idleTime,
		&kernelTime,
		&userTime
	))
	{
		return 0.0;
	}

	ULARGE_INTEGER idle{};
	idle.HighPart = idleTime.dwHighDateTime;
	idle.LowPart = idleTime.dwLowDateTime;

	ULARGE_INTEGER kernel{};
	kernel.HighPart = kernelTime.dwHighDateTime;
	kernel.LowPart = kernelTime.dwLowDateTime;

	ULARGE_INTEGER user{};
	user.HighPart = userTime.dwHighDateTime;
	user.LowPart = userTime.dwLowDateTime;

	CpuTimes currentCpuTimes{
		idle.QuadPart,
		kernel.QuadPart,
		user.QuadPart
	};

	if (!hasPreviousCpuTimes)
	{
		previousCpuTimes = currentCpuTimes;
		hasPreviousCpuTimes = true;

		return 0.0;
	}

	std::uint64_t idleDelta =
		currentCpuTimes.idle - previousCpuTimes.idle;

	std::uint64_t kernelDelta =
		currentCpuTimes.kernel - previousCpuTimes.kernel;

	std::uint64_t userDelta =
		currentCpuTimes.user - previousCpuTimes.user;

	std::uint64_t totalDelta =
		kernelDelta + userDelta;
	std::uint64_t busyDelta =
		totalDelta - idleDelta;

	if (totalDelta == 0)
	{
		previousCpuTimes = currentCpuTimes;
		return 0.0;
	}

	// Calcular el porcentaje de uso total (%)
	double cpuUsage =
		static_cast<double>(busyDelta) /
		static_cast<double>(totalDelta) *
		100.0;

	previousCpuTimes = currentCpuTimes;

	return cpuUsage;
}