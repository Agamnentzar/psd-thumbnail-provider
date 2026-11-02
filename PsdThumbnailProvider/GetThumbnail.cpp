#include "GetThumbnail.h"
#include "gdiplus.h"
#include <Shlwapi.h>
#include <math.h>
#include <objidl.h> // Gdiplus
#include <gdiplus.h> // Gdiplus

#pragma comment(lib, "Shlwapi.lib")
#pragma comment (lib, "Gdiplus.lib") // Gdiplus
#pragma comment (lib, "Msimg32.lib") // AlphaBlend

using namespace Gdiplus;

inline void ReadData(IStream *pStream, BYTE *data, ULONG length) {
	ULONG read, total = 0;
	HRESULT hr;

	do {
		hr = pStream->Read(data + total, length - total, &read);
		total += read;
	} while (total < length && hr == S_OK);
}

inline UINT ReadUInt32(IStream *pStream) {
	BYTE b[4];
	ReadData(pStream, b, 4);
	return b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
}

inline ULONG64 ReadUInt64(IStream *pStream) {
	BYTE b[8];
	ReadData(pStream, b, 8);
	return (ULONG64)b[0] << 56 | (ULONG64)b[1] << 48 | (ULONG64)b[2] << 40 | (ULONG64)b[3] << 32 | (ULONG64)b[4] << 24 | (ULONG64)b[5] << 16 | (ULONG64)b[6] << 8 | (ULONG64)b[7];
}

inline SHORT ReadInt16(IStream *pStream) {
	BYTE b[2];
	ReadData(pStream, b, 2);
	return b[0] << 8 | b[1];
}

inline USHORT ReadUInt16(IStream *pStream) {
	BYTE b[2];
	ReadData(pStream, b, 2);
	return b[0] << 8 | b[1];
}

inline BYTE ReadByte(IStream *pStream) {
	BYTE b;
	ReadData(pStream, &b, 1);
	return b;
}

inline LONGLONG ReadSectionLength(IStream *pStream, int version = 1) {
	if (version == 1) {
		return (LONGLONG)ReadUInt32(pStream);
	} else {
		return (LONGLONG)ReadUInt64(pStream);
	}
}

inline void Seek(IStream *pStream, LONGLONG offset, DWORD origin) {
	if (offset > 0) {
		LARGE_INTEGER pos;
		pos.QuadPart = offset;
		pStream->Seek(pos, origin, NULL);
	}
}

inline ULONGLONG Tell(IStream *pStream) {
	LARGE_INTEGER pos;
	ULARGE_INTEGER newPos;
	pos.QuadPart = 0;
	pStream->Seek(pos, STREAM_SEEK_CUR, &newPos);
	return newPos.QuadPart;
}

// decodes PackBits compressed row into dst, stops after count bytes
static void DecodeRLERow(const BYTE *src, ULONG length, BYTE *dst, int count) {
	ULONG i = 0;
	int x = 0;

	while (i < length && x < count) {
		BYTE header = src[i++];

		if (header > 128) {
			if (i >= length) break;

			BYTE value = src[i++];
			int n = min(257 - header, count - x);
			memset(dst + x, value, n);
			x += n;
		} else if (header < 128) {
			int n = min((int)min((ULONG)header + 1, length - i), count - x);
			memcpy(dst + x, src + i, n);
			x += n;
			i += n;
		} // 128 is no-op
	}
}

// reads first `count` of the composite image channels (out of `channels` stored in the file), values of each channel
// are written to data with `step` starting at its offset, 16-bit values are reduced to 8-bit by taking the high byte
static void ReadCompositeChannels(IStream *stream, USHORT compression, BYTE *data, int width, int height, int bytesPerChannel, int version, int channels, const int *offsets, int count, int step) {
	int rowSize = width * bytesPerChannel;
	BYTE* row = new BYTE[rowSize];

	if (compression == 1) {
		ULONG* lengths = new ULONG[channels * height];
		ULONG maxLength = 0;

		for (int i = 0; i < channels * height; i++) {
			lengths[i] = version == 1 ? ReadUInt16(stream) : ReadUInt32(stream);
			maxLength = max(maxLength, lengths[i]);
		}

		BYTE* buffer = new BYTE[maxLength];

		for (int c = 0, li = 0; c < count; c++) {
			for (int y = 0; y < height; y++, li++) {
				ReadData(stream, buffer, lengths[li]);
				memset(row, 0, rowSize);
				DecodeRLERow(buffer, lengths[li], row, rowSize);

				BYTE* dst = data + (size_t)y * width * step + offsets[c];

				for (int x = 0; x < width; x++, dst += step) {
					*dst = row[x * bytesPerChannel];
				}
			}
		}

		delete[] buffer;
		delete[] lengths;
	} else {
		for (int c = 0; c < count; c++) {
			for (int y = 0; y < height; y++) {
				ReadData(stream, row, rowSize);

				BYTE* dst = data + (size_t)y * width * step + offsets[c];

				for (int x = 0; x < width; x++, dst += step) {
					*dst = row[x * bytesPerChannel];
				}
			}
		}
	}

	delete[] row;
}

inline BYTE ClampToByte(double value) {
	value = round(value);
	return value <= 0 ? 0 : (value >= 255 ? 255 : (BYTE)value);
}

// converts CMYK values (0 = 100% ink) to BGR, using approximation of U.S. Web Coated (SWOP) v2 profile
inline void CmykToBgr(const BYTE *cmyk, BYTE *bgr) {
	double c = 1 - cmyk[0] / 255.0;
	double m = 1 - cmyk[1] / 255.0;
	double y = 1 - cmyk[2] / 255.0;
	double k = 1 - cmyk[3] / 255.0;

	bgr[2] = ClampToByte(255 +
		c * (-4.387332384609988 * c + 54.48615194189176 * m + 18.82290502165302 * y + 212.25662451639585 * k - 285.2331026137004) +
		m * (1.7149763477362134 * m - 5.6096736904047315 * y - 17.873870861415444 * k - 5.497006427196366) +
		y * (-2.5217340131683033 * y - 21.248923337353073 * k + 17.5119270841813) +
		k * (-21.86122147463605 * k - 189.48180835922747));
	bgr[1] = ClampToByte(255 +
		c * (8.841041422036149 * c + 60.118027045597366 * m + 6.871425592049007 * y + 31.159100130055922 * k - 79.2970844816548) +
		m * (-15.310361306967817 * m + 17.575251261109482 * y + 131.35250912493976 * k - 190.9453302588951) +
		y * (4.444339102852739 * y + 9.8632861493405 * k - 24.86741582555878) +
		k * (-20.737325471181034 * k - 187.80453709719578));
	bgr[0] = ClampToByte(255 +
		c * (0.8842522430003296 * c + 8.078677503112928 * m + 30.89978309703729 * y - 0.23883238689178934 * k - 14.183576799673286) +
		m * (10.49593273432072 * m + 63.02378494754052 * y + 50.606957656360734 * k - 112.23884253719248) +
		y * (0.03296041114873217 * y + 115.60384449646641 * k - 193.58209356861505) +
		k * (-22.33816807309886 * k - 180.12613974708367));
}

HBITMAP GetPSDThumbnail(IStream* stream) {
	HBITMAP result = NULL;
	UINT signature = ReadUInt32(stream);
	USHORT version = ReadUInt16(stream);

	if (signature != 0x38425053 || (version != 1 && version != 2)) return NULL;

	Seek(stream, 6, STREAM_SEEK_CUR);

	USHORT channels = ReadUInt16(stream);
	int height = ReadUInt32(stream);
	int width = ReadUInt32(stream);
	USHORT bitsPerChannel = ReadUInt16(stream);
	USHORT colorMode = ReadUInt16(stream);
	int maxSize = version == 1 ? 30000 : 300000;
	if (width > maxSize || height > maxSize) return NULL;

	// color mode data (palette of indexed color mode: 256 red values, followed by green and blue values)
	auto colorModeLength = ReadSectionLength(stream);
	BYTE palette[768];
	bool hasPalette = colorMode == 2 && colorModeLength == 768;

	if (hasPalette) {
		ReadData(stream, palette, 768);
	} else {
		Seek(stream, colorModeLength, STREAM_SEEK_CUR);
	}

	// image resources
	auto resourcesLength = ReadSectionLength(stream);
	auto reasourceOffset = Tell(stream);
	ULONGLONG resourceEnd = reasourceOffset + resourcesLength;
	LONGLONG thumbnailOffset = 0;
	ULONG thumbnailLength = 0;
	int transparencyIndex = -1;

	while (Tell(stream) < resourceEnd) {
		Seek(stream, 4, STREAM_SEEK_CUR); // signature

		USHORT id = ReadUInt16(stream);

		BYTE nameLength = ReadByte(stream); // name
		Seek(stream, ((nameLength + 1) % 2) ? nameLength + 1 : nameLength, STREAM_SEEK_CUR);

		auto length = ReadSectionLength(stream);
		auto offset = Tell(stream);

		if (id == 1036) {
			thumbnailOffset = offset;
			thumbnailLength = (ULONG)length;
		} else if (id == 1047 && length >= 2) {
			transparencyIndex = ReadUInt16(stream);
		}

		Seek(stream, offset + length, STREAM_SEEK_SET);
		if (Tell(stream) % 2) Seek(stream, 1, STREAM_SEEK_CUR);
	}

	// read composite image (RGB, CMYK, grayscale or indexed, 8 or 16 bits per channel)
	bool grayscale = colorMode == 1;
	bool indexed = colorMode == 2;
	bool cmyk = colorMode == 4;
	int colorChannels = (grayscale || indexed) ? 1 : (cmyk ? 4 : 3);
	int bytesPerChannel = bitsPerChannel / 8;
	bool supportedMode = colorMode == 3 || cmyk || grayscale || (indexed && hasPalette);
	bool supportedDepth = bitsPerChannel == 8 || (bitsPerChannel == 16 && !indexed);

	if (supportedMode && supportedDepth && channels >= colorChannels && ((width <= 256 && height <= 256) || !thumbnailOffset)) {
		Seek(stream, resourceEnd, STREAM_SEEK_SET);

		auto layerAndMaskInfoLength = ReadSectionLength(stream, version);
		Seek(stream, layerAndMaskInfoLength, STREAM_SEEK_CUR);

		USHORT compression = ReadUInt16(stream);

		if (compression == 1 || compression == 0) {
			size_t pixels = (size_t)width * height;
			size_t dataLength = pixels * 4;
			BYTE* data = new BYTE[dataLength];
			memset(data, 0xff, dataLength);

			// first extra channel is transparency (indexed color mode uses transparency index instead)
			int count = min((int)channels, indexed ? 1 : colorChannels + 1);

			if (indexed) {
				BYTE* indices = new BYTE[pixels];
				int offsets[] = { 0 };
				ReadCompositeChannels(stream, compression, indices, width, height, 1, version, channels, offsets, count, 1);

				for (size_t i = 0; i < pixels; i++) {
					BYTE index = indices[i];
					BYTE* dst = data + i * 4;

					if (index == transparencyIndex) {
						memset(dst, 0, 4);
					} else {
						dst[0] = palette[512 + index];
						dst[1] = palette[256 + index];
						dst[2] = palette[index];
					}
				}

				delete[] indices;
			} else if (cmyk) {
				BYTE* values = new BYTE[pixels * 5];
				memset(values, 0xff, pixels * 5);
				int offsets[] = { 0, 1, 2, 3, 4 };
				ReadCompositeChannels(stream, compression, values, width, height, bytesPerChannel, version, channels, offsets, count, 5);

				for (size_t i = 0; i < pixels; i++) {
					CmykToBgr(values + i * 5, data + i * 4);
					data[i * 4 + 3] = values[i * 5 + 4];
				}

				delete[] values;
			} else if (grayscale) {
				int offsets[] = { 0, 3 };
				ReadCompositeChannels(stream, compression, data, width, height, bytesPerChannel, version, channels, offsets, count, 4);

				for (size_t i = 0; i < pixels; i++) {
					data[i * 4 + 1] = data[i * 4 + 2] = data[i * 4];
				}
			} else {
				int offsets[] = { 2, 1, 0, 3 };
				ReadCompositeChannels(stream, compression, data, width, height, bytesPerChannel, version, channels, offsets, count, 4);
			}

			bool allWhite = true;

			for (size_t i = 0; i < dataLength; i++) {
				if (data[i] != 0xff) {
					allWhite = false;
					break;
				}
			}

			if (!allWhite) {
				if (width > 256 || height > 256) {
					HBITMAP fullBitmap = CreateBitmap(width, height, 1, 32, data);
					int thumbWidth, thumbHeight;

					if (width > height) {
						thumbWidth = 256;
						thumbHeight = height * thumbWidth / width;
					} else {
						thumbHeight = 256;
						thumbWidth = width * thumbHeight / height;
					}

					BITMAPINFO bmi = {};
					bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
					bmi.bmiHeader.biWidth = thumbWidth;
					bmi.bmiHeader.biHeight = -thumbHeight;
					bmi.bmiHeader.biPlanes = 1;
					bmi.bmiHeader.biBitCount = 32;
					bmi.bmiHeader.biCompression = BI_RGB;

					void* pBits = nullptr;
					result = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);

					if (result) {
						HDC srcDC = CreateCompatibleDC(NULL);
						HDC memDC = CreateCompatibleDC(NULL);
						SelectObject(memDC, result);
						SelectObject(srcDC, fullBitmap);

						BLENDFUNCTION fnc;
						fnc.BlendOp = AC_SRC_OVER;
						fnc.BlendFlags = 0;
						fnc.SourceConstantAlpha = 0xFF;
						fnc.AlphaFormat = AC_SRC_ALPHA;

						AlphaBlend(memDC, 0, 0, thumbWidth, thumbHeight, srcDC, 0, 0, width, height, fnc);

						DeleteDC(srcDC);
						DeleteDC(memDC);
					}

					DeleteObject(fullBitmap);
				} else {
					result = CreateBitmap(width, height, 1, 32, data);
				}
			}

			delete[] data;
		}
	}

	// read thumbnail resource
	if (!result && thumbnailOffset) {
		Seek(stream, thumbnailOffset, STREAM_SEEK_SET);
		ULONG_PTR token;
		GdiplusStartupInput input;

		if (Ok == GdiplusStartup(&token, &input, NULL)) {
			Seek(stream, 4 + 4 + 4 + 4 + 4 + 4 + 2 + 2, STREAM_SEEK_CUR);

			BYTE *data = new BYTE[thumbnailLength]; // TODO: should this be freed?
			ReadData(stream, data, thumbnailLength);
			IStream *memory = SHCreateMemStream(data, thumbnailLength);

			if (memory) {
				Seek(memory, 0, STREAM_SEEK_SET);

				Bitmap *bitmap = Bitmap::FromStream(memory);

				if (bitmap) {
					Color color(0, 255, 255, 255);
					bitmap->GetHBITMAP(color, &result);
					delete bitmap;
				}

				memory->Release(); // test
			}
		}

		GdiplusShutdown(token);
	}

	return result;
}
