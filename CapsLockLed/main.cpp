#include <windows.h>
#include <winioctl.h>
#include <dinput.h>
#include <ntsecapi.h>
#include <string>

#pragma comment(linker, "/subsystem:windows")

#define IOCTL_KEYBOARD_SET_INDICATORS CTL_CODE(FILE_DEVICE_KEYBOARD, 0x0002, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_KEYBOARD_QUERY_TYPEMATIC CTL_CODE(FILE_DEVICE_KEYBOARD, 0x0008, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_KEYBOARD_QUERY_INDICATORS CTL_CODE(FILE_DEVICE_KEYBOARD, 0x0010, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define KBD_CAPS 4
#define KBD_NUM 2
#define KBD_SCROLL 1

#define KBD_ON 0x80
#define KBD_OFF 0x40

typedef struct _KEYBOARD_INDICATOR_PARAMETERS
{
	WORD wId;
	WORD wFlags;
} KEYBOARD_INDICATOR_PARAMETERS, *PKEYBOARD_INDICATOR_PARAMETERS;

void KeybdLight(DWORD wFlags)
{
	HANDLE hKeybd;
	KEYBOARD_INDICATOR_PARAMETERS buffer;
	DWORD retlen;

	DefineDosDevice(DDD_RAW_TARGET_PATH, "Keybd", "\\Device\\KeyboardClass0");
	hKeybd = CreateFile("\\\\.\\Keybd", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);

	DeviceIoControl(hKeybd, IOCTL_KEYBOARD_QUERY_INDICATORS, 0, 0, &buffer, sizeof(buffer), &retlen, 0);

	if (wFlags & KBD_ON)
		buffer.wFlags |= wFlags & 15;
	else if (wFlags & KBD_OFF)
		buffer.wFlags = ~(WORD)wFlags & 15;
	else
		buffer.wFlags ^= wFlags & 15;

	DeviceIoControl(hKeybd, IOCTL_KEYBOARD_SET_INDICATORS, &buffer, sizeof(buffer), 0, 0, &retlen, 0);

	DefineDosDevice(DDD_REMOVE_DEFINITION, "Keybd", 0);
	CloseHandle(hKeybd);
}

const DWORD DOUBLE_TAP_MILLISECONDS = 400;

DWORD last_time = 0;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	KBDLLHOOKSTRUCT * kbdllhookstruct = (KBDLLHOOKSTRUCT *)lParam;

	if (kbdllhookstruct->scanCode == DIK_CAPSLOCK)
	{
		if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
		{
			if (kbdllhookstruct->time - last_time < DOUBLE_TAP_MILLISECONDS)
			{
				KeybdLight(KBD_CAPS | KBD_OFF);
				PostQuitMessage(0);
				return 0;
			}

			last_time = kbdllhookstruct->time;
		}

		KeybdLight(KBD_CAPS | KBD_ON);
	}
	return 0;
}

DWORD flashSleepMilliseconds = 1000;

void Flash()
{
	for (;;)
	{
		Sleep(flashSleepMilliseconds);
		KeybdLight(KBD_CAPS);
	}
}

int WINAPI WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nShowCmd)
{
	KeybdLight(KBD_CAPS | KBD_ON);

	LPWSTR *szArgList;
	int argCount;

	szArgList = CommandLineToArgvW(GetCommandLineW(), &argCount);

	if (szArgList != NULL)
	{
		for (int i = 0; i < argCount; i++)
		{
			wchar_t* param = szArgList[i];
			if (wcscmp(param, L"-f") == 0 || wcscmp(param, L"--flash") == 0)
			{
				if (i + 1 < argCount)
				{
					int milliseconds = _wtoi(szArgList[i + 1]);
					if (milliseconds > 0)
					{
						flashSleepMilliseconds = milliseconds;
					}
				}

				CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Flash, 0, 0, 0);
			}
		}
	}

	LocalFree(szArgList);

	SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(0), 0);

	MSG msg;
	while (GetMessage(&msg, 0, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}
