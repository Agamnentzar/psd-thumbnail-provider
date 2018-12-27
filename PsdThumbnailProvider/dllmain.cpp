#include <windows.h>
#include <Guiddef.h>
#include <shlobj.h> // For SHChangeNotify
#include "ClassFactory.h" // For the class factory

// {5BB45D32-AF01-414F-B60A-5E999B986681}
const CLSID CLSID_RecipeThumbnailProvider =
	{ 0x5bb45d32, 0xaf01, 0x414f, { 0xb6, 0xa, 0x5e, 0x99, 0x9b, 0x98, 0x66, 0x81 } };

HINSTANCE g_hInst = NULL;
long g_cDllRef = 0;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
	switch (dwReason) {
	case DLL_PROCESS_ATTACH:
		// Hold the instance of this DLL module, we will use it to get the 
		// path of the DLL to register the component.
		g_hInst = hModule;
		DisableThreadLibraryCalls(hModule);
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) {
	HRESULT hr = CLASS_E_CLASSNOTAVAILABLE;

	if (IsEqualCLSID(CLSID_RecipeThumbnailProvider, rclsid)) {
		hr = E_OUTOFMEMORY;

		ClassFactory *pClassFactory = new ClassFactory();
		if (pClassFactory) {
			hr = pClassFactory->QueryInterface(riid, ppv);
			pClassFactory->Release();
		}
	}

	return hr;
}

STDAPI DllCanUnloadNow(void) {
	return g_cDllRef > 0 ? S_FALSE : S_OK;
}

STDAPI DllRegisterServer(void) {
	return S_OK;
}

STDAPI DllUnregisterServer(void) {
	return S_OK;
}
