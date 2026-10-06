#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct Process {
	std::wstring name;
	std::uint32_t pid;
	std::uint64_t memory;
	std::uint32_t threads;
	std::wstring path;
	std::uint64_t cpuTime;
};

class ProcessManager {
public:
	std::vector<Process> GetProcesses();
};