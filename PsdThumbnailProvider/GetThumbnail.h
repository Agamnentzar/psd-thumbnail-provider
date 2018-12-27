#pragma once

#include <windows.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

HBITMAP GetPSDThumbnail(IStream* stream);
