//-------------------------------------------------------------------------------Nvzdev0-------------------------------------------------------------------------------

#include <iostream>
#include <Windows.h>   

#include <string>
#include <vector>
#include <TlHelp32.h>
#include <thread>
#include <ctime>
#include <sstream>

#include <thread>           // std::thread
#include <chrono>           // std::chrono::seconds
#include <atomic>           // std::atomic
#include <condition_variable> // std::condition_variable
#include <mutex>            // std::mutex, std::unique_lock

using namespace std;

constexpr uint32_t JobObjectFreezeInformation = 18;

typedef struct _JOBOBJECT_WAKE_FILTER {
	ULONG HighEdgeFilter;
	ULONG LowEdgeFilter;
} JOBOBJECT_WAKE_FILTER, * PJOBOBJECT_WAKE_FILTER;

typedef struct _JOBOBJECT_FREEZE_INFORMATION {
	union {
		ULONG Flags;
		struct {
			ULONG FreezeOperation : 1;
			ULONG FilterOperation : 1;
			ULONG SwapOperation : 1;
			ULONG Reserved : 29;
		};
	};
	BOOLEAN Freeze;
	BOOLEAN Swap;
	UCHAR Reserved0[2];
	JOBOBJECT_WAKE_FILTER WakeFilter;
} JOBOBJECT_FREEZE_INFORMATION, * PJOBOBJECT_FREEZE_INFORMATION;

HANDLE globalJobHandle = NULL;

// VANGUARD SENDS ITS HEARTBEATS AFTER UR SYSTEM TIME
void SetSystemTimePlusTwo() {
	SYSTEMTIME st;
	GetLocalTime(&st);

	st.wHour += 2;

	// Checking for day end so we dont go -23:59 hours this would fuck up vgc
	if (st.wHour >= 24) {
		st.wHour -= 24;
		st.wDay += 1;
	}

	if (SetLocalTime(&st)) {
		std::wcout << L"Systemzeit erfolgreich um 2h vorgestellt." << std::endl;
	}
	else {
		std::wcout << L"Fehler: Administratorrechte erforderlich!" << std::endl;
	}
}

// reset time in lobby so we have -2 hours till next heartbeat
void SyncTime() {
	std::wcout << L"Bereite Zeit-Synchronisierung vor..." << std::endl;


	system("net start w32time");

	std::wcout << L"gzwhousefhaijifjnioeif" << std::endl;
	int result = system("w32tm /resync");

	if (result == 0) {
		std::wcout << L"zgwuhefjieijfonffe" << std::endl;
	}
	else {
		std::wcerr << L"Fehler bei der Synchronisierung. (Admin-Rechte vorhanden?)" << std::endl;
	}
}

void SetNativeLimit(bool enable) {
	if (enable) {
		std::cout << "[+] Activating Windows QoS Limiting (24 Bps)..." << std::endl;

		std::string cmd = "powershell -Command \"New-NetQosPolicy -Name 'VanguardLimit' -AppPathNameMatchCondition 'vgc.exe' -ThrottleRateActionBitsPerSecond 192 -Confirm:$false\"";
		system(cmd.c_str());
	}
	else {
		std::cout << "[-] Removing QoS Limiting..." << std::endl;
		system("powershell -Command \"Remove-NetQosPolicy -Name 'VanguardLimit' -Confirm:$false\"");
	}
}

DWORD GetServicePID(const wchar_t* serviceName) {
	DWORD pid = 0;
	SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	if (hSCManager) {
		SC_HANDLE hService = OpenService(hSCManager, serviceName, SERVICE_QUERY_STATUS);
		if (hService) {
			SERVICE_STATUS_PROCESS ssp;
			DWORD bytesNeeded;
			if (QueryServiceStatusEx(hService, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesNeeded)) {
				pid = ssp.dwProcessId;
			}
			CloseServiceHandle(hService);
		}
		CloseServiceHandle(hSCManager);
	}
	return pid;
}

bool FreezeProcess(HANDLE hProcess) {
	if (!hProcess) return false;
	globalJobHandle = CreateJobObject(NULL, NULL);
	if (!globalJobHandle) return false;
	if (!AssignProcessToJobObject(globalJobHandle, hProcess)) return false;

	JOBOBJECT_FREEZE_INFORMATION freezeInfo = { 0 };
	freezeInfo.FreezeOperation = 1;
	freezeInfo.Freeze = TRUE;
	return SetInformationJobObject(globalJobHandle, (JOBOBJECTINFOCLASS)JobObjectFreezeInformation, &freezeInfo, sizeof(freezeInfo));
}

void ThawProcess() {
	if (globalJobHandle) {
		JOBOBJECT_FREEZE_INFORMATION freezeInfo = { 0 };
		freezeInfo.FreezeOperation = 1;
		freezeInfo.Freeze = FALSE;
		SetInformationJobObject(globalJobHandle, (JOBOBJECTINFOCLASS)JobObjectFreezeInformation, &freezeInfo, sizeof(freezeInfo));
		CloseHandle(globalJobHandle);
		globalJobHandle = NULL;
	}
}

// Modified handle_client function with termination if a vgc process is detected.
void handle_client(HANDLE pipe) {
	// Immediately check if the connecting client is the original Vanguard client.
	DWORD clientPid = 0;
	if (GetNamedPipeClientProcessId(pipe, &clientPid)) {
		HANDLE hClient = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, clientPid);
		if (hClient) {
			wchar_t processName[MAX_PATH] = { 0 };
			DWORD size = MAX_PATH;
			if (QueryFullProcessImageNameW(hClient, 0, processName, &size)) {
				std::wstring procNameStr(processName);
				// If the process image name contains "vgc", terminate the service.
				if (procNameStr.find(L"vgc") != std::wstring::npos) {
					system("sc stop vgc");
					CloseHandle(hClient);
					CloseHandle(pipe);
					return;  // Exit early if vgc was detected.
				}
			}
			CloseHandle(hClient);
		}
	}

	// Continue normal communication with the client.
	DWORD bytesRead;
	char buffer[4096];
	try {
		while (true) {
			BOOL result = ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr);
			if (!result) break; // Likely a disconnection.
			if (bytesRead > 0) {
				std::wstring data;
				int len = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, nullptr, 0);
				if (len > 0) {
					data.resize(len);
					MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, &data[0], len);
				}
				std::wcout << L"\n  [-] Received data: " << data << std::endl;
				WriteFile(pipe, buffer, bytesRead, &bytesRead, nullptr);
				std::wcout << L"\n  [!] Sent data back: " << data << std::endl;
			}
			else {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
	}
	catch (const std::exception& e) {

	}
	CloseHandle(pipe);

}

void Popup() {
	MessageBoxA(
		nullptr,
		"Please open the Riot Client now then press ok.",
		"Action Required",
		MB_OK | MB_ICONINFORMATION
	);

	SetSystemTimePlusTwo();

	SetNativeLimit(true);

	system("sc start vgc");

	uint32_t val_pid = 0;

	MessageBoxA(
		nullptr,
		"Please open Valorant now and wait 60 Seconds.",
		"Waiting for Valorant",
		MB_OK | MB_ICONINFORMATION
	);

	// Wait for Valorant process
	while (val_pid == 0) {
		PROCESSENTRY32W entry = { sizeof(PROCESSENTRY32W) };
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (Process32FirstW(snapshot, &entry)) {
			do {
				if (wcscmp(entry.szExeFile, L"VALORANT-Win64-Shipping.exe") == 0) {
					val_pid = entry.th32ProcessID;
					break;
				}
			} while (Process32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
		Sleep(1000);
	}

	Sleep(40000);

	SyncTime();

	DWORD dns_pid = GetServicePID(L"Dnscache");
	if (dns_pid != 0) {
		HANDLE hDns = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dns_pid);
		if (FreezeProcess(hDns)) {

			MessageBoxA(
				nullptr,
				"Preparing bypass, Press ok once in lobby",
				"Status",
				MB_OK | MB_ICONINFORMATION
			);

			Sleep(5000);

			SetNativeLimit(false);

			MessageBoxA(
				nullptr,
				"You are ready to queue now.",
				"Success",
				MB_OK | MB_ICONINFORMATION
			);

		}
	}
	else {
		MessageBoxA(
			nullptr,
			"Popup bypass Failed, Try again",
			"Error",
			MB_OK | MB_ICONERROR
		);

		SetNativeLimit(false);
	}
}

void Emulation() {

	// Terminate VALORANT process(es)
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32W procEntry;
	procEntry.dwSize = sizeof(PROCESSENTRY32W);
	if (Process32FirstW(hSnap, &procEntry)) {
		do {
			if (_wcsicmp(procEntry.szExeFile, L"VALORANT-Win64-Shipping.exe") == 0) {
				HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, procEntry.th32ProcessID);
				if (hProc != NULL) {
					TerminateProcess(hProc, 0);
					CloseHandle(hProc);
				}
			}
		} while (Process32NextW(hSnap, &procEntry));
	}
	CloseHandle(hSnap);

	DWORD dns_pid = GetServicePID(L"Dnscache");
	HANDLE hDns = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dns_pid);
	ThawProcess();
	if (hDns) CloseHandle(hDns);

	Sleep(200);
	system("sc stop vgc");
	Sleep(200);
	system("cls");
	Sleep(500);
	system("taskkill /F /IM vgc.exe >nul 2>&1");
	Beep(500, 400);

	const wchar_t* oldPath = L"C:\\Program Files\\Riot Vanguard\\vgc.exe";
	const wchar_t* newPath = L"C:\\Program Files\\Riot Vanguard\\vgc.exe.old";

	if (MoveFileW(oldPath, newPath)) {



	}

	else {

		MessageBoxA(
			nullptr,
			"Failed to add emulator, run again or use match end.",
			"Emulation Failed",
			MB_OK | MB_ICONERROR
		);
		return;

	}

	/*const wchar_t* targetPath =                          --------------> This part is for adding the fake exe in the vgc's place ( add the fake vgc as bytes )
		L"C:\\Program Files\\Riot Vanguard\\vgc.exe";

	HANDLE hFile = CreateFileW(
		targetPath,
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
	{

		MessageBoxA(
			nullptr,
			"Failed to add emulator, run again or use match end.",
			"Emulation Failed",
			MB_OK | MB_ICONERROR
		);
		return;
	}

	DWORD written = 0;
	BOOL ok = WriteFile(
		hFile,
		embeddedExe,
		embeddedExeSize,
		&written,
		nullptr
	);

	CloseHandle(hFile);

	if (!ok || written != embeddedExeSize)
	{

		MessageBoxA(
			nullptr,
			"Failed to add emulator, run again or use match end.",
			"Emulation Failed",
			MB_OK | MB_ICONERROR
		);
		return;
	}*/

	Sleep(5000);

	system("taskkill /F /IM vgc.exe >nul 2>&1");

	system("sc start vgc");

	MessageBoxA(
		nullptr,
		"Vanguard Emulated.",
		"Success",
		MB_OK | MB_ICONINFORMATION
	);

}

void MatchEnd() {

	const wchar_t* originalPath = L"C:\\Program Files\\Riot Vanguard\\vgc.exe";
	const wchar_t* backupPath = L"C:\\Program Files\\Riot Vanguard\\vgc.exe.old";

	// Terminate VALORANT process(es)
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32W procEntry;
	procEntry.dwSize = sizeof(PROCESSENTRY32W);
	if (Process32FirstW(hSnap, &procEntry)) {
		do {
			if (_wcsicmp(procEntry.szExeFile, L"VALORANT-Win64-Shipping.exe") == 0) {
				HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, procEntry.th32ProcessID);
				if (hProc != NULL) {
					TerminateProcess(hProc, 0);
					CloseHandle(hProc);
				}
			}
		} while (Process32NextW(hSnap, &procEntry));
	}
	CloseHandle(hSnap);

	DWORD dns_pid = GetServicePID(L"Dnscache");
	HANDLE hDns = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dns_pid);
	ThawProcess();
	if (hDns) CloseHandle(hDns);

	// Stop Vanguard service
	system("sc stop vgc");
	Sleep(3000);

	system("taskkill /F /IM vgc.exe >nul 2>&1");

	// Delete current (emulated) vgc.exe
	if (!DeleteFileW(originalPath))
	{
		MessageBoxA(
			nullptr,
			"Failed to remove emulator, run again.",
			"Undo Failed",
			MB_OK | MB_ICONERROR
		);
		return;
	}

	// Restore original vgc.exe
	if (!MoveFileW(backupPath, originalPath))
	{

		MessageBoxA(
			nullptr,
			"Failed to remove emulator, run again.",
			"Undo Failed",
			MB_OK | MB_ICONERROR
		);
		return;
	}

	Sleep(5000);

	system("taskkill /F /IM vgc.exe >nul 2>&1");

	// Restart Vanguard service
	system("sc start vgc");

	MessageBoxA(
		nullptr,
		"Vanguard is resetted successfully.",
		"Match End Compeleted",
		MB_OK | MB_ICONINFORMATION
	);

}

int main() {
    int choice;

    while (true) {
        cout << "=== Nvzdev0 Vanguard Bypass ===" << endl;
        cout << "1. Popup Bypass" << endl;
        cout << "2. Emulate Vanguard ( In Game Players Screen )" << endl;
        cout << "3. Exit Emulation ( After Your Game Match Ends )" << endl;
        cout << "4. Exit" << endl;
        cout << "Enter your choice: ";
        cin >> choice;

        switch (choice) {
        case 1:
            Popup();
            cout << "Done.." << endl;
            break;

        case 2:
            Emulation();
            cout << "Done.." << endl;
            break;

        case 3:
            MatchEnd();
            cout << "Done.." << endl;
            break;

        case 4:
            cout << "Exiting..." << endl;
            return 0;

        default:
            cout << "Invalid Choice!" << endl;
            break;
        }
    }

    return 0;
}

//-------------------------------------------------------------------------------Nvzdev0-------------------------------------------------------------------------------
