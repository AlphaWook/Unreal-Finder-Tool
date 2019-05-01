#include "pch.h"
#include "Memory.h"
#include <vector>
#include <TlHelp32.h>
#include <psapi.h>
#include <iostream>

Memory::Memory(const HANDLE processHandle, const bool useKernal)
{
	if (processHandle == nullptr || processHandle == INVALID_HANDLE_VALUE)
		throw std::exception("processId can't be NULL");

	ProcessHandle = processHandle;
	ProcessId = GetProcessId(processHandle);
	use_kernal = useKernal;
	if (useKernal)
		bypa_ph = new BypaPH(ProcessId);

	IsWow64Process(ProcessHandle, &Is64Bit);
	Is64Bit = !Is64Bit;
}

Memory::Memory(const int processId, const bool useKernal)
{
	if (processId == 0)
		return;

	ProcessHandle = OpenProcess(0x0 | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, processId);
	ProcessId = GetProcessId(ProcessHandle);
	use_kernal = useKernal;
	if (useKernal)
		bypa_ph = new BypaPH(ProcessId);

	IsWow64Process(ProcessHandle, &Is64Bit);
	Is64Bit = !Is64Bit;
}

Memory::~Memory()
{
	delete bypa_ph;
	CloseHandle(ProcessHandle);
}

int Memory::GetProcessIdByName(char* processName)
{
	PROCESSENTRY32 pe32;
	HANDLE hSnapshot = nullptr;
	pe32.dwSize = sizeof(PROCESSENTRY32);
	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (Process32First(hSnapshot, &pe32)) {
		do
		{
			if (strcmp(pe32.szExeFile, processName) == 0)
				break;
		} while (Process32Next(hSnapshot, &pe32));
	}

	if (hSnapshot != INVALID_HANDLE_VALUE)
		CloseHandle(hSnapshot);
	const int err = GetLastError();
	//std::cout << err << std::endl;
	if (err != 0)
		return 0;
	return pe32.th32ProcessID;
}

uintptr_t Memory::GetModuleBase(const string sModuleName)  // NOLINT
{
	HMODULE *hModules = nullptr;
	char szBuf[50];
	DWORD cModules;
	uintptr_t dwBase = -1;

	EnumProcessModules(ProcessHandle, hModules, 0, &cModules);
	hModules = new HMODULE[cModules / sizeof(HMODULE)];

	if (EnumProcessModules(ProcessHandle, hModules, cModules / sizeof(HMODULE), &cModules)) {
		for (size_t i = 0; i < cModules / sizeof(HMODULE); i++) {
			if (GetModuleBaseNameA(ProcessHandle, hModules[i], szBuf, sizeof(szBuf))) {
				if (sModuleName == szBuf) {
					dwBase = reinterpret_cast<uintptr_t>(hModules[i]);
					break;
				}
			}
		}
	}

	delete[] hModules;
	return dwBase;
}

BOOL Memory::SetPrivilegeM(HANDLE hToken, const LPCTSTR lpszPrivilege, const BOOL bEnablePrivilege)
{
	TOKEN_PRIVILEGES tp;
	LUID luid;

	if (!LookupPrivilegeValue(nullptr, lpszPrivilege, &luid)) {
		//printf("LookupPrivilegeValue error: %u\n", GetLastError() );
		return FALSE;
	}

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	if (bEnablePrivilege)
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	else
		tp.Privileges[0].Attributes = 0;

	if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), static_cast<PTOKEN_PRIVILEGES>(nullptr), static_cast<PDWORD>(nullptr))) {
		//printf("AdjustTokenPrivileges error: %u\n", GetLastError() );
		return FALSE;
	}

	if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
		//printf("The token does not have the specified privilege. \n");
		return FALSE;
	}

	return TRUE;
}

BOOL Memory::GetDebugPrivileges()
{
	HANDLE hToken = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
		return FALSE;
	
	return SetPrivilegeM(hToken, SE_DEBUG_NAME, TRUE);
}

SIZE_T Memory::ReadBytes(const uintptr_t address, BYTE* buf, const int len)
{
	if (address == static_cast<uintptr_t>(-1))
		return 0;

	SIZE_T numberOfBytesActuallyRead = 0;
	const SIZE_T numberOfBytesToRead = len;

	if (use_kernal)
	{
		const auto state = bypa_ph->RWVM(bypa_ph->m_hTarget,
			reinterpret_cast<PVOID>(address),
			buf,
			numberOfBytesToRead,
			&numberOfBytesActuallyRead);
		if (state != STATUS_PARTIAL_COPY && state != STATUS_SUCCESS)
			std::cout << "Memory Error! " << GetLastError() << std::endl;
	}
	else
	{
		const auto success = ReadProcessMemory(ProcessHandle, reinterpret_cast<LPCVOID>(address), buf, numberOfBytesToRead, &numberOfBytesActuallyRead);
		if (!success && GetLastError() != 299)
			std::cout << "Memory Error! " << GetLastError() << std::endl;
	}
	
	return numberOfBytesToRead;
}

int Memory::ReadInt(const uintptr_t address)
{
	if (address == static_cast<uintptr_t>(-1))
		return -1;

	int buffer = 0;
	const SIZE_T numberOfBytesToRead = sizeof buffer; //this is equal to 4
	SIZE_T numberOfBytesActuallyRead;

	if (use_kernal)
	{
		const auto state = bypa_ph->RWVM(bypa_ph->m_hTarget,
			reinterpret_cast<PVOID>(address),
			&buffer,
			numberOfBytesToRead,
			&numberOfBytesActuallyRead);
		if (state != STATUS_SUCCESS)
			return -1;
	}
	else
	{
		const auto state = ReadProcessMemory(ProcessHandle, reinterpret_cast<LPCVOID>(address), &buffer, numberOfBytesToRead, &numberOfBytesActuallyRead);
		if (!state)
		{
			std::cout << "Memory Error! " << GetLastError() << std::endl;
			return -1;
		}
	}

	return buffer;
}

INT64 Memory::ReadInt64(const uintptr_t address)
{
	if (address == static_cast<uintptr_t>(-1))
		return -1;
	INT64 buffer = 0;
	const SIZE_T numberOfBytesToRead = sizeof(buffer); //this is equal to 8
	SIZE_T numberOfBytesActuallyRead;
	if (use_kernal)
	{
		const auto state = bypa_ph->RWVM(bypa_ph->m_hTarget,
			reinterpret_cast<PVOID>(address),
			&buffer,
			numberOfBytesToRead,
			&numberOfBytesActuallyRead);
		if (state != STATUS_SUCCESS)
			return -1;
	}
	else
	{
		const auto state = ReadProcessMemory(ProcessHandle, reinterpret_cast<LPCVOID>(address), &buffer, numberOfBytesToRead, &numberOfBytesActuallyRead);
		if (!state)
		{
			std::cout << "Memory Error! " << GetLastError() << std::endl;
			return -1;
		}
	}
	return buffer;
}

UINT32 Memory::ReadUInt(const uintptr_t address)
{
	if (address == static_cast<uintptr_t>(-1))
		return -1;

	UINT32 buffer = 0;
	const SIZE_T numberOfBytesToRead = sizeof buffer; //this is equal to 4
	SIZE_T numberOfBytesActuallyRead;
	if (use_kernal)
	{
		const auto state = bypa_ph->RWVM(bypa_ph->m_hTarget,
			reinterpret_cast<PVOID>(address),
			&buffer,
			numberOfBytesToRead,
			&numberOfBytesActuallyRead);
		if (state != STATUS_SUCCESS)
			return -1;
	}
	else
	{
		const auto state = ReadProcessMemory(ProcessHandle, reinterpret_cast<LPCVOID>(address), &buffer, numberOfBytesToRead, &numberOfBytesActuallyRead);
		if (!state)
		{
			std::cout << "Memory Error! " << GetLastError() << std::endl;
			return -1;
		}
	}
	return buffer;
}

UINT64 Memory::ReadUInt64(const uintptr_t address)
{
	if (address == static_cast<uintptr_t>(-1))
		return -1;
	UINT64 buffer = 0;
	const SIZE_T numberOfBytesToRead = sizeof(buffer); //this is equal to 8
	SIZE_T numberOfBytesActuallyRead;
	if (use_kernal)
	{
		const auto state = bypa_ph->RWVM(bypa_ph->m_hTarget,
			reinterpret_cast<PVOID>(address),
			&buffer,
			numberOfBytesToRead,
			&numberOfBytesActuallyRead);
		if (state != STATUS_SUCCESS)
			return -1;
	}
	else
	{
		const auto state = ReadProcessMemory(ProcessHandle, reinterpret_cast<LPCVOID>(address), &buffer, numberOfBytesToRead, &numberOfBytesActuallyRead);
		if (!state)
		{
			std::cout << "Memory Error! " << GetLastError() << std::endl;
			return -1;
		}
	}
	return buffer;
}

float Memory::ReadFloat(const uintptr_t address) {
	if (address == static_cast<uintptr_t>(-1))
		return -1;

	float buffer = 0.0;
	const SIZE_T numberOfBytesToRead = sizeof(buffer); //this is equal to 4
	SIZE_T numberOfBytesActuallyRead;
	if (use_kernal)
	{
		const auto state = bypa_ph->RWVM(bypa_ph->m_hTarget,
			reinterpret_cast<PVOID>(address),
			&buffer,
			numberOfBytesToRead,
			&numberOfBytesActuallyRead);
		if (state != STATUS_SUCCESS)
			return -1;
	}
	else
	{
		const auto state = ReadProcessMemory(ProcessHandle, reinterpret_cast<LPCVOID>(address), &buffer, numberOfBytesToRead, &numberOfBytesActuallyRead);
		if (!state)
		{
			std::cout << "Memory Error! " << GetLastError() << std::endl;
			return -1;
		}
	}
	return buffer;
}

int Memory::GetPointerAddress(const uintptr_t address, const int offsets[], const int offsetCount) {
	if (address == static_cast<uintptr_t>(-1))
		return -1;
	auto ptr = ReadInt(address);
	for (auto i = 0; i < offsetCount - 1; i++) {
		ptr += offsets[i];
		ptr = ReadInt(ptr);
	}
	ptr += offsets[offsetCount - 1];
	return ptr;
}

int Memory::ReadPointerInt(const uintptr_t address, int offsets[], const int offsetCount) {
	if (address == static_cast<uintptr_t>(-1))
		return -1;

	return ReadInt(GetPointerAddress(address, offsets, offsetCount));
}

float Memory::ReadPointerFloat(const uintptr_t address, int offsets[], int offsetCount) {
	if (address == static_cast<uintptr_t>(-1))
		return -1;
	return ReadFloat(GetPointerAddress(address, offsets, offsetCount));
}

string Memory::ReadText(uintptr_t address)
{
	if (address == static_cast<uintptr_t>(-1))
		return "-1";

	char buffer = 1;
	const auto stringToRead = new char[128];
	const SIZE_T numberOfBytesToRead = sizeof(buffer);
	SIZE_T numberOfBytesActuallyRead;
	auto i = 0;
	while (buffer != 0)
	{
		if (use_kernal)
		{
			const auto state = bypa_ph->RWVM(bypa_ph->m_hTarget,
				reinterpret_cast<PVOID>(address),
				&buffer,
				numberOfBytesToRead,
				&numberOfBytesActuallyRead);
			if (state != STATUS_SUCCESS)
				return "-1";
		}
		else
		{
			const auto state = ReadProcessMemory(ProcessHandle, reinterpret_cast<LPCVOID>(address), &buffer, numberOfBytesToRead, &numberOfBytesActuallyRead);
			if (!state)
			{
				std::cout << "Memory Error! " << GetLastError() << std::endl;
				return "-1";
			}
		}
		stringToRead[i] = buffer;
		i++;
		address++;
	}
	return stringToRead;
}

string Memory::ReadPointerText(const uintptr_t address, int offsets[], int offsetCount) {
	if (address == static_cast<uintptr_t>(-1))
		return "-1";
	return ReadText(GetPointerAddress(address, offsets, offsetCount));
}