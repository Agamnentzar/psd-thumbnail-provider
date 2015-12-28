#include "PsdThumbnailProvider.h"
#include "gdiplus.h"
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

using namespace Gdiplus;

extern HINSTANCE g_hInst;
extern long g_cDllRef;

void ReadData(IStream *pStream, BYTE *data, ULONG length) {
	ULONG read, total = 0;
	HRESULT hr;

	do {
		hr = pStream->Read(data + total, length - total, &read);
		total += read;
	} while (total < length && hr == S_OK);
}

UINT ReadUInt32(IStream *pStream) {
	BYTE b[4];
	ReadData(pStream, b, 4);
	return b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
}

USHORT ReadUInt16(IStream *pStream) {
	BYTE b[2];
	ReadData(pStream, b, 2);
	return b[0] << 8 | b[1];
}

BYTE ReadByte(IStream *pStream) {
	BYTE b;
	ReadData(pStream, &b, 1);
	return b;
}

void Seek(IStream *pStream, int offset, DWORD origin) {
	LARGE_INTEGER pos;
	pos.QuadPart = offset;
	pStream->Seek(pos, origin, NULL);
}

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
	*phbmp = NULL;
	*pdwAlpha = WTSAT_ARGB;

	Seek(m_pStream, 4 + 2 + 6 + 2 + 4 + 4 + 2 + 2, STREAM_SEEK_SET);

	UINT length = ReadUInt32(m_pStream);

	if (length > 0)
		Seek(m_pStream, length, STREAM_SEEK_CUR);

	UINT resourcesLength = ReadUInt32(m_pStream);
	UINT resourceAdvanced = 0;

	while (resourcesLength > resourceAdvanced) {
		Seek(m_pStream, 4, STREAM_SEEK_CUR);

		USHORT id = ReadUInt16(m_pStream);
		BYTE strl = ReadByte(m_pStream);

		Seek(m_pStream, strl % 2 == 0 ? strl + 1 : strl, STREAM_SEEK_CUR);

		length = ReadUInt32(m_pStream);

		if (id == 1036) {
			ULONG_PTR token;
			GdiplusStartupInput input;

			if (Ok == GdiplusStartup(&token, &input, NULL)) {
				Seek(m_pStream, 4 + 4 + 4 + 4 + 4 + 4 + 2 + 2, STREAM_SEEK_CUR);

				BYTE *data = new BYTE[length];

				ReadData(m_pStream, data, length);

				IStream *memory = SHCreateMemStream(data, length);

				Seek(memory, 0, STREAM_SEEK_SET);

				Bitmap *bitmap = Bitmap::FromStream(memory);
				Color color(0, 255, 255, 255);
				bitmap->GetHBITMAP(color, phbmp);
				delete bitmap;
			}

			GdiplusShutdown(token);
			break;
		}

		Seek(m_pStream, length % 2 ? length + 1 : length, STREAM_SEEK_CUR);

		resourceAdvanced += 6 + 1 + (strl % 2 == 0 ? strl + 1 : strl) +
			4 + (length % 2 ? length + 1 : length);
	}

	return *phbmp != NULL ? NOERROR : E_NOTIMPL;
}
