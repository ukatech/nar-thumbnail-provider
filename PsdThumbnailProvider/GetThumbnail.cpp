#include "GetThumbnail.h"
#include "gdiplus.h"
#include <Shlwapi.h>
#include <math.h>

#pragma comment(lib, "Shlwapi.lib")

using namespace Gdiplus;

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

SHORT ReadInt16(IStream *pStream) {
	BYTE b[2];
	ReadData(pStream, b, 2);
	return b[0] << 8 | b[1];
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

UINT Tell(IStream *pStream) {
	LARGE_INTEGER pos;
	ULARGE_INTEGER newPos;
	pos.QuadPart = 0;
	pStream->Seek(pos, STREAM_SEEK_CUR, &newPos);
	return (UINT)newPos.QuadPart;
}

HBITMAP GetPSDThumbnail(IStream* stream) {
	HBITMAP result = NULL;
	UINT signature = ReadUInt32(stream);
	USHORT version = ReadUInt16(stream);

	if (signature != 0x38425053 || version != 1)
		return NULL;

	Seek(stream, 6, STREAM_SEEK_CUR);

	USHORT channels = ReadUInt16(stream);
	int height = ReadUInt32(stream);
	int width = ReadUInt32(stream);
	USHORT bitsPerChannel = ReadUInt16(stream);
	USHORT colorMode = ReadUInt16(stream);
	UINT length = ReadUInt32(stream);

	if (length > 0) {
		Seek(stream, length, STREAM_SEEK_CUR);
	}

	UINT resourcesLength = ReadUInt32(stream);
	UINT reasourceOffset = Tell(stream);
	UINT thumbnailOffset = 0;
	UINT resourceAdvanced = 0;

	while (resourcesLength > resourceAdvanced) {
		Seek(stream, 4, STREAM_SEEK_CUR);

		USHORT id = ReadUInt16(stream);
		BYTE strl = ReadByte(stream);

		Seek(stream, strl % 2 == 0 ? strl + 1 : strl, STREAM_SEEK_CUR);

		length = ReadUInt32(stream);

		if (id == 1036) {
			thumbnailOffset = Tell(stream);
			break;
		}

		Seek(stream, length % 2 ? length + 1 : length, STREAM_SEEK_CUR);

		resourceAdvanced += 6 + 1 + (strl % 2 == 0 ? strl + 1 : strl) +
			4 + (length % 2 ? length + 1 : length);
	}

	// read composite image
	if (colorMode == 3 && ((width <= 256 && height <= 256) || !thumbnailOffset)) {
		Seek(stream, reasourceOffset + resourcesLength, STREAM_SEEK_SET);

		UINT layerAndMaskInfoLength = ReadUInt32(stream);
		UINT layerAndMastInfoOffset = Tell(stream);

		UINT layerInfoLength = ReadUInt32(stream);
		SHORT layerCount = ReadInt16(stream);
		bool globalAlpha = layerCount < 0;

		Seek(stream, layerAndMastInfoOffset + layerAndMaskInfoLength, STREAM_SEEK_SET);

		USHORT compression = ReadUInt16(stream);

		if (compression == 1) {
			int channelCount = 3;
			int offsets[16] = { 2, 1, 0 };

			if (channels && channels > 3) {
				for (int i = 3; i < channels; i++) {
					offsets[i] = i;
					channelCount++;
				}
			}
			else if (globalAlpha) {
				offsets[3] = 3;
				channelCount++;
			}

			USHORT* lengths = new USHORT[channelCount * height];
			int dataLength = width * height * 4;
			int step = 4;
			int maxLength = 0;
			BYTE* data = new BYTE[dataLength];

			for (int i = 0; i < dataLength; i++) {
				data[i] = 0xff;
			}

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
				}
				else {
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
							}
							else { // header < 128
								for (int j = 0; j <= header; j++) {
									data[p] = buffer[++i];
									p += step;
								}
							}
						}
					}
				}
			}

			if (width > 256 || height > 256) {
				HBITMAP fullBitmap = CreateBitmap(width, height, 1, 32, data);
				int thumbWidth, thumbHeight;

				if (width > height) {
					thumbWidth = 256;
					thumbHeight = height * thumbWidth / width;
				}
				else {
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
			}
			else {
				result = CreateBitmap(width, height, 1, 32, data);
			}

			delete lengths;
			delete buffer;
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

			BYTE *data = new BYTE[length];
			ReadData(stream, data, length);
			IStream *memory = SHCreateMemStream(data, length);

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
