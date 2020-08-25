#include <Windows.h>
#include <iostream>
#include <conio.h>
#include <UIAutomation.h>

#define CLASS_NAME TEXT("LogonUI Logon Window")
#define STATUS_LOGONUI_ERROR 0
#define STATUS_LOGONUI_OTHER 1
#define STATUS_LOGONUI_LOGOFF 2
#define STATUS_LOGONUI_POWEROFF 3
#define STATUS_LOGONUI_RESTART 4
#define STATUS_LOGONUI_PLEASEWAIT 5
#define STATUS_LOGONUI_PREPARING_OTHER 6

#define DATA_LENGTH 20

IUIAutomation* g_pAutomation;

IUIAutomationElement* GetTopLevelWindowByClassName(const TCHAR* windowName)
{
	if (windowName == NULL)
	{
		return NULL;
	}

	VARIANT varProp;
	varProp.vt = VT_BSTR;
	varProp.bstrVal = SysAllocString(windowName);
	if (varProp.bstrVal == NULL)
	{
		return NULL;
	}

	IUIAutomationElement* pRoot = NULL;
	IUIAutomationElement* pFound = NULL;
	IUIAutomationCondition* pCondition = NULL;

	// Get the desktop element. 
	HRESULT hr = g_pAutomation->GetRootElement(&pRoot);
	if (FAILED(hr) || pRoot == NULL)
		goto cleanup;

	// Get a top-level element by class name, such as "Shell_TrayWnd"
	hr = g_pAutomation->CreatePropertyCondition(
		UIA_ClassNamePropertyId, 
		varProp, 
		&pCondition
	);
	if (FAILED(hr))
		goto cleanup;

	pRoot->FindFirst(TreeScope_Children, pCondition, &pFound);

cleanup:
	if (pRoot != NULL)
		pRoot->Release();

	if (pCondition != NULL)
		pCondition->Release();

	VariantClear(&varProp);
	return pFound;
}

// CAUTION: Do not pass in the root (desktop) element. Traversing the 
// entire subtree of the desktop could take a very long time and even
// lead to a stack overflow.
void ListDescendants(IUIAutomationElement* pParent, int indent)
{
	if (pParent == NULL)
		return;

	IUIAutomationTreeWalker* pControlWalker = NULL;
	IUIAutomationElement* pNode = NULL;

	g_pAutomation->get_ControlViewWalker(&pControlWalker);
	if (pControlWalker == NULL)
		goto cleanup;

	pControlWalker->GetFirstChildElement(pParent, &pNode);
	if (pNode == NULL)
		goto cleanup;

	while (pNode)
	{
		BSTR desc;
		BSTR desc2;
		pNode->get_CurrentName(&desc);
		pNode->get_CurrentLocalizedControlType(&desc2);
		for (int x = 0; x <= indent; x++)
		{
			std::wcout << L"   ";
		}
		std::wcout << desc << " - " << desc2 << L"\n";
		SysFreeString(desc);
		SysFreeString(desc2);

		ListDescendants(pNode, indent + 1);
		IUIAutomationElement* pNext;
		pControlWalker->GetNextSiblingElement(pNode, &pNext);
		pNode->Release();
		pNode = pNext;
	}

cleanup:
	if (pControlWalker != NULL)
		pControlWalker->Release();

	if (pNode != NULL)
		pNode->Release();

	return;
}

int CheckStatus(IUIAutomationElement* pParent)
{
	int ret = STATUS_LOGONUI_ERROR;
	if (pParent == NULL)
		return ret;

	IUIAutomationTreeWalker* pControlWalker = NULL;
	IUIAutomationElement* pNode = NULL;
	IUIAutomationElement* pNext = NULL;

	g_pAutomation->get_ControlViewWalker(&pControlWalker);
	if (pControlWalker == NULL)
		goto cleanup;

	pControlWalker->GetFirstChildElement(pParent, &pNode);
	if (pNode == NULL)
		goto cleanup;

	pControlWalker->GetNextSiblingElement(pNode, &pNext);
	if (pNext == NULL)
		goto cleanup;

	BSTR pNodeType, pNextType;
	pNode->get_CurrentLocalizedControlType(&pNodeType);
	pNext->get_CurrentLocalizedControlType(&pNextType);
	if (
		!wcscmp(pNodeType, TEXT("progress ring")) && 
		!wcscmp(pNextType, TEXT("pane"))
	)
	{
		//printf("ring and pane\n");
		BSTR pNodeText, pNextText;
		pNode->get_CurrentName(&pNodeText);
		pNext->get_CurrentName(&pNextText);
		if (!wcscmp(pNodeText, TEXT("Busy...")))
		{
			//printf("busy\n");
			if (!wcscmp(pNextText, TEXT("Signing out")))
			{
				ret = STATUS_LOGONUI_LOGOFF;
			}
			else if (!wcscmp(pNextText, TEXT("Shutting down")))
			{
				ret = STATUS_LOGONUI_POWEROFF;
			}
			else if (!wcscmp(pNextText, TEXT("Restarting")))
			{
				ret = STATUS_LOGONUI_RESTART;
			}
			else if (!wcscmp(pNextText, TEXT("Please wait")))
			{
				// this shows when booting into Advanced startup
				// from Settings - Recovery
				ret = STATUS_LOGONUI_PLEASEWAIT;
			}
			else
			{
				ret = STATUS_LOGONUI_PREPARING_OTHER;
			}
		}
		else
		{
			ret = STATUS_LOGONUI_OTHER;
		}
		SysFreeString(pNodeText);
		SysFreeString(pNextText);
	}
	else
	{
		ret = STATUS_LOGONUI_OTHER;
	}
	SysFreeString(pNodeType);
	SysFreeString(pNextType);
	pNode->Release();
	pNext->Release();


cleanup:
	if (pControlWalker != NULL)
		pControlWalker->Release();

	if (pNode != NULL)
		pNode->Release();

	return ret;
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
	
	g_pAutomation = NULL;

	CoInitialize(NULL);
	HRESULT hr;
	hr = CoCreateInstance(
		__uuidof(CUIAutomation), 
		NULL, 
		CLSCTX_INPROC_SERVER,
		__uuidof(IUIAutomation), 
		(void**)&g_pAutomation
	);
	if (FAILED(hr))
	{
		printf("CoCreateInstance: %d\n", GetLastError());
#ifdef _DEBUG
		_getch();
#endif
		return 1;
	}

	HANDLE pipe = CreateNamedPipe(
		L"\\\\.\\pipe\\ShutdownDaemonPipe",
		PIPE_ACCESS_OUTBOUND,
		PIPE_TYPE_BYTE,
		1,
		0,
		0,
		0,
		NULL
	);
	if (pipe == INVALID_HANDLE_VALUE) {
		printf("CreateNamedPipe: %d\n", GetLastError());
#ifdef _DEBUG
		_getch();
#endif
		return 1;
	}

	BOOL result = ConnectNamedPipe(
		pipe, 
		NULL
	);
	if (!result) {
		printf("ConnectNamedPipe: %d\n", GetLastError());
#ifdef _DEBUG
		_getch();
#endif
		return 2;
	}

	TCHAR data[DATA_LENGTH];
	ZeroMemory(data, DATA_LENGTH);
	int st = STATUS_LOGONUI_ERROR;
	IUIAutomationElement* logonUi = GetTopLevelWindowByClassName(CLASS_NAME);
	st = CheckStatus(logonUi);
	switch (st)
	{
	case STATUS_LOGONUI_ERROR:
		lstrcat(data, TEXT("error"));
		break;
	case STATUS_LOGONUI_PREPARING_OTHER:
		lstrcat(data, TEXT("preparing"));
		break;
	case STATUS_LOGONUI_LOGOFF:
		lstrcat(data, TEXT("logoff"));
		break;
	case STATUS_LOGONUI_POWEROFF:
		lstrcat(data, TEXT("poweroff"));
		break;
	case STATUS_LOGONUI_RESTART:
		lstrcat(data, TEXT("restart"));
		break;
	case STATUS_LOGONUI_PLEASEWAIT:
		lstrcat(data, TEXT("plasewait"));
		break;
	case STATUS_LOGONUI_OTHER:
	default:
		lstrcat(data, TEXT("other"));
		break;
	}

	DWORD numBytesWritten = 0;
	result = WriteFile(
		pipe,
		data,
		(wcslen(data) + 1) * sizeof(TCHAR),
		&numBytesWritten,
		NULL
	);
	if (!result)
	{
		printf("WriteFile: %d\n", GetLastError());
#ifdef _DEBUG
		_getch();
#endif
		return 3;
	}

	CloseHandle(pipe);
	return 0;
}