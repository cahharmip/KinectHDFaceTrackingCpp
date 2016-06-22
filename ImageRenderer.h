#pragma once

#include <d2d1.h>
#include <Dwrite.h>
#include <DirectXMath.h>
#include "stdafx.h"

class ImageRenderer {
public:
	ImageRenderer();
	virtual ~ImageRenderer();
	HRESULT Initialize(HWND hwnd, ID2D1Factory* pD2DFactory, int sourceWidth, int sourceHeight, int sourceStride);
	HRESULT BeginDrawing();
	HRESULT EndDrawing();
	HRESULT DrawBackground(BYTE* pImage, unsigned long cbImage);
	void DrawFaceFrameResults(int iFace,int x,int y);
private:
	HRESULT EnsureResource();
	void DiscardResource();
	bool ValidateFaceBoxAndPoint(const RectI* pFaceBox,const PointF* pFacePoints);
	HWND m_hwnd;

	UINT m_sourceWidth;
	UINT m_sourceHeight;
	UINT m_sourceStride;

	ID2D1Factory* m_pD2DFactory;
	ID2D1HwndRenderTarget* m_pRenderTarget;
	ID2D1Bitmap* m_pBitmap;
	ID2D1SolidColorBrush* m_pFaceBrush[BODY_COUNT];

	IDWriteFactory* m_pDWriteFactory;
	IDWriteTextFormat* m_pTextFormat;

};
