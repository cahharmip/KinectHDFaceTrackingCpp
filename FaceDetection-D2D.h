#include "resource.h"
#include "ImageRenderer.h"
#include "stdafx.h"
#include <vector>

class FaceDetection{
	static const int cDepthWidth = 512;
	static const int cDepthHeight = 424;
	static const int cColorWidth = 1920;
	static const int cColorHeight = 1080;

public:
	FaceDetection();
	~FaceDetection();

	/// <param name="uMsg">message</param>
	/// <param name="wParam">message data</param>
	/// <param name="lParam">additional message data</param>
	static LRESULT CALLBACK MessageRouter(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK DlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	int Run(HINSTANCE hInstance, int nCmdShow);

private:
	void Update();
	HRESULT UpdateBodyData(IBody** ppBodies);
	HRESULT InitializeSensor();

	void DrawColorFaceStream(INT64 nTime, RGBQUAD* pBuffer, int nWidth, int nHeight);
	void ProcessFace();

	/// <summary>
	/// Set the status bar message
	/// </summary>
	/// <param name="szMessage">message to display</param>
	/// <param name="nShowTimeMsec">time in milliseconds for which to ignore future status messages</param>
	/// <param name="bForce">force status update</param>
	/// <returns>success or failure</returns>
	bool SetStatusMessage(_In_z_ WCHAR* szMessage, ULONGLONG nShowTimeMsec, bool bForce);
	HRESULT GetScreenshotFileName(_Out_writes_z_(nFilePathSize) LPWSTR lpszFilePath, UINT nFilePathSize);
	HRESULT SaveBitmapToFile(BYTE* pBitmapBits, LONG lWidth, LONG lHeight, WORD wBitsPerPixel, LPCWSTR lpszFilePath);

	HWND m_hwnd;
	INT64 m_nStartTime;
	INT64 m_nLastCounter;
	bool m_bSaveScreenshot;
	double m_fFreq;
	ULONGLONG m_nNextStatusTime;
	DWORD m_nFrameSinceUpdate;

	IKinectSensor* m_pKinectSensor;
	ICoordinateMapper* m_pCoordinateMapper;
	IColorFrameReader* m_pColorFrameReader;
	IBodyFrameReader* m_pBodyFrameReader;
	/*-------------HD------------*/
	IHighDefinitionFaceFrameSource* m_pHDFaceFrameSource[BODY_COUNT];
	IHighDefinitionFaceFrameReader* m_pHDFaceFrameReader[BODY_COUNT];
	IFaceModelBuilder* m_pFaceModelBuilder[BODY_COUNT];
	bool produce[BODY_COUNT] = { false };
	IFaceAlignment* m_pFaceAlignment[BODY_COUNT];
	IFaceModel* m_pFaceModel[BODY_COUNT];
	UINT32 vertex = 0;
	/*--------------------------*/
	IFaceFrameSource* m_pFaceFrameSource[BODY_COUNT];
	IFaceFrameReader* m_pFaceFrameReader[BODY_COUNT];
	RectI faceBox;
	

	ImageRenderer* m_pDrawDataStreams;
	ID2D1Factory* m_pD2DFactory;
	RGBQUAD* m_pColorRGBX;
	ColorSpacePoint* m_pColorSpacePoint;
};