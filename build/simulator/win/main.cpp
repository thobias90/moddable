/*
 * Copyright (c) 2016-2022 Moddable Tech, Inc.
 *
 *   This file is part of the Moddable SDK Tools.
 * 
 *   The Moddable SDK Tools is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 * 
 *   The Moddable SDK Tools is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 * 
 *   You should have received a copy of the GNU General Public License
 *   along with the Moddable SDK Tools.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <ole2.h>
#include <gdiplus.h>
using namespace Gdiplus;
#include <stdio.h>
#include <screen.h>

typedef struct {
	IStream* stream;
	Image* image;
	int x;
	int y;
	int width;
	int height;
	char* title;
	char* name;
} txMockup;

static BOOL CALLBACK fxMockupCount(HMODULE hModule, LPCTSTR lpszType, LPTSTR lpszName, LONG_PTR lParam);
static BOOL CALLBACK fxMockupCreate(HMODULE hModule, LPCTSTR lpszType, LPTSTR lpszName, LONG_PTR lParam);
static void fxMockupCreateAux(txMockup* mockup, char* nameFrom, char* nameTo, char* valueFrom, char* valueTo);
static void fxScreenAbort(txScreen* screen, int status);
static void fxScreenBufferChanged(txScreen* screen);
static void fxScreenFormatChanged(txScreen* screen);
static void fxScreenStart(txScreen* screen, double interval);
static void fxScreenStop(txScreen* screen);
static LRESULT CALLBACK fxScreenWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK fxScreenViewProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

#define WM_CREATE_SCREEN WM_USER
#define WM_DELETE_SCREEN WM_USER + 1
#define WM_OPEN_FILE WM_USER + 2

#define WM_LAUNCH_MACHINE WM_USER
#define WM_QUIT_MACHINE WM_USER + 1

HDC gDC = NULL;
HINSTANCE gInstance = NULL;

long gxMockupCount = 0;
long gxMockupIndex = 0;
txMockup** gxMockups = NULL;

char* gxFormatNames[pixelFormatCount] = {
	"16-bit RGB 565 Little Endian",
	"16-bit RGB 565 Big Endian",
	"8-bit Gray",
	"8-bit RGB 332",
	"4-bit Gray",
	"4-bit Color Look-up Table",
};

txScreen* gxScreen = NULL;
Bitmap* gxScreenBitmap = NULL;

char gxArchiveName[MAX_PATH] = "";
char gxArchivePath[MAX_PATH] = "";
HANDLE gxArchiveFile = INVALID_HANDLE_VALUE;
HANDLE gxArchiveMapping = INVALID_HANDLE_VALUE;
char gxLibraryName[MAX_PATH] = "";
char gxLibraryPath[MAX_PATH] = "";
HMODULE gxLibrary = NULL;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	COPYDATASTRUCT cds;
	cds.cbData = (DWORD)strlen(lpCmdLine);
	if (cds.cbData > 0) {
		cds.cbData++;
		cds.lpData = lpCmdLine;
	}
    HANDLE hMutex = OpenMutex(MUTEX_ALL_ACCESS, 0, "Screen Test");
    if (hMutex) {
		HWND window = FindWindow("ScreenWindow", NULL);
		SetForegroundWindow(window);
		if (cds.cbData)
			SendMessage(window, WM_COPYDATA, 0, (LPARAM)&cds);
    	return 0;
    }
    hMutex = CreateMutex(0, 0, "Screen Test");

	ULONG_PTR m_gdiplusToken;
	GdiplusStartupInput gdiplusStartupInput;
	GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);
	
	gDC = GetDC(NULL);
	gInstance = hInstance;
	
	EnumResourceNames(hInstance, "JSON", fxMockupCount, NULL);
	gxMockups = (txMockup**)calloc(gxMockupCount, sizeof(txMockup*));
	EnumResourceNames(hInstance, "JSON", fxMockupCreate, NULL);
	gxMockupIndex = 0;

	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = fxScreenWindowProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(100));
	wcex.hIconSm = NULL;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = "ScreenWindow";
	RegisterClassEx(&wcex);
	
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = fxScreenViewProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = NULL;
	wcex.hIconSm = NULL;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = NULL;
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = "ScreenView";
	RegisterClassEx(&wcex);
	
	WORD accelerators[2][3]; // ACCEL misaligned with /Zp1
	HMENU menubar = CreateMenu();
	
	HMENU fileMenu = CreateMenu();
	AppendMenu(fileMenu, MF_STRING, 0x0100, "Open...\tCtrl+O");
	accelerators[0][0] = FCONTROL | FVIRTKEY;
	accelerators[0][1] = LOBYTE(VkKeyScan('O'));
	accelerators[0][2] = 0x0100;
	AppendMenu(fileMenu, MF_STRING, 0x0101, "Close\tCtrl+W");
	accelerators[1][0] = FCONTROL | FVIRTKEY;
	accelerators[1][1] = LOBYTE(VkKeyScan('W'));
	accelerators[1][2] = 0x0101;
	AppendMenu(fileMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(fileMenu, MF_STRING, 0x0102, "Quit\tCtrl+Q");
	accelerators[1][0] = FCONTROL | FVIRTKEY;
	accelerators[1][1] = LOBYTE(VkKeyScan('Q'));
	accelerators[1][2] = 0x0102;
	AppendMenu(menubar, MF_POPUP, (UINT_PTR)fileMenu, "File");
	
	HMENU sizeMenu = CreateMenu();
	for (gxMockupIndex = 0; gxMockupIndex < gxMockupCount; gxMockupIndex++)
		AppendMenu(sizeMenu, MF_STRING, (UINT)0x0200 + gxMockupIndex, gxMockups[gxMockupIndex]->title);
	gxMockupIndex = 0;
	AppendMenu(menubar, MF_POPUP, (UINT_PTR)sizeMenu, "Size");
	
	HMENU helpMenu = CreateMenu();
	AppendMenu(helpMenu, MF_STRING, 0x0301, "Moddable Developer");
	AppendMenu(helpMenu, MF_SEPARATOR, 0, NULL);
	AppendMenu(helpMenu, MF_STRING, 0x0302, "About Screen Test");
	AppendMenu(menubar, MF_POPUP, (UINT_PTR)helpMenu, "Help");

	char path[MAX_PATH];
	SHGetSpecialFolderPath(NULL, path, CSIDL_LOCAL_APPDATA, TRUE);
	strcpy(gxLibraryPath, path);
	strcat(gxLibraryPath, "\\tech.moddable.simulator.so");
	strcpy(gxArchivePath, path);
	strcat(gxArchivePath, "\\tech.moddable.simulator.xsa");
	
	HWND window = CreateWindowEx(0, "ScreenWindow", "Screen Test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, menubar, hInstance, NULL);
	ShowWindow(window, nCmdShow);
	SetFocus(window);
	if (cds.cbData)
		SendMessage(window, WM_COPYDATA, 0, (LPARAM)&cds);
		
	HACCEL haccel = CreateAcceleratorTable((LPACCEL)accelerators, 2);		
	MSG msg;
    while( GetMessage(&msg, NULL, 0, 0) > 0 ) {
		if (TranslateAccelerator(window, haccel, &msg))
			continue;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	DestroyAcceleratorTable(haccel);
    GdiplusShutdown(m_gdiplusToken);
    return 0;
}

BOOL CALLBACK fxMockupCount(HMODULE hModule, LPCTSTR lpszType, LPTSTR lpszName, LONG_PTR lParam)
{
	HRSRC pngRsrc = FindResource(gInstance, lpszName, "PNG");
	HRSRC jsonRsrc = FindResource(gInstance, lpszName, "JSON");
	if (!pngRsrc || !jsonRsrc) return TRUE;
	gxMockupCount++;
	return TRUE;
}

enum {
	JSON_FILE = 0,
	JSON_OBJECT, 
	JSON_NAME,
	JSON_COLON,
	JSON_VALUE,
	JSON_NUMBER,
	JSON_STRING,
	JSON_COMMA,
};

BOOL CALLBACK fxMockupCreate(HMODULE hModule, LPCTSTR lpszType, LPTSTR lpszName, LONG_PTR lParam)
{
	HRSRC pngRsrc = FindResource(gInstance, lpszName, "PNG");
	HRSRC jsonRsrc = FindResource(gInstance, lpszName, "JSON");
	if (!pngRsrc || !jsonRsrc) return TRUE;
	txMockup* mockup = (txMockup*)calloc(1, sizeof(txMockup));

	HGLOBAL pngGlobal = LoadResource(gInstance, pngRsrc);
	void* bytes = LockResource(pngGlobal);
	ULONG pngSize = SizeofResource(gInstance, pngRsrc);
	CreateStreamOnHGlobal(NULL, TRUE, &mockup->stream);
    ULONG written;
    mockup->stream->Write(bytes, pngSize, &written);
	mockup->image = new Image(mockup->stream, FALSE);

	HGLOBAL jsonGlobal = LoadResource(gInstance, jsonRsrc);
	char* string = (char*)LockResource(jsonGlobal);
	ULONG size = SizeofResource(gInstance, jsonRsrc);
	ULONG offset = 0;
	char* nameFrom;
	char* nameTo;
	char* valueFrom;
	char* valueTo;
	int state = JSON_FILE;
	while (offset < size) {
		char c = *string;
		switch (c) {
		case '{':
			state = JSON_OBJECT;
			break;
		case '}':
			state = JSON_FILE;
			break;
		case ':':
			if (state == JSON_COLON)
				state = JSON_VALUE;
			break;
		case ',':
			if (state == JSON_NUMBER) {
				valueTo = string;
				fxMockupCreateAux(mockup, nameFrom, nameTo, valueFrom, valueTo);
				state = JSON_OBJECT;
			}
			else if (state == JSON_COMMA) {
				state = JSON_OBJECT;
			}
			break;
		case '"':
			if (state == JSON_OBJECT) {
				nameFrom = string + 1;
				state = JSON_NAME;
			}
			else if (state == JSON_NAME) {
				nameTo = string;
				state = JSON_COLON;
			}
			else if (state == JSON_VALUE) {
				valueFrom = string + 1;
				state = JSON_STRING;
			}
			else if (state == JSON_STRING) {
				valueTo = string;
				fxMockupCreateAux(mockup, nameFrom, nameTo, valueFrom, valueTo);
				state = JSON_COMMA;
			}
			break;
		default:
			if (32 < c) {
				if (state == JSON_VALUE) {
					valueFrom = string;
					state = JSON_NUMBER;
				}
			}
			else {
				if (state == JSON_NUMBER) {
					valueTo = string;
					fxMockupCreateAux(mockup, nameFrom, nameTo, valueFrom, valueTo);
					state = JSON_COMMA;
				}
			}
			break;
		}
		string++;
		offset++;
	}
	offset = 0;
	size = (ULONG)strlen(lpszName) - 5;
	mockup->name = (char*)malloc(size + 1);
	while (offset < size) {
		mockup->name[offset] = tolower(lpszName[5 + offset]);
		offset++;
	}
	mockup->name[offset] = 0;
	gxMockups[gxMockupIndex] = mockup;
	gxMockupIndex++;
	return TRUE;
}

void fxMockupCreateAux(txMockup* mockup, char* nameFrom, char* nameTo, char* valueFrom, char* valueTo)
{
	if (!strncmp("title", nameFrom, nameTo - nameFrom)) {
		size_t length = valueTo - valueFrom;
		mockup->title = (char*)malloc(length + 1);
		memcpy(mockup->title, valueFrom, length);
		mockup->title[length] = 0;
	}
	else if (!strncmp("x", nameFrom, nameTo - nameFrom))
		mockup->x = atoi(valueFrom);
	else if (!strncmp("y", nameFrom, nameTo - nameFrom))
		mockup->y = atoi(valueFrom);
	else if (!strncmp("width", nameFrom, nameTo - nameFrom))
		mockup->width = atoi(valueFrom);
	else if (!strncmp("height", nameFrom, nameTo - nameFrom))
		mockup->height = atoi(valueFrom);
}

void fxScreenAbort(txScreen* screen, int status)
{
	HWND view = (HWND)screen->view;
	PostMessage(view, WM_QUIT_MACHINE, status, 0);
}

void fxScreenBufferChanged(txScreen* screen)
{
	HWND view = (HWND)screen->view;
	InvalidateRect(view, NULL, TRUE);
}

void fxScreenFormatChanged(txScreen* screen)
{
	HWND view = (HWND)screen->view;
	HWND window = GetParent(view);
	char title[MAX_PATH];
	strcpy(title, "Screen Test - ");
	strcat(title, gxLibraryName);
	strcat(title, " - ");
	if (gxArchiveName[0]) {
		strcat(title, gxArchiveName);
		strcat(title, " - ");
	}
	strcat(title, gxFormatNames[screen->pixelFormat]);
	SetWindowText(window, title);
}

void fxScreenStart(txScreen* screen, double interval)
{
	HWND view = (HWND)screen->view;
	UINT id = 0;
	SetTimer(view, 0, (UINT)interval, NULL);
}

void fxScreenStop(txScreen* screen)
{
	HWND view = (HWND)screen->view;
	UINT id = 0;
	KillTimer(view, 0);
}

void fxScreenStop(HWND window, char* )
{
}

LRESULT CALLBACK fxScreenWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)	{
	case WM_CLOSE: {
		HWND view = GetWindow(window, GW_CHILD);
		SendMessage(view, WM_QUIT_MACHINE, 0, 0);
		HKEY key;
		if (RegCreateKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\moddable.tech\\Screen Test\\screen", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
			RegSetValueEx(key, "index", 0, REG_DWORD, (const BYTE*)&gxMockupIndex, 4);
			RegCloseKey(key);
		}
		if (RegCreateKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\moddable.tech\\Screen Test\\window", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
			WINDOWPLACEMENT placement;
			placement.length = sizeof(WINDOWPLACEMENT);
			GetWindowPlacement(window, &placement);
			RegSetValueEx(key, "show", 0, REG_DWORD, (const BYTE*)&placement.showCmd, 4);
			RegSetValueEx(key, "xMin", 0, REG_DWORD, (const BYTE*)&placement.ptMinPosition.x, 4);
			RegSetValueEx(key, "yMin", 0, REG_DWORD, (const BYTE*)&placement.ptMinPosition.y, 4);
			RegSetValueEx(key, "xMax", 0, REG_DWORD, (const BYTE*)&placement.ptMaxPosition.x, 4);
			RegSetValueEx(key, "yMax", 0, REG_DWORD, (const BYTE*)&placement.ptMaxPosition.y, 4);
			RegSetValueEx(key, "left", 0, REG_DWORD, (const BYTE*)&placement.rcNormalPosition.left, 4);
			RegSetValueEx(key, "right", 0, REG_DWORD, (const BYTE*)&placement.rcNormalPosition.right, 4);
			RegSetValueEx(key, "top", 0, REG_DWORD, (const BYTE*)&placement.rcNormalPosition.top, 4);
			RegSetValueEx(key, "bottom", 0, REG_DWORD, (const BYTE*)&placement.rcNormalPosition.bottom, 4);
			RegCloseKey(key);
		}
		PostQuitMessage(0);
		} break;
	case WM_COPYDATA: {
		COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
		SendMessage(window, WM_OPEN_FILE, 0, (LPARAM)cds->lpData);
		} break;
	case WM_CREATE: {
		HKEY key;
		DWORD size = 4;
		
		CreateWindowEx(0, "ScreenView", NULL, WS_CHILD, 0, 0, 0, 0, window, NULL, gInstance, NULL);
		
		gxMockupIndex = -1;
		if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\moddable.tech\\Screen Test\\screen", 0, KEY_READ, &key) == ERROR_SUCCESS) {
			RegQueryValueEx(key, "index", 0, NULL, (BYTE*)&gxMockupIndex, &size);
			RegCloseKey(key);
		}
		if ((gxMockupIndex < 0) || (gxMockupCount <= gxMockupIndex))
			gxMockupIndex = 4;
		
		SendMessage(window, WM_CREATE_SCREEN, 0, 0);
		
		if (RegOpenKeyEx(HKEY_CURRENT_USER, "SOFTWARE\\moddable.tech\\Screen Test\\window", 0, KEY_READ, &key) == ERROR_SUCCESS) {
			WINDOWPLACEMENT placement;
			placement.length = sizeof(WINDOWPLACEMENT);
			placement.flags = 0;
			RegQueryValueEx(key, "show", 0, NULL, (BYTE*)&placement.showCmd, &size);
			RegQueryValueEx(key, "xMin", 0, NULL, (BYTE*)&placement.ptMinPosition.x, &size);
			RegQueryValueEx(key, "yMin", 0, NULL, (BYTE*)&placement.ptMinPosition.y, &size);
			RegQueryValueEx(key, "xMax", 0, NULL, (BYTE*)&placement.ptMaxPosition.x, &size);
			RegQueryValueEx(key, "yMax", 0, NULL, (BYTE*)&placement.ptMaxPosition.y, &size);
			RegQueryValueEx(key, "left", 0, NULL, (BYTE*)&placement.rcNormalPosition.left, &size);
			RegQueryValueEx(key, "right", 0, NULL, (BYTE*)&placement.rcNormalPosition.right, &size);
			RegQueryValueEx(key, "top", 0, NULL, (BYTE*)&placement.rcNormalPosition.top, &size);
			RegQueryValueEx(key, "bottom", 0, NULL, (BYTE*)&placement.rcNormalPosition.bottom, &size);
			SetWindowPlacement(window, &placement);
			RegCloseKey(key);
		}
		} break;
	case WM_CREATE_SCREEN: {
		HWND view = GetWindow(window, GW_CHILD);
		txMockup* mockup = gxMockups[gxMockupIndex]; 
		gxScreen = (txScreen*)malloc(sizeof(txScreen) - 1 + (mockup->width * mockup->height * screenBytesPerPixel));
		memset(gxScreen, 0, sizeof(txScreen) - 1 + (mockup->width * mockup->height * screenBytesPerPixel));
		gxScreen->view = view;
		gxScreen->abort = fxScreenAbort;
		gxScreen->bufferChanged = fxScreenBufferChanged;
		gxScreen->formatChanged = fxScreenFormatChanged;
		gxScreen->start = fxScreenStart;
		gxScreen->stop = fxScreenStop;
		gxScreen->width = mockup->width;
		gxScreen->height = mockup->height;

    	gxScreenBitmap = new Bitmap(gxScreen->width, gxScreen->height, 4 * gxScreen->width, PixelFormat32bppRGB, gxScreen->buffer);

		InvalidateRect(window, NULL, TRUE);
		SendMessage(window, WM_SIZE, SIZE_RESTORED, 0);
		} break;
	case WM_DELETE_SCREEN: {
		delete gxScreenBitmap;
		gxScreenBitmap = NULL;
		free(gxScreen);
		gxScreen = NULL;
		} break;
	case WM_COMMAND: {
		UINT id = LOWORD(wParam);
		switch (id >> 8) {
		case 1:
			switch (id & 0x00FF) {
			case 0: {
				char szFile[MAX_PATH];
				OPENFILENAME ofn;
				ZeroMemory(&ofn, sizeof(ofn));
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = window;
				ofn.lpstrFile = szFile;
				ofn.lpstrFile[0] = '\0';
				ofn.nMaxFile = sizeof(szFile);
				ofn.lpstrFilter = "All\0*.*\0Apps\0*.dll\0Mods\0*.xsa\0";
				ofn.nFilterIndex = 1;
				ofn.lpstrFileTitle = NULL;
				ofn.nMaxFileTitle = 0;
				ofn.lpstrInitialDir = NULL;
				ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
				if (GetOpenFileName(&ofn)==TRUE) {
					SendMessage(window, WM_OPEN_FILE, 0, (LPARAM)szFile);
				}
				} break;
			case 1: {
				HWND view = GetWindow(window, GW_CHILD);
				SendMessage(view, WM_QUIT_MACHINE, 0, 0);
				} break;
			case 2:
				SendMessage(window, WM_CLOSE, 0, 0);
				gxArchiveName[0] = 0;
				gxLibraryName[0] = 0;
				break;
			}
			break;
		case 2:
			id &= 0x00FF;
			if (gxMockupIndex != id) {
				HWND view = GetWindow(window, GW_CHILD);
				SendMessage(view, WM_QUIT_MACHINE, 0, 0);
				SendMessage(window, WM_DELETE_SCREEN, 0, 0);
				gxMockupIndex = id;
				SendMessage(window, WM_CREATE_SCREEN, 0, 0);
				SendMessage(view, WM_LAUNCH_MACHINE, 0, 0);
			}
			break;
		case 3:
			switch (id & 0x00FF) {
			case 1: {
				SHELLEXECUTEINFOW shellExecuteInfo;
				memset(&shellExecuteInfo, 0, sizeof(shellExecuteInfo));
				shellExecuteInfo.cbSize = sizeof(shellExecuteInfo);
				shellExecuteInfo.fMask = 0;
				shellExecuteInfo.lpVerb = L"open";
				shellExecuteInfo.lpFile = L"http://moddable.tech";
				shellExecuteInfo.hwnd = GetDesktopWindow();
				shellExecuteInfo.nShow = SW_SHOWNORMAL;
				ShellExecuteExW(&shellExecuteInfo);
				} break;
			case 2: {
				MSGBOXPARAMSW params;
				memset(&params, 0, sizeof(params));
				params.cbSize = sizeof(params);
				params.hwndOwner = window;
				params.hInstance = gInstance;
				params.dwLanguageId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
				params.lpszCaption = L"About";
				params.dwStyle = MB_USERICON;
				params.lpszIcon = (LPCWSTR)MAKEINTRESOURCE(100);
				params.lpszText = L"Screen Test\n\nCopyright 2017-2022 Moddable Tech, Inc.\nAll rights reserved.\n\nThis application incorporates open source software from Marvell, Inc. and others.";
				MessageBoxIndirectW(&params);
				} break;
			}
			break;
		}
		} break;
	case WM_INITMENUPOPUP: {
		HMENU menu = (HMENU)wParam;
		int c = GetMenuItemCount(menu), i;
		for (i = 0; i < c; i++) {
			UINT id = GetMenuItemID(menu, i);
			switch (id >> 8) {
			case 1:
				switch (id & 0x00FF) {
				case 0:
					EnableMenuItem(menu, i, MF_BYPOSITION | MF_ENABLED);
					break;
				case 1:
					EnableMenuItem(menu, i, MF_BYPOSITION | ((gxLibrary) ? MF_ENABLED : MF_GRAYED));
					break;
				case 2:
					EnableMenuItem(menu, i, MF_BYPOSITION | MF_ENABLED);
					break;
				}
				break;
			case 2:
				EnableMenuItem(menu, i, MF_BYPOSITION | MF_ENABLED);
				CheckMenuItem(menu, i, MF_BYPOSITION | ((gxMockupIndex == i) ? MF_CHECKED : MF_UNCHECKED));
				break;
			case 3:
				switch (id & 0x00FF) {
				case 0:
					EnableMenuItem(menu, i, MF_BYPOSITION | MF_ENABLED);
					break;
				case 1:
					EnableMenuItem(menu, i, MF_BYPOSITION | MF_ENABLED);
					break;
				}
				break;
			}
		}
		} break;
	case WM_OPEN_FILE: {
		char* path = (char*)lParam;
		HWND view = GetWindow(window, GW_CHILD);
		BOOL launch = FALSE;
		SendMessage(view, WM_QUIT_MACHINE, 0, 0);
		if (path) {
			char* slash = strrchr(path, '\\');
			if (slash) {
				char* dot = strrchr(slash, '.');
				if (dot) {
					char* begin;
					char* end;
					if (!strcmp(dot, ".dll")) {
						if (CopyFile(path, gxLibraryPath, FALSE)) {
							end = strrchr(path, '\\');
							*end = 0;
							begin = strrchr(path, '\\');
							strcpy(gxLibraryName, begin + 1);
							*end = '/';
							launch = TRUE;
						}
					}
					else if (!strcmp(dot, ".xsa")) {
						if (CopyFile(path, gxArchivePath, FALSE)) {
							end = strrchr(path, '\\');
							*end = 0;
							begin = strrchr(path, '\\');
							strcpy(gxArchiveName, begin + 1);
							*end = '/';
							launch = TRUE;
						}
					}
					
				}
			}
		}
		if (launch) {
			char* slash;
			int i;
			for (i = 0; i < 3; i++) {
				slash = strrchr(path, '\\');
				if (!slash)
					break;
				*slash = 0;
			}
			if (slash) {
				slash++;
				for (i = 0; i < gxMockupCount; i++) {
					if (!strcmp(gxMockups[i]->name, slash)) {
						if (gxMockupIndex != i) {
							SendMessage(window, WM_DELETE_SCREEN, 0, 0);
							gxMockupIndex = i;
							SendMessage(window, WM_CREATE_SCREEN, 0, 0);
						}
					}
				}
			}
			SendMessage(view, WM_LAUNCH_MACHINE, 0, 0);
		}
		} break;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(window, &ps);
		Graphics* graphics = new Graphics(hdc);
		RECT area;
		GetClientRect(window, &area);
		txMockup* mockup = gxMockups[gxMockupIndex];
		Image* image = mockup->image;
		long x = (area.right - (long)image->GetWidth()) >> 1;
		long y = (area.bottom - (long)image->GetHeight()) >> 1;
		graphics->DrawImage(image, x, y);
		delete graphics;
		EndPaint(window, &ps);
		return TRUE;
		} break;
	case WM_SIZE: {
		if (wParam != SIZE_MINIMIZED) {
			RECT area;
			GetClientRect(window, &area);
			txMockup* mockup = gxMockups[gxMockupIndex];
			Image* image = mockup->image;
			HWND view = GetWindow(window, GW_CHILD);
			long x = ((area.right - (long)image->GetWidth()) >> 1) + mockup->x;
			long y = ((area.bottom - (long)image->GetHeight()) >> 1) + mockup->y;
			SetWindowPos(view, NULL, x, y, mockup->width, mockup->height, SWP_SHOWWINDOW);
		}
		} break;
	default:
		return DefWindowProc(window, message, wParam, lParam);
	}
	return 0;
}  

LRESULT CALLBACK fxScreenViewProc(HWND view, UINT message, WPARAM wParam, LPARAM lParam)
{
	static BOOL dragging = FALSE;
	switch(message)	{
	case WM_LAUNCH_MACHINE: {
		if (gxLibraryName[0]) {
			gxLibrary = LoadLibrary(gxLibraryPath);
			if (!gxLibrary) goto error;
			txScreenLaunchProc launch = (txScreenLaunchProc)GetProcAddress(gxLibrary, "fxScreenLaunch");
			if (!launch) goto error;
			if (gxArchiveName[0]) {
				DWORD size;
				gxArchiveFile = CreateFile(gxArchivePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (gxArchiveFile == INVALID_HANDLE_VALUE) goto error;
				size = GetFileSize(gxArchiveFile, &size);
				if (size == INVALID_FILE_SIZE) goto error;
				gxArchiveMapping = CreateFileMapping(gxArchiveFile, NULL, PAGE_READWRITE, 0, (SIZE_T)size, NULL);
				if (gxArchiveMapping == INVALID_HANDLE_VALUE) goto error;
				gxScreen->archive = MapViewOfFile(gxArchiveMapping, FILE_MAP_WRITE, 0, 0, (SIZE_T)size);
				if (gxScreen->archive == NULL) goto error;
			}
			(*launch)(gxScreen);
		}
		return TRUE;
	error:
		DWORD code = GetLastError();
		char buffer[1024];
		if (gxLibrary) {
			FreeLibrary(gxLibrary);
			gxLibrary = NULL;
		}
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK, NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, sizeof(buffer), NULL);
		MessageBox(GetParent(view), buffer, NULL, MB_OK);
		} break;
	case WM_LBUTTONDOWN: {
		dragging = TRUE;
		if (gxScreen && gxScreen->touch) 
			(*gxScreen->touch)(gxScreen, touchEventBeganKind, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0);
		} break;
	case WM_LBUTTONUP: {
		dragging = FALSE;
		if (gxScreen && gxScreen->touch) 
			(*gxScreen->touch)(gxScreen, touchEventEndedKind, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0);
		} break;
	case WM_MOUSEMOVE: {
		if (dragging) {
			if (gxScreen && gxScreen->touch) 
				(*gxScreen->touch)(gxScreen, touchEventMovedKind, 0, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0);
		}
		} break;
	case WM_ERASEBKGND:
		return TRUE;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(view, &ps);
		Graphics graphics(hdc);
   		graphics.SetCompositingQuality(CompositingQualityHighQuality);
		graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
		graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
  		graphics.DrawImage(gxScreenBitmap, PointF(0.0f, 0.0f));
		EndPaint(view, &ps);
		return TRUE;
		} break;
	case WM_QUIT_MACHINE: {
		if (gxScreen && gxScreen->quit) 
			(*gxScreen->quit)(gxScreen);
		if (gxScreen->archive) {
			UnmapViewOfFile(gxScreen->archive);
			gxScreen->archive = NULL;
		}
		if (gxArchiveMapping) {
			CloseHandle(gxArchiveMapping);
			gxArchiveMapping = INVALID_HANDLE_VALUE;
		}
		if (gxArchiveFile) {
			CloseHandle(gxArchiveFile);
			gxArchiveFile = INVALID_HANDLE_VALUE;
		}
		if (gxLibrary) {
			FreeLibrary(gxLibrary);
			gxLibrary = NULL;
		}
		InvalidateRect(view, NULL, TRUE);
		SetWindowText(GetParent(view), "Screen Test");
		if (wParam) {
			PWSTR reasons[9] = {
				L"",
				L"XS abort: memory full!",
				L"XS abort: stack overflow!",
				L"XS abort: fatal check!",
				L"XS abort: dead strip!",
				L"XS abort: unhandled exception!",
				L"XS abort: not enough keys!",
				L"XS abort: too much computation!",
				L"XS abort: unhandled rejection!",
			};
			MSGBOXPARAMSW params;
			memset(&params, 0, sizeof(params));
			params.cbSize = sizeof(params);
			params.hwndOwner = GetParent(view);
			params.hInstance = gInstance;
			params.dwLanguageId = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
			params.dwStyle = MB_ICONSTOP;
			params.lpszText = reasons[wParam];
			MessageBoxIndirectW(&params);
		}
		} break;
	case WM_TIMER: {
		if (gxScreen && gxScreen->idle) 
			(*gxScreen->idle)(gxScreen);
		} break;
	default:
		return DefWindowProc(view, message, wParam, lParam);
	}
	return 0;
}
