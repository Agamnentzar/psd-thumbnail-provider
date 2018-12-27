#include "PsdThumbnailProvider.h"
#include "GetThumbnail.h"
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

extern HINSTANCE g_hInst;
extern long g_cDllRef;

PsdThumbnailProvider::PsdThumbnailProvider() : m_cRef(1), m_pStream(NULL) {
	InterlockedIncrement(&g_cDllRef);
}

PsdThumbnailProvider::~PsdThumbnailProvider() {
	InterlockedDecrement(&g_cDllRef);
}

// IUnknown

IFACEMETHODIMP PsdThumbnailProvider::QueryInterface(REFIID riid, void **ppv) {
	static const QITAB qit[] =
	{
		QITABENT(PsdThumbnailProvider, IThumbnailProvider),
		QITABENT(PsdThumbnailProvider, IInitializeWithStream),
		{ 0 },
	};
	return QISearch(this, qit, riid, ppv);
}

IFACEMETHODIMP_(ULONG) PsdThumbnailProvider::AddRef() {
	return InterlockedIncrement(&m_cRef);
}

IFACEMETHODIMP_(ULONG) PsdThumbnailProvider::Release() {
	ULONG cRef = InterlockedDecrement(&m_cRef);

	if (0 == cRef)
		delete this;

	return cRef;
}

// IInitializeWithStream

IFACEMETHODIMP PsdThumbnailProvider::Initialize(IStream *pStream, DWORD grfMode) {
	HRESULT hr = HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);

	if (m_pStream == NULL)
		hr = pStream->QueryInterface(&m_pStream);

	return hr;
}

// IThumbnailProvider

IFACEMETHODIMP PsdThumbnailProvider::GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha) {
	*pdwAlpha = WTSAT_ARGB;
	*phbmp = GetPSDThumbnail(m_pStream);

	// m_pStream->Release();

	return *phbmp != NULL ? NOERROR : E_NOTIMPL;
}
