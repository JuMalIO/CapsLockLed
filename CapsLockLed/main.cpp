#include <windows.h>
#include <setupapi.h>
#include <dinput.h>
#include <Dbt.h>
#include <stdexcept>

#define HID_CLASSGUID {0x4d1e55b2, 0xf16f, 0x11cf, {0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}}
#define WND_NAME "CAPS_LOCK_LED"
#define HWND_MESSAGE     ((HWND)-3)

#pragma comment(linker, "/subsystem:windows")

#pragma comment(lib, "Setupapi.lib")

GUID GUID_DEVINTERFACE_KEYBOARD = { 0x884b96c3, 0x56ef, 0x11d1, 0xbc, 0x8c, 0x00, 0xa0, 0xc9, 0x14, 0x05, 0xdd };

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
	USHORT  UnitId;
	USHORT  LedFlags;
} KEYBOARD_INDICATOR_PARAMETERS, *PKEYBOARD_INDICATOR_PARAMETERS;

void KeybdLight(DWORD wFlags)
{
	DWORD devindex, needed, retbytes;
	PSP_INTERFACE_DEVICE_DETAIL_DATA detail;
	SP_INTERFACE_DEVICE_DATA ifdata = { sizeof(SP_INTERFACE_DEVICE_DATA) };
	SP_DEVINFO_DATA devdata = { sizeof(SP_DEVINFO_DATA) };
	KEYBOARD_INDICATOR_PARAMETERS buffer;

	HDEVINFO info = SetupDiGetClassDevs(&GUID_DEVINTERFACE_KEYBOARD, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (info == INVALID_HANDLE_VALUE)
	{
		throw std::runtime_error("SetupDiGetClassDevs failed!");
		return;
	}

	for (devindex = 0; SetupDiEnumDeviceInterfaces(info, NULL, &GUID_DEVINTERFACE_KEYBOARD, devindex, &ifdata); ++devindex)
	{
		SetupDiGetDeviceInterfaceDetail(info, &ifdata, NULL, 0, &needed, NULL);

		detail = (PSP_INTERFACE_DEVICE_DETAIL_DATA)malloc(needed);
		detail->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);

		SetupDiGetDeviceInterfaceDetail(info, &ifdata, detail, needed, NULL, &devdata);
		
		HANDLE hKeyb = CreateFile(detail->DevicePath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		free((void*)detail);
		if (hKeyb == INVALID_HANDLE_VALUE)
		{
			continue;
		}

		DeviceIoControl(hKeyb, IOCTL_KEYBOARD_QUERY_INDICATORS, 0, 0, &buffer, sizeof(buffer), &retbytes, 0);

		if (wFlags & KBD_ON)
			buffer.LedFlags |= wFlags & 15;
		else if (wFlags & KBD_OFF)
			buffer.LedFlags = ~(WORD)wFlags & 15;
		else
			buffer.LedFlags ^= wFlags & 15;

		DeviceIoControl(hKeyb, IOCTL_KEYBOARD_SET_INDICATORS, &buffer, sizeof(buffer), 0, 0, &retbytes, 0);

		CloseHandle(hKeyb);
	}

	if (!devindex)
	{
		throw std::runtime_error("SetupDiEnumDeviceInterfaces failed!");
	}

	SetupDiDestroyDeviceInfoList(info);
}

bool IsOnBatteryPower()
{
	SYSTEM_POWER_STATUS lpSysPwrStatus;
	if (GetSystemPowerStatus(&lpSysPwrStatus))
	{
		if (lpSysPwrStatus.ACLineStatus == 0)
		{
			return true;
		}
	}
	return false;
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

bool batteryPowerEnabled = false;
bool flashEnabled = false;
DWORD flashSleepMilliseconds = 1000;

void Flash()
{
	for (;;)
	{
		Sleep(flashSleepMilliseconds);
		KeybdLight(KBD_CAPS);
	}
}

LRESULT WndProc(HWND__* hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg)
	{
		case WM_NCCREATE:
		{
			return true;
		}
		case WM_CREATE:
		{
			LPCREATESTRUCT params = (LPCREATESTRUCT)lParam;
			GUID InterfaceClassGuid = *((GUID*)params->lpCreateParams);
			DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
			ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
			NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
			NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
			NotificationFilter.dbcc_classguid = InterfaceClassGuid;
			HDEVNOTIFY dev_notify = RegisterDeviceNotification(hwnd, &NotificationFilter,
				DEVICE_NOTIFY_WINDOW_HANDLE);
			if (dev_notify == NULL)
			{
				throw std::runtime_error("Could not register for device notifications!");
			}

			HPOWERNOTIFY power_notify = RegisterPowerSettingNotification(hwnd, &GUID_ACDC_POWER_SOURCE,
				DEVICE_NOTIFY_WINDOW_HANDLE);
			if (power_notify == NULL)
			{
				throw std::runtime_error("Could not register for power notifications!");
			}
			break;
		}
		case WM_DEVICECHANGE:
		{
			PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;
			if (lpdb != NULL && lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE)
			{
				if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)
				{
					KeybdLight(KBD_CAPS | KBD_ON);
				}
			}
			break;
		}
		case WM_CLOSE:
		{
			KeybdLight(KBD_CAPS | KBD_OFF);
			break;
		}
		case WM_POWERBROADCAST:
		{
			if (wParam == PBT_APMPOWERSTATUSCHANGE)
			{
				if (!batteryPowerEnabled && IsOnBatteryPower())
				{
					KeybdLight(KBD_CAPS | KBD_OFF);
					PostQuitMessage(0);
				}
			}
			else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
			{
				KeybdLight(KBD_CAPS | KBD_ON);
			}
			break;
		}
	}
	return 0;
}

void ReadArguments()
{
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

				flashEnabled = true;
			}
			else if (wcscmp(param, L"-b") == 0 || wcscmp(param, L"--battery") == 0)
			{
				batteryPowerEnabled = true;
			}
		}
	}

	LocalFree(szArgList);
}

int WINAPI WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int nShowCmd)
{
	ReadArguments();

	if (!batteryPowerEnabled && IsOnBatteryPower())
	{
		return 0;
	}

	KeybdLight(KBD_CAPS | KBD_ON);

	if (flashEnabled)
	{
		CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Flash, 0, 0, 0);
	}

	SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(0), 0);

	WNDCLASSEX wx;
	ZeroMemory(&wx, sizeof(wx));
	wx.cbSize = sizeof(WNDCLASSEX);
	wx.lpfnWndProc = reinterpret_cast<WNDPROC>(WndProc);
	wx.style = CS_HREDRAW | CS_VREDRAW;
	wx.hInstance = GetModuleHandle(0);
	wx.hbrBackground = (HBRUSH)(COLOR_WINDOW);
	wx.lpszClassName = WND_NAME;

	HWND hWnd = NULL;

	if (RegisterClassEx(&wx))
	{
		GUID guid = HID_CLASSGUID;
		hWnd = CreateWindow(WND_NAME, WND_NAME, WS_ICONIC, 0, 0, 0, 0,
			NULL, NULL, GetModuleHandle(0), (void*)&guid);
	}

	if (hWnd == NULL)
	{
		throw std::runtime_error("Could not create message window!");
	}

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return 0;
}
