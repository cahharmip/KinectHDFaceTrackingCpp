#include "FaceDetection-D2D.h"
#include "stdafx.h"
#include "resource.h"
#include <strsafe.h>

// face property text layout offset in X axis
static const float c_FaceTextLayoutOffsetX = -0.1f;

// face property text layout offset in Y axis
static const float c_FaceTextLayoutOffsetY = -0.125f;
static int count = 0;
static const int limit = 2;
std::vector<std::vector<float>> deformations(BODY_COUNT, std::vector<float>(FaceShapeDeformations::FaceShapeDeformations_Count));

static const DWORD c_FaceFrameFeatures =
FaceFrameFeatures::FaceFrameFeatures_BoundingBoxInColorSpace
| FaceFrameFeatures::FaceFrameFeatures_PointsInColorSpace;

/*
static const DWORD c_FaceFrameFeatures =
FaceFrameFeatures::FaceFrameFeatures_BoundingBoxInInfraredSpace
| FaceFrameFeatures::FaceFrameFeatures_PointsInInfraredSpace;
*/

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	FaceDetection application;
	application.Run(hInstance, nCmdShow);
}

FaceDetection::FaceDetection():
	m_hwnd(0),
	m_nStartTime(0),
	m_nLastCounter(0),
	m_fFreq(0),
	m_nNextStatusTime(0),
	m_nFrameSinceUpdate(0),
	faceBox{0},
	m_bSaveScreenshot(0),

	m_pKinectSensor(nullptr),
	m_pCoordinateMapper(nullptr),
	m_pColorFrameReader(nullptr),
	m_pBodyFrameReader(nullptr),
	m_pDrawDataStreams(nullptr),
	m_pD2DFactory(nullptr),
	m_pColorRGBX(nullptr),
	m_pColorSpacePoint(nullptr)

{
	LARGE_INTEGER qpf = { 0 };
	if (QueryPerformanceFrequency(&qpf))
	{
		m_fFreq = double(qpf.QuadPart);
	}

	for (int i = 0; i < BODY_COUNT; i++) {
		m_pFaceFrameSource[i] = nullptr;
		m_pFaceFrameReader[i] = nullptr;
		m_pHDFaceFrameReader[i] = nullptr;
		m_pHDFaceFrameSource[i] = nullptr;
		m_pFaceModelBuilder[i] = nullptr;
		m_pFaceModel[i] = nullptr;
		m_pFaceAlignment[i] = nullptr;
	}
	m_pColorRGBX = new RGBQUAD[cColorWidth * cColorHeight];
}

FaceDetection::~FaceDetection() {
	// clean up Direct2D renderer
	if (m_pDrawDataStreams)
	{
		delete m_pDrawDataStreams;
		m_pDrawDataStreams = nullptr;
	}

	if (m_pColorRGBX)
	{
		delete[] m_pColorRGBX;
		m_pColorRGBX = nullptr;
	}

	// clean up Direct2D
	SafeRelease(m_pD2DFactory);

	// done with face sources and readers
	for (int i = 0; i < BODY_COUNT; i++)
	{
		SafeRelease(m_pFaceFrameSource[i]);
		SafeRelease(m_pFaceFrameReader[i]);
	}

	// done with body frame reader
	SafeRelease(m_pBodyFrameReader);

	// done with color frame reader
	SafeRelease(m_pColorFrameReader);

	// done with coordinate mapper
	SafeRelease(m_pCoordinateMapper);

	// close the Kinect Sensor
	if (m_pKinectSensor)
	{
		m_pKinectSensor->Close();
	}

	SafeRelease(m_pKinectSensor);
}

int FaceDetection::Run(HINSTANCE hInstance, int nCmdShow) {
	MSG       msg = { 0 };
	WNDCLASS  wc;

	// Dialog custom window class
	ZeroMemory(&wc, sizeof(wc));
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
	wc.lpfnWndProc = DefDlgProcW;
	wc.lpszClassName = L"FaceBasicsAppDlgWndClass";

	if (!RegisterClassW(&wc))
	{
		return 0;
	}

	HWND hWndApp = CreateDialogParamW(
		NULL,
		MAKEINTRESOURCE(IDD_APP),
		NULL,
		(DLGPROC)FaceDetection::MessageRouter,
		reinterpret_cast<LPARAM>(this));

	ShowWindow(hWndApp, nCmdShow);
	while (WM_QUIT != msg.message)
	{
		Update();

		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			// If a dialog message will be taken care of by the dialog proc
			if (hWndApp && IsDialogMessageW(hWndApp, &msg))
			{
				continue;
			}

			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK FaceDetection::MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	FaceDetection* pThis = nullptr;

	if (WM_INITDIALOG == uMsg)
	{
		pThis = reinterpret_cast<FaceDetection*>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
	}
	else
	{
		pThis = reinterpret_cast<FaceDetection*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}

	if (pThis)
	{
		return pThis->DlgProc(hWnd, uMsg, wParam, lParam);
	}

	return 0;
}

LRESULT CALLBACK FaceDetection::DlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	UNREFERENCED_PARAMETER(wParam);
	UNREFERENCED_PARAMETER(lParam);

	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		// Bind application window handle
		m_hwnd = hwnd;

		// Init Direct2D
		D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);

		// Create and initialize a new Direct2D image renderer (take a look at ImageRenderer.h)
		// We'll use this to draw the data we receive from the Kinect to the screen
		m_pDrawDataStreams = new ImageRenderer();
		HRESULT hr = m_pDrawDataStreams->Initialize(GetDlgItem(m_hwnd, IDC_VIDEOVIEW), m_pD2DFactory, cColorWidth, cColorHeight, cColorWidth * sizeof(RGBQUAD));
		if (FAILED(hr))
		{
			SetStatusMessage(L"Failed to initialize the Direct2D draw device.", 10000, true);
		}

		// Get and initialize the default Kinect sensor
		InitializeSensor();
	}
	break;

	// If the titlebar X is clicked, destroy app
	case WM_CLOSE:
		DestroyWindow(hwnd);
		break;

	case WM_DESTROY:
		// Quit the main message pump
		PostQuitMessage(0);
		break;
	}

	return FALSE;
}

HRESULT FaceDetection::InitializeSensor() {
	HRESULT hr;
	OutputDebugStringW(L"\nHello From Initialize string.\n\n");
	hr = GetDefaultKinectSensor(&m_pKinectSensor);
	if (FAILED(hr)) {
		return hr;
	}
	

	if (m_pKinectSensor) {
		IColorFrameSource* m_pColorFrameSource = nullptr;
		IBodyFrameSource* m_pBodyFrameSource = nullptr;
		hr = m_pKinectSensor->Open();
		if (SUCCEEDED(hr)) {
			hr = m_pKinectSensor->get_CoordinateMapper(&m_pCoordinateMapper);
		}

		if (SUCCEEDED(hr)) {
			hr = m_pKinectSensor->get_ColorFrameSource(&m_pColorFrameSource);
		}

		if (SUCCEEDED(hr)) {
			hr = m_pColorFrameSource->OpenReader(&m_pColorFrameReader);
		}

		if (SUCCEEDED(hr)) {
			hr = m_pKinectSensor->get_BodyFrameSource(&m_pBodyFrameSource);
		}

		if (SUCCEEDED(hr)) {
			hr = m_pBodyFrameSource->OpenReader(&m_pBodyFrameReader);
		}

		if (SUCCEEDED(hr)) {
			for (int i = 0; i < BODY_COUNT; i++) {
				hr = CreateFaceFrameSource(m_pKinectSensor,0,c_FaceFrameFeatures,&m_pFaceFrameSource[i]);
				if (SUCCEEDED(hr)) {
					hr = m_pFaceFrameSource[i]->OpenReader(&m_pFaceFrameReader[i]);
				}

				if (SUCCEEDED(hr)) {
					hr = CreateHighDefinitionFaceFrameSource(m_pKinectSensor, &m_pHDFaceFrameSource[i]);
				}

				if (SUCCEEDED(hr)) {
					hr = m_pHDFaceFrameSource[i]->OpenReader(&m_pHDFaceFrameReader[i]);
				}

				if (SUCCEEDED(hr)) {
					hr = m_pHDFaceFrameSource[i]->OpenModelBuilder(FaceModelBuilderAttributes::FaceModelBuilderAttributes_None, &m_pFaceModelBuilder[i]);
				}

				if (SUCCEEDED(hr)) {
					hr = m_pFaceModelBuilder[i]->BeginFaceDataCollection();
				}

				if (SUCCEEDED(hr)) {
					hr = CreateFaceAlignment(&m_pFaceAlignment[i]);
				}

				if (SUCCEEDED(hr)) {
					hr = CreateFaceModel(1.0f, FaceShapeDeformations::FaceShapeDeformations_Count, &deformations[count][0], &m_pFaceModel[count]);
				}
			}
			hr = GetFaceModelVertexCount(&vertex);
		}
		SafeRelease(m_pColorFrameSource);
		SafeRelease(m_pBodyFrameSource);
	}

	if (!m_pKinectSensor || FAILED(hr))
	{
		SetStatusMessage(L"No ready Kinect found!", 10000, true);
		return E_FAIL;
	}

	return hr;
}

void FaceDetection::Update() {
	HRESULT hr;
	if (!m_pColorFrameReader || !m_pBodyFrameReader) {
		OutputDebugString(L"/nRETURNNN/n");
		return;
	}
	UINT dCapacity;
	UINT16* dBuffer;
	IDepthFrame* m_pDepthFrame = nullptr;
	/*hr = m_pDepthFrame->AccessUnderlyingBuffer(&dCapacity,&dBuffer);
	if (SUCCEEDED(hr)) {
		hr = m_pCoordinateMapper->MapDepthFrameToColorSpace(dCapacity,dBuffer,cColorHeight*cColorWidth,m_pColorSpacePoint);
	}	
	*/
	IColorFrame* m_pColorFrame = nullptr;
	hr = m_pColorFrameReader->AcquireLatestFrame(&m_pColorFrame);
	if (SUCCEEDED(hr)) {
		INT64 nTime = 0;
		IFrameDescription* m_pFrameDescription = nullptr;
		int width = 0;
		int height = 0;
		ColorImageFormat imageFormat = ColorImageFormat_None;
		UINT bufferSize = 0;
		RGBQUAD* pBuffer = nullptr;
		RGBQUAD* pRGBAccess = nullptr;
		RGBQUAD* pfaceBuffer = nullptr;
		RGBQUAD* pfaceOrigin = nullptr;
		int faceWidth = 0;
		int faceHeight = 0;

		char msgBuf[256];

		hr = m_pColorFrame->get_RelativeTime(&nTime);
		if (SUCCEEDED(hr)) {
			hr = m_pColorFrame->get_FrameDescription(&m_pFrameDescription);
		}

		if (SUCCEEDED(hr))
		{	
			hr = m_pFrameDescription->get_Width(&width);/*
			sprintf_s(msgBuf, "\nMy variable is %d\n\n", width);
			OutputDebugStringA(msgBuf);*/
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pFrameDescription->get_Height(&height);/*
			sprintf_s(msgBuf, "\nMy variable is %d\n\n", height);
			OutputDebugStringA(msgBuf);*/
		}
		
		if (SUCCEEDED(hr)) {
			hr = m_pColorFrame->get_RawColorImageFormat(&imageFormat);
			//sprintf_s(msgBuf, "\nImageFormat is %d\n\n", imageFormat);
			//OutputDebugStringA(msgBuf);
		}

		if (SUCCEEDED(hr)) {
			if (imageFormat == ColorImageFormat_Bgra) {
				hr = m_pColorFrame->AccessRawUnderlyingBuffer(&bufferSize, reinterpret_cast<BYTE**>(&pBuffer));
			}
			else if (m_pColorRGBX) {
				pBuffer = m_pColorRGBX;
				pRGBAccess = m_pColorRGBX;
				bufferSize = width * height * sizeof(RGBQUAD);
				hr = m_pColorFrame->CopyConvertedFrameDataToArray(bufferSize, reinterpret_cast<BYTE*>(pRGBAccess), ColorImageFormat_Bgra);
				hr = m_pColorFrame->CopyConvertedFrameDataToArray(bufferSize,reinterpret_cast<BYTE*>(pBuffer), ColorImageFormat_Bgra);
				RectI* pFaceBox = &faceBox;
				int initCoor = 0;
				faceWidth = pFaceBox->Right - pFaceBox->Left;
				faceHeight = pFaceBox->Bottom - pFaceBox->Top;
				pfaceBuffer = new RGBQUAD[faceWidth*faceHeight];
				pfaceOrigin = pfaceBuffer;
				if (pFaceBox->Left != 0 && pFaceBox->Right != 0) {
					initCoor = pFaceBox->Left + (pFaceBox->Top - 1) * 1920;
					sprintf_s(msgBuf, "\nInitCoor is %d\n\n", initCoor);
					OutputDebugStringA(msgBuf);
				}
				
				if (pFaceBox->Left != 0 && pFaceBox->Right != 0 && initCoor >= 0) {
					pRGBAccess += initCoor;
					for (int i = 0; i < faceHeight; i++) {
						for (int j = 0; j < faceWidth; j++) {
							/*Grey Scale*/
							/*pfaceBuffer->rgbBlue = pRGBAccess->rgbBlue * 0.1140 + pRGBAccess->rgbRed * 0.2989 + pRGBAccess->rgbGreen*0.5870 ;
							pfaceBuffer->rgbRed = pRGBAccess->rgbBlue * 0.1140 + pRGBAccess->rgbRed * 0.2989 + pRGBAccess->rgbGreen*0.5870;
							pfaceBuffer->rgbGreen = pRGBAccess->rgbBlue * 0.1140 + pRGBAccess->rgbRed * 0.2989 + pRGBAccess->rgbGreen*0.5870;*/

							/*RGB*/
							pfaceBuffer->rgbBlue = pRGBAccess->rgbBlue;
							pfaceBuffer->rgbRed = pRGBAccess->rgbRed;
							pfaceBuffer->rgbGreen = pRGBAccess->rgbGreen;
							pfaceBuffer->rgbReserved = 0x00;
							pRGBAccess += 1;
							pfaceBuffer += 1;
						}
						pRGBAccess += 1920 - faceWidth;
					}
					pfaceBuffer = pfaceOrigin;
				}
					
				sprintf_s(msgBuf, "\nPBuffer is %d\n\n", pRGBAccess->rgbBlue);
				OutputDebugStringA(msgBuf);
				if (FAILED(hr)) {
					OutputDebugString(L"\nCan't CopyConvertedFrameDataToArray\n");
				}
			}
			else {
				hr = E_FAIL;
			}
		}
		if (SUCCEEDED(hr)) {
			//OutputDebugString(L"\nTime to Draw!!\n");
			DrawColorFaceStream(nTime, pBuffer, width, height);
			if (faceWidth != 0 && faceHeight != 0) {
				m_bSaveScreenshot = true;
				if (m_bSaveScreenshot)
				{
					OutputDebugString(L"\nScreenShortSave!!\n");
					WCHAR szScreenshotPath[MAX_PATH];

					// Retrieve the path to My Photos
					GetScreenshotFileName(szScreenshotPath, _countof(szScreenshotPath));

					// Write out the bitmap to disk
					HRESULT hr = SaveBitmapToFile(reinterpret_cast<BYTE*>(pfaceBuffer), faceWidth, faceHeight, sizeof(RGBQUAD) * 8, szScreenshotPath);

					WCHAR szStatusMessage[64 + MAX_PATH];
					if (SUCCEEDED(hr))
					{
						// Set the status bar to show where the screenshot was saved
						StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L"Screenshot saved to %s", szScreenshotPath);
					}
					else
					{
						StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L"Failed to write screenshot to %s", szScreenshotPath);
					}

					SetStatusMessage(szStatusMessage, 5000, true);

					// toggle off so we don't save a screenshot again next frame
					m_bSaveScreenshot = false;
				}
			}
		}
		SafeRelease(m_pFrameDescription);
	}
	SafeRelease(m_pColorFrame);
}

void FaceDetection::DrawColorFaceStream(INT64 nTime, RGBQUAD* pBuffer, int width, int height) {
	if (m_hwnd) {
		HRESULT hr;
		char msgBuf[256];
		hr = m_pDrawDataStreams->BeginDrawing();
		if (SUCCEEDED(hr)) {
			if (pBuffer && (width == cColorWidth && height == cColorHeight)) {
				hr = m_pDrawDataStreams->DrawBackground(reinterpret_cast<BYTE*>(pBuffer), cColorWidth * cColorHeight * sizeof(RGBQUAD));
			}
			else {
				hr = E_INVALIDARG;
			}
			if (SUCCEEDED(hr)) {
				ProcessFace();
			}

			m_pDrawDataStreams->EndDrawing();
		}
		if (!m_nStartTime) {
			m_nStartTime = nTime;
		}

		double fps = 0.0;
		LARGE_INTEGER qpcNow = { 0 };
		if (m_fFreq)
		{
			if (QueryPerformanceCounter(&qpcNow))
			{
				if (m_nLastCounter)
				{
					m_nFrameSinceUpdate++;
					fps = m_fFreq * m_nFrameSinceUpdate / double(qpcNow.QuadPart - m_nLastCounter);
				}
			}
		}
		WCHAR szStatusMessage[64];
		StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L" FPS = %0.2f    Time = %I64d", fps, (nTime - m_nStartTime));

		if (SetStatusMessage(szStatusMessage, 1000, false))
		{
			m_nLastCounter = qpcNow.QuadPart;
			m_nFrameSinceUpdate = 0;
		}
	}
}

void FaceDetection::ProcessFace() {
	HRESULT hr;
	IBody* ppBodies[BODY_COUNT] = { 0 };
	bool bHaveBodyData = SUCCEEDED(UpdateBodyData(ppBodies));

	for (int iFace = 0; iFace < BODY_COUNT; ++iFace) {
		//IFaceFrame* m_pFaceFrame = nullptr;
		IHighDefinitionFaceFrame* m_pHDFaceFrame = nullptr;
		//hr = m_pFaceFrameReader[iFace]->AcquireLatestFrame(&m_pFaceFrame); /*<--- Start Here ----*/
		/*BOOLEAN bFaceTracked = false;
		if (SUCCEEDED(hr) && nullptr != m_pFaceFrame) {
			hr = m_pFaceFrame->get_IsTrackingIdValid(&bFaceTracked);
		}*/
		BOOLEAN bFaceTrackedHD = false;
		hr = m_pHDFaceFrameSource[iFace]->get_IsTrackingIdValid(&bFaceTrackedHD);

		if (SUCCEEDED(hr)) {
			/*============== HD ZONE ===============*/
			if (!bFaceTrackedHD) {
				IBody* pBody = ppBodies[iFace];
				BOOLEAN bTracked = false;
				if (pBody != nullptr) {
					hr = pBody->get_IsTracked(&bTracked);
					if (SUCCEEDED(hr) && bTracked) {
						UINT64 trackingId = _UI64_MAX;
						hr = ppBodies[iFace]->get_TrackingId(&trackingId);
						if (SUCCEEDED(hr)) {
							m_pHDFaceFrameSource[iFace]->put_TrackingId(trackingId);
						}
					}
				}
			}
		}
	}
	if (bHaveBodyData) {
		for (int i = 0; i < _countof(ppBodies); i++) {
			SafeRelease(ppBodies[i]);
		}
	}

	for (int count = 0; count < BODY_COUNT; count++) {
		IHighDefinitionFaceFrame* pHDFaceFrame = nullptr;
		hr = m_pHDFaceFrameReader[count]->AcquireLatestFrame(&pHDFaceFrame);
		if (SUCCEEDED(hr) && pHDFaceFrame != nullptr) {
			BOOLEAN bFaceTracked = false;
			hr = pHDFaceFrame->get_IsFaceTracked(&bFaceTracked);
			if (SUCCEEDED(hr) && bFaceTracked) {
				hr = pHDFaceFrame->GetAndRefreshFaceAlignmentResult(m_pFaceAlignment[count]);
				if (SUCCEEDED(hr) && m_pFaceAlignment[count] != nullptr) {
					// Face Model Building
					if (!produce[count]) {
						OutputDebugStringW(L"cls");
						FaceModelBuilderCollectionStatus collection;
						hr = m_pFaceModelBuilder[count]->get_CollectionStatus(&collection);
						if (collection == FaceModelBuilderCollectionStatus::FaceModelBuilderCollectionStatus_Complete) {
							OutputDebugStringW(L"Status :: Complete");
							IFaceModelData* pFaceModelData = nullptr;
							hr = m_pFaceModelBuilder[count]->GetFaceData(&pFaceModelData);
							if (SUCCEEDED(hr) && pFaceModelData != nullptr) {
								hr = pFaceModelData->ProduceFaceModel(&m_pFaceModel[count]);
								if (SUCCEEDED(hr) && m_pFaceModel[count] != nullptr) {
									produce[count] = true;
								}
							}
							SafeRelease(pFaceModelData);
						}
						else {
							char msgBuf[256];
							sprintf_s(msgBuf, "\n Status :: %d\n", collection);
							OutputDebugStringA(msgBuf);
							//std::cout << "Status : " << collection << std::endl;
							//cv::putText(bufferMat, "Status : " + std::to_string(collection), cv::Point(50, 50), cv::FONT_HERSHEY_SIMPLEX, 1.0f, static_cast<cv::Scalar>(color[count]), 2, CV_AA);

							// Collection Status
							if (collection >= FaceModelBuilderCollectionStatus::FaceModelBuilderCollectionStatus_TiltedUpViewsNeeded) {
								OutputDebugStringW(L"Need : Tilted Up Views");
								//std::cout << "Need : Tilted Up Views" << std::endl;
								//cv::putText(bufferMat, "Need : Tilted Up Views", cv::Point(50, 100), cv::FONT_HERSHEY_SIMPLEX, 1.0f, static_cast<cv::Scalar>(color[count]), 2, CV_AA);
							}
							else if (collection >= FaceModelBuilderCollectionStatus::FaceModelBuilderCollectionStatus_RightViewsNeeded) {
								OutputDebugStringW(L"Need : Right Views");
								//std::cout << "Need : Right Views" << std::endl;
								//cv::putText(bufferMat, "Need : Right Views", cv::Point(50, 100), cv::FONT_HERSHEY_SIMPLEX, 1.0f, static_cast<cv::Scalar>(color[count]), 2, CV_AA);
							}
							else if (collection >= FaceModelBuilderCollectionStatus::FaceModelBuilderCollectionStatus_LeftViewsNeeded) {
								OutputDebugStringW(L"Need : Left Views");
								//std::cout << "Need : Left Views" << std::endl;
								//cv::putText(bufferMat, "Need : Left Views", cv::Point(50, 100), cv::FONT_HERSHEY_SIMPLEX, 1.0f, static_cast<cv::Scalar>(color[count]), 2, CV_AA);
							}
							else if (collection >= FaceModelBuilderCollectionStatus::FaceModelBuilderCollectionStatus_FrontViewFramesNeeded) {
								OutputDebugStringW(L"Need : Front ViewFrames");
								//std::cout << "Need : Front ViewFrames" << std::endl;
								//cv::putText(bufferMat, "Need : Front ViewFrames", cv::Point(50, 100), cv::FONT_HERSHEY_SIMPLEX, 1.0f, static_cast<cv::Scalar>(color[count]), 2, CV_AA);
							}

							// Capture Status
							FaceModelBuilderCaptureStatus capture;
							hr = m_pFaceModelBuilder[count]->get_CaptureStatus(&capture);
							switch (capture) {
							case FaceModelBuilderCaptureStatus::FaceModelBuilderCaptureStatus_FaceTooFar:
								OutputDebugStringW(L"Error : Face Too Far from Camera");
								//std::cout << "Error : Face Too Far from Camera" << std::endl;
								//cv::putText(bufferMat, "Error : Face Too Far from Camera", cv::Point(50, 150), cv::FONT_HERSHEY_SIMPLEX, 1.0f, static_cast<cv::Scalar>(color[count]), 2, CV_AA);
								break;
							case FaceModelBuilderCaptureStatus::FaceModelBuilderCaptureStatus_FaceTooNear:
								OutputDebugStringW(L"Error : Face Too Near to Camera");
								//std::cout << "Error : Face Too Near to Camera" << std::endl;
								//cv::putText(bufferMat, "Error : Face Too Near to Camera", cv::Point(50, 150), cv::FONT_HERSHEY_SIMPLEX, 1.0f, static_cast<cv::Scalar>(color[count]), 2, CV_AA);
								break;
							case FaceModelBuilderCaptureStatus_MovingTooFast:
								OutputDebugStringW(L"Error : Moving Too Fast");
								//std::cout << "Error : Moving Too Fast" << std::endl;
								//cv::putText(bufferMat, "Error : Moving Too Fast", cv::Point(50, 150), cv::FONT_HERSHEY_SIMPLEX, 1.0f, static_cast<cv::Scalar>(color[count]), 2, CV_AA);
								break;
							default:
								break;
							}
						}
					}
					//Dont Breakkk Why??
					// HD Face Points  <<<<<<<<<<<<<<<<< CONTINUE HEREE >>>>>>>>>>>>>>>>>>>>>
					std::vector<CameraSpacePoint> facePoints(vertex);
					if (m_pFaceModel[count] != NULL) {
						hr = m_pFaceModel[count]->CalculateVerticesForAlignment(m_pFaceAlignment[count], vertex, &facePoints[0]);
						if (SUCCEEDED(hr)) {
							for (int point = 0; point < vertex; point++) {
								ColorSpacePoint colorSpacePoint;
								hr = m_pCoordinateMapper->MapCameraPointToColorSpace(facePoints[point], &colorSpacePoint);
								if (SUCCEEDED(hr)) {
									int x = static_cast<int>(colorSpacePoint.X);
									int y = static_cast<int>(colorSpacePoint.Y);
									if ((x >= 0) && (x < 1920) && (y >= 0) && (y < 1080)) {
										m_pDrawDataStreams->DrawFaceFrameResults(count, x, y);
										//cv::circle(bufferMat, cv::Point(static_cast<int>(colorSpacePoint.X), static_cast<int>(colorSpacePoint.Y)), 5, static_cast<cv::Scalar>(color[count]), -1, CV_AA);
									}
								}
							}
						}
					}
				}
			}
		}
		SafeRelease(pHDFaceFrame);
	}
}

HRESULT FaceDetection::UpdateBodyData(IBody** ppBodies) {
	HRESULT hr = E_FAIL;
	if (m_pBodyFrameReader != nullptr) {
		IBodyFrame* m_pBodyFrame = nullptr;
		hr = m_pBodyFrameReader->AcquireLatestFrame(&m_pBodyFrame);
		if (SUCCEEDED(hr)) {
			hr = m_pBodyFrame->GetAndRefreshBodyData(BODY_COUNT, ppBodies);
		}
		SafeRelease(m_pBodyFrame);
	}

	return hr;
}

bool FaceDetection::SetStatusMessage(_In_z_ WCHAR* szMessage, ULONGLONG nShowTimeMsec, bool bForce)
{
	ULONGLONG now = GetTickCount64();

	if (m_hwnd && (bForce || (m_nNextStatusTime <= now)))
	{
		SetDlgItemText(m_hwnd, IDC_STATUS, szMessage);
		m_nNextStatusTime = now + nShowTimeMsec;

		return true;
	}

	return false;
}

/// <summary>
/// Get the name of the file where screenshot will be stored.
/// </summary>
/// <param name="lpszFilePath">string buffer that will receive screenshot file name.</param>
/// <param name="nFilePathSize">number of characters in lpszFilePath string buffer.</param>
/// <returns>
/// S_OK on success, otherwise failure code.
/// </returns>
HRESULT FaceDetection::GetScreenshotFileName(_Out_writes_z_(nFilePathSize) LPWSTR lpszFilePath, UINT nFilePathSize)
{
	WCHAR* pszKnownPath = NULL;
	HRESULT hr = SHGetKnownFolderPath(FOLDERID_Pictures, 0, NULL, &pszKnownPath);

	if (SUCCEEDED(hr))
	{
		// Get the time
		WCHAR szTimeString[MAX_PATH];
		GetTimeFormatEx(NULL, 0, NULL, L"hh'-'mm'-'ss", szTimeString, _countof(szTimeString));

		// File name will be KinectScreenshotColor-HH-MM-SS.bmp
		StringCchPrintfW(lpszFilePath, nFilePathSize, L"%s\\KinectScreenshot-Color-%s.bmp", pszKnownPath, szTimeString);
	}

	if (pszKnownPath)
	{
		CoTaskMemFree(pszKnownPath);
	}

	return hr;
}

/// <summary>
/// Save passed in image data to disk as a bitmap
/// </summary>
/// <param name="pBitmapBits">image data to save</param>
/// <param name="lWidth">width (in pixels) of input image data</param>
/// <param name="lHeight">height (in pixels) of input image data</param>
/// <param name="wBitsPerPixel">bits per pixel of image data</param>
/// <param name="lpszFilePath">full file path to output bitmap to</param>
/// <returns>indicates success or failure</returns>
HRESULT FaceDetection::SaveBitmapToFile(BYTE* pBitmapBits, LONG lWidth, LONG lHeight, WORD wBitsPerPixel, LPCWSTR lpszFilePath)
{
	
	DWORD dwByteCount = lWidth * lHeight * (wBitsPerPixel / 8);

	BITMAPINFOHEADER bmpInfoHeader = { 0 };

	bmpInfoHeader.biSize = sizeof(BITMAPINFOHEADER);  // Size of the header
	bmpInfoHeader.biBitCount = wBitsPerPixel;             // Bit count
	bmpInfoHeader.biCompression = BI_RGB;                    // Standard RGB, no compression
	bmpInfoHeader.biWidth = lWidth;                    // Width in pixels
	bmpInfoHeader.biHeight = -lHeight;                  // Height in pixels, negative indicates it's stored right-side-up
	bmpInfoHeader.biPlanes = 1;                         // Default
	bmpInfoHeader.biSizeImage = dwByteCount;               // Image size in bytes

	BITMAPFILEHEADER bfh = { 0 };

	bfh.bfType = 0x4D42;                                           // 'M''B', indicates bitmap
	bfh.bfOffBits = bmpInfoHeader.biSize + sizeof(BITMAPFILEHEADER);  // Offset to the start of pixel data
	bfh.bfSize = bfh.bfOffBits + bmpInfoHeader.biSizeImage;        // Size of image + headers

																   // Create the file on disk to write to
	HANDLE hFile = CreateFileW(lpszFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	// Return if error opening file
	if (NULL == hFile)
	{
		return E_ACCESSDENIED;
	}

	DWORD dwBytesWritten = 0;

	// Write the bitmap file header
	if (!WriteFile(hFile, &bfh, sizeof(bfh), &dwBytesWritten, NULL))
	{
		CloseHandle(hFile);
		return E_FAIL;
	}

	// Write the bitmap info header
	if (!WriteFile(hFile, &bmpInfoHeader, sizeof(bmpInfoHeader), &dwBytesWritten, NULL))
	{
		CloseHandle(hFile);
		return E_FAIL;
	}

	// Write the RGB Data
	if (!WriteFile(hFile, pBitmapBits, bmpInfoHeader.biSizeImage, &dwBytesWritten, NULL))
	{
		CloseHandle(hFile);
		return E_FAIL;
	}

	// Close the file
	CloseHandle(hFile);
	return S_OK;
}


