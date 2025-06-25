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

	// color mode data
	auto colorModeLength = ReadSectionLength(stream);
	Seek(stream, colorModeLength, STREAM_SEEK_CUR);

	// image resources
	auto resourcesLength = ReadSectionLength(stream);
	auto reasourceOffset = Tell(stream);
	ULONGLONG resourceEnd = reasourceOffset + resourcesLength;
	LONGLONG thumbnailOffset = 0;
	ULONG thumbnailLength = 0;

	while (Tell(stream) < resourceEnd) {
		Seek(stream, 4, STREAM_SEEK_CUR); // signature

		USHORT id = ReadUInt16(stream);

		BYTE nameLength = ReadByte(stream); // name
		Seek(stream, ((nameLength + 1) % 2) ? nameLength + 1 : nameLength, STREAM_SEEK_CUR);

		auto length = ReadSectionLength(stream);

		if (id == 1036) {
			thumbnailOffset = Tell(stream);
			thumbnailLength = (ULONG)length;
			break;
		}

		Seek(stream, length, STREAM_SEEK_CUR);
		if (Tell(stream) % 2) Seek(stream, 1, STREAM_SEEK_CUR);
	}

	// read composite image
	if (false && colorMode == 3 && ((width <= 256 && height <= 256) || !thumbnailOffset)) {
		Seek(stream, reasourceOffset + resourcesLength, STREAM_SEEK_SET);

		auto layerAndMaskInfoLength = ReadSectionLength(stream);
		auto layerAndMastInfoOffset = Tell(stream);

		auto layerInfoLength = ReadSectionLength(stream);
		SHORT layerCount = ReadInt16(stream);
		bool globalAlpha = layerCount < 0;

		Seek(stream, layerAndMastInfoOffset + layerAndMaskInfoLength, STREAM_SEEK_SET);

		USHORT compression = ReadUInt16(stream);

		int channelCount = 3;
		int offsets[16] = { 2, 1, 0 };

		if (channels && channels > 3) {
			for (int i = 3; i < channels; i++) {
				offsets[i] = i;
				channelCount++;
			}
		} else if (globalAlpha) {
			offsets[3] = 3;
			channelCount++;
		}

		if (compression == 1 || compression == 0) {
			int dataLength = width * height * 4;
			BYTE* data = new BYTE[dataLength];

			for (int i = 0; i < dataLength; i++) {
				data[i] = 0xff;
			}

			if (compression == 1) {
				USHORT* lengths = new USHORT[channelCount * height];
				int step = 4;
				int maxLength = 0;

				for (int o = 0, li = 0; o < channelCount; o++) {
					for (int y = 0; y < height; y++, li++) {
						lengths[li] = ReadUInt16(stream);
						maxLength = maxLength < lengths[li] ? lengths[li] : maxLength;
					}
				}

				BYTE* buffer = new BYTE[maxLength];

				for (int c = 0, li = 0; c < channelCount; c++) {
					int offset = offsets[c];
					int extra = c > 3 || offset > 3;

					if (extra) {
						for (int y = 0; y < height; y++, li++) {
							Seek(stream, lengths[li], STREAM_SEEK_CUR);
						}
					} else {
						for (int y = 0, p = offset; y < height; y++, li++) {
							int length = lengths[li];
							ReadData(stream, buffer, length);

							for (int i = 0; i < length; i++) {
								BYTE header = buffer[i];

								if (header >= 128) {
									BYTE value = buffer[++i];
									header = (256 - header);

									for (int j = 0; j <= header; j++) {
										data[p] = value;
										p += step;
									}
								} else { // header < 128
									for (int j = 0; j <= header; j++) {
										data[p] = buffer[++i];
										p += step;
									}
								}
							}
						}
					}
				}
				delete lengths;
				delete buffer;
			} else {
				int pixels = width * height;
				BYTE* buffer = new BYTE[pixels];
				if (channelCount > 4) channelCount = 4;

				for (int c = 0; c < channelCount; c++) {
					int o = offsets[c];
					ReadData(stream, buffer, pixels);

					for (int i = 0; i < pixels; i++) {
						data[i * 4 + o] = buffer[i];
					}
				}

				delete buffer;
			}

			bool allWhite = true;

			for (int i = 0; i < dataLength; i++) {
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

					BLENDFUNCTION fnc;
					fnc.BlendOp = AC_SRC_OVER;
					fnc.BlendFlags = 0;
					fnc.SourceConstantAlpha = 0xFF;
					fnc.AlphaFormat = AC_SRC_ALPHA;

					HDC dc = GetDC(NULL);
					HDC srcDC = CreateCompatibleDC(dc);
					HDC memDC = CreateCompatibleDC(dc);
					result = CreateCompatibleBitmap(dc, thumbWidth, thumbHeight);
					SelectObject(memDC, result);
					SelectObject(srcDC, fullBitmap);

					RECT rect = {};
					rect.right = thumbWidth;
					rect.bottom = thumbHeight;
					FillRect(memDC, &rect, (HBRUSH)GetStockObject(NULL_BRUSH));
					AlphaBlend(memDC, 0, 0, thumbWidth, thumbHeight, srcDC, 0, 0, width, height, fnc);

					DeleteObject(fullBitmap);
					DeleteDC(srcDC);
					DeleteDC(memDC);
					ReleaseDC(NULL, dc);
				} else {
					result = CreateBitmap(width, height, 1, 32, data);
				}
			}

			delete data;
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
