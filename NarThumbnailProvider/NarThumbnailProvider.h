#pragma once

#include <windows.h>
#include <thumbcache.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

class NarThumbnailProvider :
	public IInitializeWithStream,
	public IThumbnailProvider {
public:
	// IUnknown
	IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv);
	IFACEMETHODIMP_(ULONG) AddRef();
	IFACEMETHODIMP_(ULONG) Release();

	// IInitializeWithStream
	IFACEMETHODIMP Initialize(IStream *pStream, DWORD grfMode);

	// IThumbnailProvider
	IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha);

	NarThumbnailProvider();

protected:
	~NarThumbnailProvider();

private:
	long m_cRef;
	IStream *m_pStream;
};
