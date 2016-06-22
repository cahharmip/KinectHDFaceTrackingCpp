#include "stdafx.h"
#include <string>
#include "ImageRenderer.h"
using namespace DirectX;

static const float c_FaceBoxThickness = 6.0f;
static const float c_FacePointThickness = 5.0f;
static const float c_FacePointRadius = 0.4f;


ImageRenderer::ImageRenderer() :
	m_hwnd(0),
	m_sourceWidth(0),
	m_sourceHeight(0),
	m_sourceStride(0),
	m_pBitmap(nullptr),
	m_pD2DFactory(nullptr),
	m_pRenderTarget(nullptr)
{
	for (int i = 0; i < BODY_COUNT; i++) {
		m_pFaceBrush[i] = nullptr;
	}
}

ImageRenderer::~ImageRenderer() {
	DiscardResource();
	SafeRelease(m_pTextFormat);
	SafeRelease(m_pDWriteFactory);
	SafeRelease(m_pD2DFactory);
}

HRESULT ImageRenderer::EnsureResource() { 
	HRESULT hr = S_OK;

	if (nullptr == m_pRenderTarget)
	{
		OutputDebugStringW(L"\nRender Target is prepare.\n\n");
		D2D1_SIZE_U size = D2D1::SizeU(m_sourceWidth, m_sourceHeight);

		D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
		rtProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
		rtProps.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;

		// Create a hWnd render target, in order to render to the window set in initialize
		hr = m_pD2DFactory->CreateHwndRenderTarget(
			rtProps,
			D2D1::HwndRenderTargetProperties(m_hwnd, size),
			&m_pRenderTarget
			);

		if (SUCCEEDED(hr))
		{
			OutputDebugStringW(L"\nRenderTargetIsReady.\n\n");
			// Create a bitmap that we can copy image data into and then render to the target
			hr = m_pRenderTarget->CreateBitmap(
				size,
				D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
				&m_pBitmap
				);
		}

		if (SUCCEEDED(hr))
		{
			OutputDebugStringW(L"\nBitmap Created.\n\n");
			hr = m_pRenderTarget->CreateSolidColorBrush((D2D1::ColorF(D2D1::ColorF::Red, 2.0f)), &m_pFaceBrush[0]);
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pRenderTarget->CreateSolidColorBrush((D2D1::ColorF(D2D1::ColorF::Green, 2.0f)), &m_pFaceBrush[1]);
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pRenderTarget->CreateSolidColorBrush((D2D1::ColorF(D2D1::ColorF::White, 2.0f)), &m_pFaceBrush[2]);
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pRenderTarget->CreateSolidColorBrush((D2D1::ColorF(D2D1::ColorF::Purple, 2.0f)), &m_pFaceBrush[3]);
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pRenderTarget->CreateSolidColorBrush((D2D1::ColorF(D2D1::ColorF::Orange, 2.0f)), &m_pFaceBrush[4]);
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pRenderTarget->CreateSolidColorBrush((D2D1::ColorF(D2D1::ColorF::Pink, 2.0f)), &m_pFaceBrush[5]);
		}
	}

	if (FAILED(hr))
	{
		DiscardResource();
	}

	return hr;
}

HRESULT ImageRenderer::Initialize(HWND hwnd, ID2D1Factory* pD2DFactory, int sourceWidth, int sourceHeight, int sourceStride) { 
	if (nullptr == pD2DFactory) {
		return E_INVALIDARG;
	}

	m_hwnd = hwnd;

	m_pD2DFactory = pD2DFactory;
	m_pD2DFactory->AddRef();

	m_sourceWidth = sourceWidth;
	m_sourceHeight = sourceHeight;
	m_sourceStride = sourceStride;

	HRESULT hr;
	hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(&m_pDWriteFactory));
	return hr;
}

HRESULT ImageRenderer::BeginDrawing() { 
	HRESULT hr = EnsureResource();

	if (SUCCEEDED(hr))
	{
		m_pRenderTarget->BeginDraw();
	}

	return hr;
}

HRESULT ImageRenderer::EndDrawing() {
	HRESULT hr;
	hr = m_pRenderTarget->EndDraw();

	// Device lost, need to recreate the render target
	// We'll dispose it now and retry drawing
	if (hr == D2DERR_RECREATE_TARGET)
	{
		hr = S_OK;
		DiscardResource();
	}

	return hr;
}

HRESULT ImageRenderer::DrawBackground(BYTE* pImage, unsigned long cbImage) {
	HRESULT hr = S_OK;
	if (cbImage < ((m_sourceHeight - 1) * m_sourceStride) + (m_sourceWidth * 4))
	{
		hr = E_INVALIDARG;
	}
	if (SUCCEEDED(hr))
	{
		// Copy the image that was passed in into the direct2d bitmap
		hr = m_pBitmap->CopyFromMemory(NULL, pImage, m_sourceStride);

		if (SUCCEEDED(hr))
		{
			// Draw the bitmap stretched to the size of the window
			m_pRenderTarget->DrawBitmap(m_pBitmap);
		}
	}

	return hr;
 }

void ImageRenderer::DrawFaceFrameResults(int iFace,int x, int y) {
		ID2D1SolidColorBrush* brush = m_pFaceBrush[iFace];
		D2D1_ELLIPSE facePoint = D2D1::Ellipse(D2D1::Point2F(x, y), c_FacePointRadius, c_FacePointRadius);
		m_pRenderTarget->DrawEllipse(facePoint, brush, c_FacePointThickness);
}

void ImageRenderer::DiscardResource() {
	for (int i = 0; i < BODY_COUNT; i++)
	{
		SafeRelease(m_pFaceBrush[i]);
	}
	SafeRelease(m_pRenderTarget);
	SafeRelease(m_pBitmap);
}
bool ImageRenderer::ValidateFaceBoxAndPoint(const RectI* pFaceBox, const PointF* pFacePoints) { 
	bool isFaceValid = false;

	if (pFaceBox != nullptr)
	{
		INT32 screenWidth = m_sourceWidth;
		INT32 screenHeight = m_sourceHeight;

		INT32 width = pFaceBox->Right - pFaceBox->Left;
		INT32 height = pFaceBox->Bottom - pFaceBox->Top;

		// check if we have a valid rectangle within the bounds of the screen space
		isFaceValid = width > 0 &&
			height > 0 &&
			pFaceBox->Right <= screenWidth &&
			pFaceBox->Bottom <= screenHeight;

		if (isFaceValid)
		{
			for (int i = 0; i < FacePointType::FacePointType_Count; i++)
			{
				// check if we have a valid face point within the bounds of the screen space                        
				bool isFacePointValid = pFacePoints[i].X > 0.0f &&
					pFacePoints[i].Y > 0.0f &&
					pFacePoints[i].X < m_sourceWidth &&
					pFacePoints[i].Y < m_sourceHeight;

				if (!isFacePointValid)
				{
					isFaceValid = false;
					break;
				}
			}
		}
	}
	return isFaceValid; 
}