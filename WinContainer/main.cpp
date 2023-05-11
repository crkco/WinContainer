// Compile as multithreaded /MT for statically linked runtime

#pragma comment(lib, "Comctl32.lib")	// Quick lib comment for visual studio
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "UxTheme.lib")

#include <Windows.h>
#include <commctrl.h>		// Common Controls (List-View, Buttons, ...)
#include <Uxtheme.h>		// Additional Themes (Explorer for List-View)
#include <iostream>			
#include <Psapi.h>			// Retrieve Process Info (File path of .exe)
#include <shlobj.h>			// Shell API (Extract icon from .exe)
#include <shlwapi.h>
#include <string>

/* Using version 6 of commctrl for modern look and feel */
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

/* Function declarations */

LRESULT CALLBACK ControlProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
HICON GetAppIcon(HWND hwnd, DWORD pid);
BOOL InitListViewColumns(HWND hWndListView);
BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam);
inline void ErrorBox(const wchar_t* errStr);

/* Precompiler definitions */

#define LIST_VIEW_COL_COUNT				4		// Number of columns of the window list view

#define INIT_PARENT_WINDOW_WIDTH		550		// Initial width of main window
#define INIT_PARENT_WINDOW_HEIGHT		500		// Initial height of main window

#define INIT_CONTROL_CONTAINER_WIDTH	INIT_PARENT_WINDOW_WIDTH	// Initial width of the lower child window containing button controls
#define INIT_CONTROL_CONTAINER_HEIGHT	50							// Initial height of this child window

#define INIT_CONTROL_CONTAINER_Y		(INIT_PARENT_WINDOW_HEIGHT - INIT_CONTROL_CONTAINER_HEIGHT)		// Initial y position of this child window inside the main window

#define INIT_BTN1_REL_L_X				20		// Position constants of buttons
#define INIT_BTN1_WIDTH					90

#define INIT_BTN2_REL_L_X				20
#define INIT_BTN2_WIDTH					90

#define INIT_BTN3_REL_R_X				20
#define INIT_BTN3_WIDTH					100

#define INIT_BTN_HEIGHT					25
#define INIT_BTN_MARGIN					15

/* Array of title strings for each column of the window list */
const WCHAR* listViewColumnsStr[LIST_VIEW_COL_COUNT] =
{
	L"Handle", L"Window Title", L"Class Name", L"PID"
};

/* Array of pixel widths for each column of the window list */
const int listViewColumnsWidth[LIST_VIEW_COL_COUNT] =
{
	90, 200, 100, 50
};

int enumWindowsIndex = 0;	// Index of list items while enumarating
int selectedItem = -1;
int lastScrollPos = -1;

bool restoreScrollPos = false;

HWND hWndControlContainer = NULL;	// Handle to child window containing controls
HWND hwndButton1 = NULL;			// Buttons
HWND hwndButton2 = NULL;
HWND hwndButton3 = NULL;
HWND hWndListView = NULL;			// Handle to window list view
HIMAGELIST himl = NULL;				// Handle to an image list to be populated with app icons

inline void ErrorBox(const wchar_t* errStr)
{
	MessageBoxW(NULL, (LPCWSTR)errStr, (LPCWSTR)(std::wstring(L"Error code: ") + std::to_wstring(GetLastError())).c_str(), MB_ICONERROR | MB_OK);
	ExitProcess(0);
}

#pragma warning(push)					// Disable visual studio warning for possible NULL, even if it is checked and terminated in ErrorBox
#pragma warning(disable : 6387)

/* Injects a dll into process of specified window handle */
void injectDLL(HWND hwnd)
{
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);			// Retrieve process ID from window handle

	WCHAR dllPath[1000] = {};
	WCHAR dllName[20] = { L"WinContainerLib.dll" };

	if (GetModuleFileNameW(NULL, dllPath, 1000) == 0)			// Modify .exe path to become .dll path
		ErrorBox(L"dllPath - GetModuleFileNameW returned 0");
	PathRemoveFileSpecW(dllPath);
	PathAddExtensionW(dllPath, (LPWSTR)L"\\");
	PathAddExtensionW(dllPath, (LPWSTR)dllName);

	int dllPathSize = sizeof(dllPath);

	HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);	// *** Maybe lower access rights
	if (process == NULL)
		ErrorBox(L"process - OpenProcess returned NULL");

	WCHAR* dllRemotePath = (WCHAR*)VirtualAllocEx(process, NULL, dllPathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);	// Allocate memory in remote process for storing dllPath
	if (dllRemotePath == NULL)
		ErrorBox(L"dllRemotePath - VirtualAllocEx returned NULL");

	SIZE_T nrBytes = 0;
	if (WriteProcessMemory(process, dllRemotePath, dllPath, dllPathSize, &nrBytes) == FALSE)		// Write the dllPath to its remote location
		ErrorBox(L"dllRemotePath - WriteProcessMemory returned FALSE");

#pragma warning(suppress : 6387)
	FARPROC kernel32_LoadLibraryW = GetProcAddress(GetModuleHandleW(L"kernel32"), "LoadLibraryW");		// Retrieve LoadLibraryW address, which is shared across processes

	HANDLE thread = CreateRemoteThread(		// Run LoadLibraryW in remote thread, with the parameter of the remote dllPath
		process,
		NULL,
		0,
		(LPTHREAD_START_ROUTINE)kernel32_LoadLibraryW,
		(LPVOID)dllRemotePath,
		0,
		NULL
	);

	if (thread == NULL)
		ErrorBox(L"thread - CreateRemoteThread returned NULL");

	if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0)		// Wait for dll finished loading
		ErrorBox(L"thread - WaitForSingleObject failed");

	HMODULE modules[1000] = {};		// *** Maybe do dynamic allocation
	DWORD nModules = 0;
	K32EnumProcessModules(process, modules, sizeof(modules), &nModules);	// Retrieve a list of remote dll addresses

	nModules = nModules / sizeof(HMODULE);

	WCHAR baseName[100] = {};
	HMODULE dllRemoteBase = NULL;

	for (int i = 0; i < (int)nModules; i++)
	{
		memset(baseName, 0, sizeof(baseName));
		K32GetModuleBaseNameW(process, modules[i], baseName, 100);	// Turn addresses into names

		if (lstrcmpiW(dllName, baseName) == 0)		// Check if our dll name is part of the remote dll list
		{
			dllRemoteBase = (HMODULE)modules[i];	// Retrieve its remote base address
		}
	}

	if (dllRemoteBase == NULL)
		ErrorBox(L"threadMain - Couldn't retrieve address of WinContainerLib.dll");

	HMODULE dllLocalBase = LoadLibraryW(dllPath);		// Load the same dll into the current process, so we can figure out the function address | DllMain should be empty
	if (dllLocalBase == NULL)
		ErrorBox(L"dllLocalBase - LoadLibraryW returned NULL");

	LPTHREAD_START_ROUTINE localThreadMain = (LPTHREAD_START_ROUTINE)GetProcAddress(dllLocalBase, "threadMain");	// Retrieve function address
	if (localThreadMain == NULL)
		ErrorBox(L"localThreadMain - GetProcAddress returned NULL");

	FreeLibrary(dllLocalBase);

	/*Calculate relative function address and add it to the remote base address*/
	LPTHREAD_START_ROUTINE remoteThreadMain = (LPTHREAD_START_ROUTINE)((LONGLONG)localThreadMain - (LONGLONG)dllLocalBase + (LONGLONG)dllRemoteBase);

	HANDLE thread2 = CreateRemoteThread(		// Call this remote function
		process,
		NULL,
		0,
		remoteThreadMain,
		(LPVOID)hwnd,
		0,
		NULL
	);

	if (thread2 == NULL)
		ErrorBox(L"thread2 - CreateRemoteThread returned NULL");

	CloseHandle(process);
}

#pragma warning(pop)

LRESULT CALLBACK ControlProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	int dpi = GetDpiForWindow(hWnd);	// Retrieve the system dpi for scaling controls

	switch (Msg)
	{
	case WM_COMMAND:
	{
		if (HIWORD(wParam) == BN_CLICKED)		// Check if button was pressed
		{
			if ((int)LOWORD(wParam) == 1)	// This is for refreshing the window list
			{
				lastScrollPos = GetScrollPos(hWndListView, SB_VERT);	// Save the scroll position

				ListView_DeleteAllItems(hWndListView);		// Delete the item list and start another enumeration 
				enumWindowsIndex = 0;
				EnumWindows(EnumWindowsCallback, NULL);

				restoreScrollPos = true;		// Can't yet restore the scroll position, so do it later (maybe wrong)

				EnableWindow(hwndButton3, FALSE);		// Disable the select button because no item is now selected
			}
			else if ((int)LOWORD(wParam) == 3)	// This is for selecting the window and starting the process
			{
				LVITEM lvItem = { 0 };
				lvItem.iItem = selectedItem;
				lvItem.iSubItem = 0;
				lvItem.mask = LVIF_PARAM;	// Only retrieve custom lparam

				ListView_GetItem(hWndListView, &lvItem);

				injectDLL((HWND)lvItem.lParam);		// Load the dll into the remote process

				ExitProcess(0);		// Jobs done
			}
		}
	}
	break;
	default:
		return DefWindowProcA(hWnd, Msg, wParam, lParam);
	}

	return 0;
}

/* Window procedure of the main window */
LRESULT CALLBACK WindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	int dpi = GetDpiForWindow(hWnd);	// Retrieve the system dpi for scaling controls

	switch (Msg)
	{
	case WM_CREATE:
	{
		HINSTANCE hInstance = (HINSTANCE)GetModuleHandleW(NULL);

		RECT rcClient;
		GetClientRect(hWnd, &rcClient);		// Retrieve the left, right, top, bottom coordinates of the client area

		HFONT hFont = CreateFont(MulDiv(16, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,			// Replicate the system font with modular font size, scaled by dpi
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

		{ /* ControlContainer: This child window contains the button controls at the bottom of the parent window */

			WNDCLASSA wndClass = { 0 };
			wndClass.lpszClassName = "_WinContainer_ControlContainer";
			wndClass.lpfnWndProc = ControlProc;			// Use the default window procedure, since it only contains passive child windows
			wndClass.hCursor = LoadCursorA(NULL, MAKEINTRESOURCEA(32512));		// Set to the default system cursor
			wndClass.hbrBackground = CreateSolidBrush(RGB(241, 243, 249));		// Set the color to Windows 11 title bar color
			RegisterClassA(&wndClass);

			int wndClientWidth = MulDiv(INIT_CONTROL_CONTAINER_WIDTH, dpi, 96);		// Scale the width and height according to the system dpi influenced by system scale
			int wndClientHeight = MulDiv(INIT_CONTROL_CONTAINER_HEIGHT, dpi, 96);
			int wndClientX = 0;
			int wndClientY = MulDiv(INIT_CONTROL_CONTAINER_Y, dpi, 96);				// Set the initial y position calculated by its initial height and the initial main client height

			hWndControlContainer = CreateWindowExA(
				0L,
				wndClass.lpszClassName,
				"Control Container",
				WS_CHILD,		// Make it only a rectangle child window
				wndClientX, wndClientY,
				wndClientWidth, wndClientHeight,
				hWnd, NULL,		// Set the parent to the main window
				hInstance,
				NULL);

			ShowWindow(hWndControlContainer, SW_SHOW);
			UpdateWindow(hWndControlContainer);

			int btnHeight = MulDiv(INIT_BTN_HEIGHT, dpi, 96);
			int btnY = (wndClientHeight - btnHeight) / 2;

			{ /* Button 1 "Refresh" */
				int btnWidth = MulDiv(INIT_BTN1_WIDTH, dpi, 96);
				int btnX = MulDiv(INIT_BTN1_REL_L_X, dpi, 96);

				hwndButton1 = CreateWindowExA(
					WS_EX_TOPMOST,
					"BUTTON",
					"Refresh",
					WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
					btnX, btnY,
					btnWidth, btnHeight,
					hWndControlContainer, (HMENU)1,		// Set the parent to control container
					hInstance,
					NULL);

				SendMessage(hwndButton1, WM_SETFONT, (WPARAM)hFont, TRUE);

				ShowWindow(hwndButton1, SW_SHOW);
				UpdateWindow(hwndButton1);
			}

			{	/* Button 2 Cursor Select */
				int btnWidth = MulDiv(INIT_BTN2_WIDTH, dpi, 96);
				int btnX = MulDiv(INIT_BTN2_REL_L_X, dpi, 96) + MulDiv(INIT_BTN_MARGIN, dpi, 96) + MulDiv(INIT_BTN1_WIDTH, dpi, 96);

				hwndButton2 = CreateWindowExA(
					WS_EX_TOPMOST,
					"BUTTON",
					"Cursor",
					WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_DISABLED,
					btnX, btnY,
					btnWidth, btnHeight,
					hWndControlContainer, (HMENU)2,		// Set the parent to control container
					hInstance,
					NULL);

				SendMessage(hwndButton2, WM_SETFONT, (WPARAM)hFont, TRUE);

				ShowWindow(hwndButton2, SW_SHOW);
				UpdateWindow(hwndButton2);
			}

			{	/* Button 3 Select */
				int btnWidth = MulDiv(INIT_BTN3_WIDTH, dpi, 96);
				int btnX = wndClientWidth - btnWidth - MulDiv(INIT_BTN1_REL_L_X, dpi, 96);

				hwndButton3 = CreateWindowExA(
					WS_EX_TOPMOST,
					"BUTTON",
					"Select",
					WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_DISABLED,
					btnX, btnY,
					btnWidth, btnHeight,
					hWndControlContainer, (HMENU)3,		// Set the parent to control container
					hInstance,
					NULL);

				SendMessage(hwndButton3, WM_SETFONT, (WPARAM)hFont, TRUE);

				ShowWindow(hwndButton3, SW_SHOW);
				UpdateWindow(hwndButton3);
			}
		}

		{ /* ListView: This control has columns and rows listing the windows with its details (title, pid, ...) */

			INITCOMMONCONTROLSEX icex = {};
			icex.dwICC = ICC_LISTVIEW_CLASSES;
			InitCommonControlsEx(&icex);		// Initialize common controls to be able to create the list view control

			hWndListView = CreateWindowExW(
				0L,				// Ex style can't be put here and need to be added after
				WC_LISTVIEW,
				L"",
				WS_CHILD | LVS_REPORT | LVS_SINGLESEL,		// LVS_REPORT for making it a detailed list and LVS_SINGLESEL to allow only one row to be selected
				0, 0,
				rcClient.right - rcClient.left,				// Occupy full width of the client area
				MulDiv(INIT_CONTROL_CONTAINER_Y, dpi, 96),	// Limit height to the y position of the control container
				hWnd,
				(HMENU)NULL,
				hInstance,
				NULL);

			InitListViewColumns(hWndListView);				// Create the columns

			SetWindowTheme(hWndListView, L"Explorer", NULL);	// Small changes in list view look

			/* Add ex styles for double buffering and instead of single item selection, select the full row */
			ListView_SetExtendedListViewStyle(hWndListView, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

			SendMessage(hWndListView, WM_SETFONT, (WPARAM)hFont, TRUE);

			ShowWindow(hWndListView, SW_SHOW);
			UpdateWindow(hWndListView);

		}
	}
	break;
	case WM_NOTIFY:
	{
		if (restoreScrollPos)	// Restore scroll position by scrolling correct amount of pixels
		{
			restoreScrollPos = false;

			if (lastScrollPos != -1)
			{
				RECT rt = {};
				ListView_GetItemRect(hWndListView, 0, &rt, LVIR_BOUNDS);

				ListView_Scroll(hWndListView, 0, (rt.bottom - rt.top) * lastScrollPos);
			}
		}

#pragma warning(suppress : 26454)
		if (((NMHDR*)lParam)->code == LVN_ITEMCHANGED)
		{
			NMLISTVIEW* listView = (NMLISTVIEW*)lParam;

			if ((listView->uNewState &= LVIS_SELECTED))		// Check if item has been selected and enable select button
			{
				selectedItem = listView->iItem;
				EnableWindow(hwndButton3, TRUE);
				break;
			}

			if ((listView->uOldState &= LVIS_SELECTED))		// Check if item has been unselected and disable select button
			{
				if (selectedItem == listView->iItem)
				{
					listView->iItem = -1;
					EnableWindow(hwndButton3, FALSE);
				}
			}
		}
	}
	break;
	case WM_GETMINMAXINFO: // Make it so window can't resize smaller than buttons overlapping width
	{
		MINMAXINFO* mmInfo = (MINMAXINFO*)lParam;

		mmInfo->ptMinTrackSize.x = (MulDiv(INIT_BTN2_REL_L_X + 3 * INIT_BTN_MARGIN + INIT_BTN1_WIDTH + INIT_BTN2_WIDTH + INIT_BTN3_WIDTH + INIT_BTN1_REL_L_X, dpi, 96));
	}
	break;
	case WM_SIZE:
	{
		RECT rt;
		GetClientRect(hWnd, &rt);

		int wndClientWidth = rt.right - rt.left;
		int wndClientHeight = rt.bottom - rt.top;
		int wndClientX = rt.left;
		int wndClientY = rt.top;

		/* Set the list-view width to parent width, and height to parent height - control window height */
		int listViewWidth = wndClientWidth;
		int listViewHeight = wndClientHeight - MulDiv(INIT_CONTROL_CONTAINER_HEIGHT, dpi, 96);
		int listViewX = 0;
		int listViewY = 0;

		/* Set the control window width to parent width, and height remains unchanged */
		int ctrlClientWidth = wndClientWidth;
		int ctrlClientHeight = MulDiv(INIT_CONTROL_CONTAINER_HEIGHT, dpi, 96);
		int ctrlClientX = 0;
		int ctrlClientY = wndClientHeight - MulDiv(INIT_CONTROL_CONTAINER_HEIGHT, dpi, 96);

		/* Refresh button */
		int btn1Width = MulDiv(INIT_BTN1_WIDTH, dpi, 96);
		int btn1Height = MulDiv(INIT_BTN_HEIGHT, dpi, 96);
		int btn1X = MulDiv(INIT_BTN1_REL_L_X, dpi, 96);
		int btn1Y = (ctrlClientHeight - btn1Height) / 2;

		/* Cursor button */
		int btn2Width = MulDiv(INIT_BTN2_WIDTH, dpi, 96);
		int btn2Height = MulDiv(INIT_BTN_HEIGHT, dpi, 96);
		int btn2X = MulDiv(INIT_BTN2_REL_L_X, dpi, 96) + MulDiv(INIT_BTN_MARGIN, dpi, 96) + MulDiv(INIT_BTN1_WIDTH, dpi, 96);
		int btn2Y = (ctrlClientHeight - btn1Height) / 2;

		/* Select button */
		int btn3Width = MulDiv(INIT_BTN3_WIDTH, dpi, 96);
		int btn3Height = MulDiv(INIT_BTN_HEIGHT, dpi, 96);
		int btn3X = ctrlClientWidth - btn3Width - MulDiv(INIT_BTN1_REL_L_X, dpi, 96);
		int btn3Y = (ctrlClientHeight - btn1Height) / 2;

		SetWindowPos(hWndListView, HWND_TOP, listViewX, listViewY, wndClientWidth, listViewHeight, SWP_SHOWWINDOW);
		SetWindowPos(hWndControlContainer, HWND_TOP, ctrlClientX, ctrlClientY, ctrlClientWidth, ctrlClientHeight, SWP_SHOWWINDOW);
		SetWindowPos(hwndButton1, HWND_TOP, btn1X, btn1Y, btn1Width, btn1Height, SWP_SHOWWINDOW);
		SetWindowPos(hwndButton2, HWND_TOP, btn2X, btn2Y, btn2Width, btn2Height, SWP_SHOWWINDOW);
		SetWindowPos(hwndButton3, HWND_TOP, btn3X, btn3Y, btn3Width, btn3Height, SWP_SHOWWINDOW);
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

#pragma warning(suppress : 28251)	// VS is dumb
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	SetProcessDPIAware();		// Make it so system dpi is available

	int dpi = GetDpiForSystem();	// Retrieve system dpi

	WNDCLASSA wndClass = { 0 };
	wndClass.lpszClassName = "_WinContainer";
	wndClass.lpfnWndProc = (WNDPROC)WindowProc;		// Use the user defined window procedure
	wndClass.hCursor = LoadCursorA(NULL, MAKEINTRESOURCEA(32512));	// Set to default cursor
	RegisterClassA(&wndClass);

	int wndClientWidth = MulDiv(INIT_PARENT_WINDOW_WIDTH, dpi, 96);		// Scale width and height according to dpi
	int wndClientHeight = MulDiv(INIT_PARENT_WINDOW_HEIGHT, dpi, 96);

	RECT rt = { 0, 0, wndClientWidth, wndClientHeight };
	AdjustWindowRect(&rt, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hWindow = CreateWindowExA(
		0L,
		wndClass.lpszClassName,
		"WinContainer",
		WS_OVERLAPPEDWINDOW,
		400, 400,
		rt.right - rt.left, rt.bottom - rt.top,		// Use adjusted rect coords to ensure the correct client area size
		NULL, NULL,
		hInstance,
		NULL);

	EnumWindows(EnumWindowsCallback, NULL);		// This calls EnumWindowsCallback with every top level window handle

	ShowWindow(hWindow, SW_SHOW);		// *** Maybe change order, so this window is also listed
	UpdateWindow(hWindow);

	MSG msg;

	while ((GetMessageW(&msg, NULL, 0, 0)) != 0)		// Typical message loop for parent window
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	return 0;
}

/* Uses a variety of ways to retrieve the icon of a running app from its window handle */
HICON GetAppIcon(HWND hwnd, DWORD pid)
{
	/* First ask the window nicely */

	HICON iconHandle = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0);

	if (iconHandle == NULL)
	{
		iconHandle = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
	}

	if (iconHandle == NULL)
	{
		iconHandle = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
	}

	if (iconHandle == NULL)
	{
		iconHandle = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
	}

	if (iconHandle == NULL)
	{
		iconHandle = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
	}

	/* If the handle is still NULL, then don't ask it nicely */
	if (iconHandle == NULL)
	{
		HANDLE processHandle = NULL;
		TCHAR filename[MAX_PATH] = {};

		/* Retrieve .exe path using remote process function */
		processHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
		if (processHandle != NULL)
		{
			if (K32GetModuleFileNameExW(processHandle, NULL, filename, MAX_PATH) == 0)
			{
				std::cerr << "Failed to get module filename." << std::endl;
			}
			CloseHandle(processHandle);
		}
		else
		{
			std::cerr << "Failed to open process." << std::endl;
		}

		SHDefExtractIconW(filename, 0, 0, &iconHandle, NULL, 48);	// Extract the icon out of .exe using the file path
	}

	/* If it still doesn't load an icon, just use the default windows app icon */
	if (iconHandle == NULL)
	{
		return LoadIconW(NULL, IDI_APPLICATION);
	}

	return iconHandle;
}

/* Retrieve each window handle of top level windows */
BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam)
{
	if (!IsWindowVisible(hwnd))	// Filter out windows that are not visible
	{
		return TRUE;
	}

	if (enumWindowsIndex == 0)	// If we are at the start, then create a new image list for storing icons
	{
		if (himl != NULL)
		{
			ImageList_Destroy(himl);
			himl = NULL;
		}

		himl = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), ILC_COLOR32, 0, 1000);	// COLOR32 important for visual quality
		ListView_SetImageList(hWndListView, himl, LVSIL_SMALL);
	}

	WCHAR wstr[80] = {};
	GetWindowTextW(hwnd, wstr, 80);		// Retrieve title

	WCHAR whandle[40] = {};
	swprintf(whandle, 40, L"%08X", (int)(INT_PTR)hwnd);	// Convert window handle to hex string

	WCHAR wclsname[40] = {};
	GetClassNameW(hwnd, wclsname, 40);	// Class name

	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);	// PID

	WCHAR wpid[40] = {};
	swprintf(wpid, 40, L"%04X", (int)pid);

	HICON hIcon = (HICON)GetAppIcon(hwnd, pid);		// Icon
	ImageList_ReplaceIcon(himl, -1, hIcon);

	LVITEM lvItem = { 0 };		// Struct for adding rows
	lvItem.iItem = 0;			// Both 0 to insert at start
	lvItem.iSubItem = 0;
	lvItem.mask = LVIF_TEXT | LVIF_STATE | LVIF_IMAGE | LVIF_PARAM;
	lvItem.pszText = (LPWSTR)whandle;
	lvItem.iImage = enumWindowsIndex;
	lvItem.lParam = (LPARAM)hwnd;

	ListView_InsertItem(hWndListView, &lvItem);			// Insert first item and then set the rest columns to the appropiate text
	ListView_SetItemText(hWndListView, 0, 1, wstr);
	ListView_SetItemText(hWndListView, 0, 2, wclsname);
	ListView_SetItemText(hWndListView, 0, 3, wpid);

	enumWindowsIndex++;

	return TRUE;
}

/* Add the columns to the view list */
BOOL InitListViewColumns(HWND hWndListView)
{
	/* Copied from microsoft */

	LVCOLUMN lvc = {};
	int iCol;

	int dpi = GetDpiForWindow(hWndListView);

	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

	for (iCol = 0; iCol < LIST_VIEW_COL_COUNT; iCol++)
	{
		lvc.iSubItem = iCol;
		lvc.pszText = (LPWSTR)listViewColumnsStr[iCol];			// Use both arrays for titles and scaled widths
		lvc.cx = MulDiv(listViewColumnsWidth[iCol], dpi, 96);
		lvc.fmt = LVCFMT_LEFT | LVCFMT_NO_DPI_SCALE;

		if (ListView_InsertColumn(hWndListView, iCol, &lvc) == -1)
			return FALSE;
	}

	return TRUE;
}

