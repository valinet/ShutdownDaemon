#include <Windows.h>
#include <stdio.h>
#include <conio.h>
#include <Wininet.h>

#pragma comment(lib, "Wininet.lib")

#define APPLICATION_NAME TEXT("MonitorDaemon")
#define CONTROL_HOSTNAME TEXT("homepi.local")
#define CONTROL_URL TEXT("http://homepi.local")
#define FORM_HEADERS TEXT("Content-Type: application/x-www-form-urlencoded; charset=utf-8\r\n")

HINTERNET hInternet;

void postData(const char* data)
{
	if (hInternet = InternetOpen(
		APPLICATION_NAME,
		INTERNET_OPEN_TYPE_DIRECT,
		NULL,
		NULL,
		NULL
	))
	{
		if (HINTERNET hConnect = InternetConnect(
			hInternet,
			CONTROL_HOSTNAME,
			INTERNET_DEFAULT_HTTP_PORT,
			NULL,
			NULL,
			INTERNET_SERVICE_HTTP,
			NULL,
			NULL
		))
		{
			if (HINTERNET hRequest = HttpOpenRequest(
				hConnect,
				L"POST",
				L"/",
				NULL,
				CONTROL_URL,
				NULL,
				NULL,
				NULL
			))
			{
				TCHAR headers[] = FORM_HEADERS;
				if (HttpSendRequest(
					hRequest,
					headers,
					wcslen(headers),
					reinterpret_cast<LPVOID>(const_cast<char*>(data)),
					strlen(data) * sizeof(char)
				))
				{
					printf("request OK\n");
				}
				else
				{
					wprintf(
						L"HttpSendRequest failed with 0x%x.\n", 
						GetLastError()
					);
				}
				InternetCloseHandle(hRequest);
			}
			else
			{
				wprintf(
					L"HttpOpenRequest failed with 0x%x.\n", 
					GetLastError()
				);
			}
			InternetCloseHandle(hConnect);
		}
		else
		{
			wprintf(
				L"InternetConnect failed with 0x%x.\n", 
				GetLastError()
			);
		}
		InternetCloseHandle(hInternet);
	}
	else
	{
		wprintf(
			L"InternetOpen failed with 0x%x.\n", 
			GetLastError()
		);
	}
}

void monitorOn()
{
	postData("source=15&r2=0");
}

void monitorOff()
{
	postData("r2=1");
}

int WINAPI wWinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	PWSTR pCmdLine,
	int nCmdShow
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

	TCHAR buffer[20];
	int err = 1000;
	do
	{
		HANDLE pipe = CreateFile(
			L"\\\\.\\pipe\\ShutdownDaemonPipe",
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		);
		if (pipe == INVALID_HANDLE_VALUE) {
			printf("CreateNamedPipe: %d\n", GetLastError());
			_getch();
			err = 1;
		}
		else
		{
			err = 0;
		}

		ZeroMemory(buffer, 20);
		DWORD numBytesRead = 0;
		BOOL result = ReadFile(
			pipe,
			buffer,
			20 * sizeof(TCHAR),
			&numBytesRead,
			NULL
		);

		if (!result) {
			printf("ReadFile: %d\n", GetLastError());
			_getch();
			err = 2;
		}
		else
		{
			err = 0;
		}
		CloseHandle(pipe);

		if (err)
		{
			Sleep(100);
		}
	}
	while (err);

#ifdef _DEBUG
	wprintf(buffer);
	_getch();
#else
	if (!wcscmp(buffer, TEXT("poweroff")))
	{
		monitorOff();
	}
#endif

	return 0;
}