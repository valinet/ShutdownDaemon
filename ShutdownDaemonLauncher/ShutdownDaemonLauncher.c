#include <Windows.h>
#include <stdio.h>
#include <assert.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include <conio.h>

#pragma comment(lib, "Shlwapi.lib")

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD);
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam);

#define SERVICE_NAME TEXT("Shutdown Daemon Service")  

DWORD WINAPI ServiceWorkerThread(
	LPVOID lpParam
)
{
#ifdef _DEBUG
	FILE* conout;
	AllocConsole();
	freopen_s(
		&conout,
		"CONOUT$",
		"w",
		stdout
	);
#endif

	HANDLE jobHandle = CreateJobObject(
		NULL,
		NULL
	);
	JOBOBJECT_BASIC_LIMIT_INFORMATION jobInfoBasic;
	jobInfoBasic.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo;
	jobInfo.BasicLimitInformation = jobInfoBasic;
	SetInformationJobObject(
		jobHandle,
		JobObjectExtendedLimitInformation, &jobInfo,
		sizeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)
	);

	while (TRUE)
	{
		HANDLE winLogon = NULL;
		PROCESSENTRY32 entry;
		entry.dwSize = sizeof(PROCESSENTRY32);
		HANDLE snapshot = CreateToolhelp32Snapshot(
			TH32CS_SNAPPROCESS,
			NULL
		);
		if (Process32First(snapshot, &entry) == TRUE)
		{
			while (Process32Next(snapshot, &entry) == TRUE)
			{
				if (!wcscmp(entry.szExeFile, L"winlogon.exe"))
				{
					if (!(winLogon = OpenProcess(
						PROCESS_ALL_ACCESS,
						FALSE,
						entry.th32ProcessID
					)))
					{
						printf("OpenProcess: %d\n", GetLastError());
#ifdef _DEBUG
						_getch();
#endif
						return 1;
					}
					break;
				}
			}
		}
		CloseHandle(snapshot);

		HANDLE userToken;
		if (!OpenProcessToken
		(
			winLogon,
			TOKEN_QUERY | TOKEN_IMPERSONATE | TOKEN_DUPLICATE,
			&userToken)
			)
		{
			printf("OpenProcessToken: %d\n", GetLastError());
#ifdef _DEBUG
			_getch();
#endif
			return 1;
		}

		HANDLE newToken = 0;
		SECURITY_ATTRIBUTES tokenAttributes = { 0 };
		tokenAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
		SECURITY_ATTRIBUTES threadAttributes = { 0 };
		threadAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);

		if (!DuplicateTokenEx
		(
			userToken,
			0x10000000,
			&tokenAttributes,
			SecurityImpersonation,
			TokenImpersonation,
			&newToken
		))
		{
			printf("DuplicateTokenEx: %d\n", GetLastError());
#ifdef _DEBUG
			_getch();
#endif
			return 1;
		}

		TOKEN_PRIVILEGES tokPrivs = { 0 };
		tokPrivs.PrivilegeCount = 1;
		LUID seDebugNameValue = { 0 };
		if (!LookupPrivilegeValue
		(
			NULL,
			SE_DEBUG_NAME,
			&seDebugNameValue
		))
		{
			printf("LookupPrivilegeValue: %d\n", GetLastError());
#ifdef _DEBUG
			_getch();
#endif
			return 1;
		}

		tokPrivs.Privileges[0].Luid = seDebugNameValue;
		tokPrivs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		if (!AdjustTokenPrivileges(
			newToken,
			FALSE,
			&tokPrivs,
			0,
			NULL,
			NULL
		)) {
			printf("AdjustTokenPrivileges: %d\n", GetLastError());
#ifdef _DEBUG
			_getch();
#endif
			return 1;
		}

		wchar_t szFileName[_MAX_PATH];
		GetModuleFileName(
			GetModuleHandle(NULL),
			szFileName,
			_MAX_PATH
		);
		PathRemoveFileSpec(szFileName);
		lstrcat(
			szFileName,
			L"\\ShutdownDaemon.exe"
		);

		while (TRUE)
		{
			PROCESS_INFORMATION pi = { 0 };
			STARTUPINFO si = { 0 };
			si.cb = sizeof(STARTUPINFO);
			si.lpDesktop = TEXT("WinSta0\\Winlogon");
			// start the process using the new token
			if (!CreateProcessAsUser(
				newToken,
				szFileName,
				NULL,
				&tokenAttributes,
				&threadAttributes,
				TRUE,
				CREATE_NEW_CONSOLE | INHERIT_CALLER_PRIORITY,
				NULL,
				NULL,
				&si,
				&pi
			)) {
				printf("CreateProcessAsUser: %d\n", GetLastError());
#ifdef _DEBUG
				_getch();
#endif
				Sleep(1000);
				break;
			}
			AssignProcessToJobObject(
				jobHandle,
				pi.hProcess
			);

			HANDLE handles[2];
			handles[0] = g_ServiceStopEvent;
			handles[1] = pi.hProcess;
			DWORD dwEvent = WaitForMultipleObjects(
				2,
				&handles,
				FALSE,
				INFINITE
			);
			if (dwEvent == WAIT_OBJECT_0 + 0)
			{
				return NULL;
			}
		}
	}

	CloseHandle(jobHandle);

	return NULL;
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
	switch (CtrlCode)
	{
	case SERVICE_CONTROL_STOP:

		if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
			break;

		/*
		 * Perform tasks necessary to stop the service here
		 */

		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
		g_ServiceStatus.dwWin32ExitCode = 0;
		g_ServiceStatus.dwCheckPoint = 4;

		if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			OutputDebugString(TEXT(
				"My Sample Service: ServiceCtrlHandler: SetServiceStatus returned error"));
		}

		// This will signal the worker thread to start shutting down
		SetEvent(g_ServiceStopEvent);

		break;

	default:
		break;
	}
}

VOID WINAPI ServiceMain(
	DWORD argc, 
	LPTSTR* argv
)
{
	DWORD Status = E_FAIL;

	// Register our service control handler with the SCM
	g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);

	if (g_StatusHandle == NULL)
	{
		goto EXIT;
	}

	// Tell the service controller we are starting
	ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwServiceSpecificExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(TEXT(
			"Shutdown Daemon Service: ServiceMain: SetServiceStatus returned error"));
	}

	/*
	 * Perform tasks necessary to start the service here
	 */

	 // Create a service stop event to wait on later
	g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (g_ServiceStopEvent == NULL)
	{
		// Error creating event
		// Tell service controller we are stopped and exit
		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		g_ServiceStatus.dwWin32ExitCode = GetLastError();
		g_ServiceStatus.dwCheckPoint = 1;

		if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			OutputDebugString(TEXT(
				"Shutdown Daemon Service: ServiceMain: SetServiceStatus returned error"));
		}
		goto EXIT;
	}

	// Tell the service controller we are started
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(TEXT(
			"Shutdown Daemon Service: ServiceMain: SetServiceStatus returned error"));
	}

	// Start a thread that will perform the main task of the service
	HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

	// Wait until our worker thread exits signaling that the service needs to stop
	WaitForSingleObject(hThread, INFINITE);


	/*
	 * Perform any cleanup tasks
	 */

	CloseHandle(g_ServiceStopEvent);

	// Tell the service controller we are stopped
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 3;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(TEXT(
			"Shutdown Daemon Service: ServiceMain: SetServiceStatus returned error"));
	}

EXIT:
	return;
}

int WINAPI wWinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	PWSTR pCmdLine,
	int nCmdShow
)
{
	SERVICE_TABLE_ENTRY ServiceTable[] =
	{
		{SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
		{NULL, NULL}
	};

	if (StartServiceCtrlDispatcher(ServiceTable) == FALSE)
	{
		return GetLastError();
	}

	return 0;
}