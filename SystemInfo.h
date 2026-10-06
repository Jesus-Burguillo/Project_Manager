#pragma once

#include <cstdint>

struct SystemMemory
{
	std::uint64_t total;
	std::uint64_t available;
	std::uint32_t usagePercent;
};

struct CpuTimes
{
	std::uint64_t idle;
	std::uint64_t kernel;
	std::uint64_t user;
};

class SystemInfo
{
private:
	CpuTimes previousCpuTimes{};
	bool hasPreviousCpuTimes = false;

public:
	SystemMemory GetMemoryInfo();
	double GetCpuUsage();
};