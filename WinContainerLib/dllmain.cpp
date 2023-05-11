// Compile as multithreaded /MT for statically linked runtime, since injected programs may not have them

#include <Windows.h>

#define EXTERN_DLL_EXPORT extern "C" __declspec(dllexport)

HWND hWindowChild = (HWND)0x0;

LRESULT CALLBACK WindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		RECT rect;
		GetClientRect(hWnd, &rect);
		HBRUSH hBrush = CreateSolidBrush(RGB(200, 200, 230));
		FillRect(hdc, &rect, hBrush);
		DeleteObject(hBrush);

		EndPaint(hWnd, &ps);
	}
	break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		ExitProcess(0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcA(hWnd, Msg, wParam, lParam);
	}

	return 0;
}

EXTERN_DLL_EXPORT void threadMain(HWND hwndChild)
{
	hWindowChild = hwndChild;

	WNDCLASSA wndClass = { 0 };
	wndClass.lpszClassName = "_UIContainer";
	wndClass.lpfnWndProc = (WNDPROC)WindowProc;

	RegisterClassA(&wndClass);

	HINSTANCE hInstance = (HINSTANCE)GetModuleHandleW(NULL);

	int wndClientWidth = 800;
	int wndClientHeight = 600;

	int hwndStyle = WS_OVERLAPPEDWINDOW;

	RECT rt = { 0, 0, wndClientWidth, wndClientHeight };
	AdjustWindowRect(&rt, hwndStyle, FALSE);

	HWND hWindow = CreateWindowExA(
		0L,
		wndClass.lpszClassName,
		"Hello",
		hwndStyle,
		0, 0,
		rt.right - rt.left, rt.bottom - rt.top,
		NULL, NULL,
		hInstance,
		NULL);

	ShowWindow(hWindow, SW_SHOW);
	UpdateWindow(hWindow);

	int style = (int)GetWindowLongPtrA(hWindowChild, GWL_STYLE);
	int exStyle = (int)GetWindowLongPtrA(hWindowChild, GWL_EXSTYLE);

	style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_POPUP);
	exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);

	SetWindowLongPtrA(hWindowChild, GWL_STYLE, style | WS_CHILD);
	SetWindowLongPtrA(hWindowChild, GWL_EXSTYLE, exStyle | WS_EX_MDICHILD | WS_EX_LAYERED | WS_EX_COMPOSITED);

	SetParent(hWindowChild, hWindow);

	RECT rt2;
	GetClientRect(hWindow, &rt2);
	SetWindowPos(hWindowChild, HWND_TOP, 0, 0, rt2.right - rt2.left, rt2.bottom - rt2.top, SWP_SHOWWINDOW);

	MSG msg;
	while ((GetMessageW(&msg, NULL, 0, 0)) != 0)
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	ExitProcess(0);
}


BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

