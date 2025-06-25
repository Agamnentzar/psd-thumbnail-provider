#include <windows.h>
#include "../PsdThumbnailProvider/GetThumbnail.cpp"

static HBITMAP bitmap = NULL;

LRESULT CALLBACK windowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	LRESULT result = 0;

	switch (message) {
	case WM_CREATE: {
		IStream* stream;
		SHCreateStreamOnFileEx(L"test7.psb", STGM_READ, FILE_ATTRIBUTE_NORMAL, false, NULL, &stream);
		bitmap = GetPSDThumbnail(stream);
		BITMAP bm = {};
		GetObject(bitmap, sizeof(bm), &bm);
		RECT rc, rcClient;
		GetWindowRect(hWnd, &rc);
		GetClientRect(hWnd, &rcClient);
		int xExtra = rc.right - rc.left - rcClient.right;
		int yExtra = rc.bottom - rc.top - rcClient.bottom;
		SetWindowPos(hWnd, NULL, 0, 0, bm.bmWidth + xExtra, bm.bmHeight + yExtra, SWP_NOMOVE);
		break;
	}
	case WM_ACTIVATEAPP:
		break;
	case WM_SIZE:
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_CLOSE:
		PostQuitMessage(0);
		break;
	case WM_PAINT: {
		RECT rect;
		GetWindowRect(hWnd, &rect);
		rect.right -= rect.left;
		rect.bottom -= rect.top;
		rect.left = 0;
		rect.top = 0;
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		HDC hdcMem = CreateCompatibleDC(hdc);
		HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
		FillRect(hdc, &rect, bgBrush);
		BITMAP bm = {};
		GetObject(bitmap, sizeof(BITMAP), &bm);
		SelectObject(hdcMem, bitmap);
		BitBlt(hdc, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);
		DeleteObject(bgBrush);
		DeleteDC(hdcMem);
		EndPaint(hWnd, &ps);
		break;
	}
	default:
		result = DefWindowProc(hWnd, message, wParam, lParam);
		break;
	}

	return result;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
	WNDCLASS windowClass = {};
	windowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = windowProc;
	windowClass.hInstance = hInstance;
	windowClass.hIcon = 0;  // TODO
	windowClass.lpszClassName = TEXT("aggie-window-class");

	RegisterClass(&windowClass);

	HWND mainWindow = CreateWindowEx(
		0,  // WS_EX_ACCEPTFILES
		windowClass.lpszClassName,
		TEXT("PSD Preview Test"),
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		hInstance,
		NULL);

	while (true) {
		MSG message;

		while (PeekMessage(&message, 0, 0, 0, PM_REMOVE)) {
			if (message.message == WM_QUIT) {
				return 0;
			}

			TranslateMessage(&message);
			DispatchMessage(&message);
		}

		Sleep(1);
	}

	return 0;
}
