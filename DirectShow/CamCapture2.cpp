#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

#include <dshow.h>
#include "qedit.h" // for SampleGrabber
#include <Windows.h>
#include <atlconv.h>

#include <Wincodec.h>
#include <stdio.h>
#include <fstream>

#include <vector>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Windowscodecs.lib")

HWND ghApp = 0;

using namespace std;

// NullRender
IBaseFilter *pNullRenderer = NULL;

// Added for the ICaptureGraphBuilder2
IGraphBuilder * pGraph = NULL;
ICaptureGraphBuilder2 * pCapture = NULL;

// Added for the SampleGrabber
ISampleGrabber *pSampleGrabber = NULL;
IBaseFilter *pSgrabberF = NULL;

IVideoWindow  * pVW = NULL;
IMediaControl * pMC = NULL;
IMediaEventEx * pME = NULL;

static void Win32DecodeJpeg(unsigned int ImageDataSize, void *ImageData,
	unsigned int DestSize, void *Dest)
{
	// IWICImagingFactory is a structure containing the function pointers of the WIC API
	static IWICImagingFactory *IWICFactory;
	if (IWICFactory == NULL)
	{
		if (CoInitializeEx(NULL, COINIT_MULTITHREADED) != S_OK)
		{
			printf("failed to initialize the COM library\n");
			return;
		}

		if (CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&IWICFactory)) != S_OK)
		{
			printf("failed to create an instance of WIC\n");
			return;
		}
	}

	IWICStream *Stream;
	if (IWICFactory->CreateStream(&Stream) != S_OK)
	{
		printf("failed to create stream\n");
		return;
	}

	if (Stream->InitializeFromMemory((unsigned char *)ImageData, ImageDataSize) != S_OK)
	{
		printf("failed to initialize stream from memory\n");
		return;
	}

	IWICBitmapDecoder *BitmapDecoder;
	if (IWICFactory->CreateDecoderFromStream(Stream, NULL, WICDecodeMetadataCacheOnDemand, &BitmapDecoder) != S_OK)
	{
		printf("failed to create bitmap decoder from stream\n");
		return;
	}

	IWICBitmapFrameDecode *FrameDecode;
	// frames apply mostly to GIFs and other animated media. JPEGs just have a single frame.
	if (BitmapDecoder->GetFrame(0, &FrameDecode) != S_OK)
	{
		printf("failed to get 0th frame from frame decoder\n");
		return;
	}

	IWICFormatConverter *FormatConverter;
	if (IWICFactory->CreateFormatConverter(&FormatConverter) != S_OK)
	{
		printf("failed to create format converter\n");
		return;
	}

	// this function does not do any actual decoding
	if (FormatConverter->Initialize(FrameDecode,
		GUID_WICPixelFormat24bppBGR,
		WICBitmapDitherTypeNone,
		nullptr,
		0.0f,
		WICBitmapPaletteTypeCustom) != S_OK)
	{
		printf("failed to initialize format converter\n");
		return;
	}

	IWICBitmap *Bitmap;
	if (IWICFactory->CreateBitmapFromSource(FormatConverter, WICBitmapCacheOnDemand, &Bitmap) != S_OK)
	{
		printf("failed to create bitmap from format converter\n");
		return;
	}

	unsigned int Width, Height;
	if (Bitmap->GetSize(&Width, &Height) != S_OK)
	{
		printf("failed to get the size of the bitmap\n");
		return;
	}
	WICRect Rect = { 0, 0, (int)Width, (int)Height };

	IWICBitmapLock *Lock;
	// this is the function that does the actual decoding. seems like they defer the decoding until it's actually needed
	if (Bitmap->Lock(&Rect, WICBitmapLockRead, &Lock) != S_OK)
	{
		printf("failed to lock bitmap\n");
		return;
	}

	unsigned int PixelDataSize = 0;
	unsigned char *PixelData;
	if (Lock->GetDataPointer(&PixelDataSize, &PixelData) != S_OK)
	{
		printf("failed to get data pointer\n");
		return;
	}

	memcpy(Dest, PixelData, DestSize < PixelDataSize ? DestSize : PixelDataSize);

	Stream->Release();
	BitmapDecoder->Release();
	FrameDecode->Release();
	FormatConverter->Release();
	Bitmap->Release();
	Lock->Release();
}
static char * ConvertWCtoC(wchar_t* str)
{
	//반환할 char* 변수 선언
	char* pStr;

	//입력받은 wchar_t 변수의 길이를 구함
	int strSize = WideCharToMultiByte(CP_ACP, 0, str, -1, NULL, 0, NULL, NULL);
	//char* 메모리 할당
	pStr = new char[strSize];

	//형 변환 
	WideCharToMultiByte(CP_ACP, 0, str, -1, pStr, strSize, 0, 0);
	return pStr;
}
static HRESULT VideoCapture(string &Devname, IBaseFilter ** ppSrcFilter)
{	
	HRESULT hr;
	IMoniker* pMoniker = NULL;
	ICreateDevEnum *pDevEnum = NULL;
	IEnumMoniker *pEnum = NULL;

	hr = CoCreateInstance(CLSID_VideoInputDeviceCategory, NULL, CLSCTX_INPROC,
		IID_ICreateDevEnum, (void **)&pDevEnum);
	if (SUCCEEDED(hr))
	{
		// Create an enumerator for the category.
		hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
		if (SUCCEEDED(hr)){
			// cam setting
			while ((hr = pEnum->Next(1, &pMoniker, NULL)) == S_OK) {
				IPropertyBag *pPropBag;
				hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag,
					(void **)&pPropBag);
				if (SUCCEEDED(hr))
				{
					// To retrieve the filter's friendly name, do the following:
					VARIANT varName;
					VariantInit(&varName);
					hr = pPropBag->Read(L"FriendlyName", &varName, 0);
					if (SUCCEEDED(hr))
					{
						// Display the name in your UI somehow.
						printf("11111%S\n", varName.bstrVal);
						char* name = ConvertWCtoC(varName.bstrVal);
						if (name == Devname) {
							VariantClear(&varName);

							//To create an instance of the filter, do the following:
							IBaseFilter *pFilter;
							hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter,
								(void**)ppSrcFilter);

							pDevEnum->Release();
							pEnum->Release();
							pMoniker->Release();
							pPropBag->Release();

							return hr;
						}
					}
					VariantClear(&varName);
				}
				pMoniker->Release();
				pPropBag->Release();
			}
		}
	}
	pDevEnum->Release();
	pEnum->Release();
	return hr;
}
static HRESULT GetInterfaces()
{
	HRESULT hr;
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	hr = CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void **)&pNullRenderer);

	// Create Filtergraph
	hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
		IID_IGraphBuilder, (void **)&pGraph);

	// Create CaptureGraphBuilder2
	hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL,
		CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2,
		(void **)&pCapture);

	//Create SampleGrabber
	hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (LPVOID *)&pSgrabberF);

	// Videowindow

	// Get interfaces
	hr = pSgrabberF->QueryInterface(IID_ISampleGrabber, (LPVOID *)&pSampleGrabber);
	hr = pGraph->QueryInterface(IID_IMediaControl, (void **)&pMC);
	hr = pGraph->QueryInterface(IID_IMediaEvent, (void **)&pME);
	hr = pGraph->QueryInterface(IID_IVideoWindow, (void **)&pVW);
	                  
	return hr;
}
static void ReleaseInterfaces() 
{
	pVW->Release();
	pME->Release();
	pNullRenderer->Release();
}

static HRESULT Read(IBaseFilter *pSrc)
//int main()
{
	HRESULT hr;
	// GLOBAL POINTER

	IBaseFilter *pSrcFilter = NULL;

	// Capture
	IBaseFilter *pMux = NULL;

	IPin *pSrcPin = NULL;
	IPin *pGrabPin = NULL;
	AM_MEDIA_TYPE am_media_type;

	hr = pGraph->AddFilter(pSrc, L"Video Capture");
	hr = pGraph->AddFilter(pSgrabberF, L"Sample Grabber");

	hr = pGraph->Connect(pSrcPin, pGrabPin);

	// Renderstream methods ( Capture )
	hr = pCapture->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, pSrc, NULL, NULL);

	// SampleGrabber
	ZeroMemory(&am_media_type, sizeof(am_media_type));
	am_media_type.majortype = MEDIATYPE_Video;
	am_media_type.subtype = MEDIASUBTYPE_RGB24;
	am_media_type.formattype = FORMAT_VideoInfo;
	pSampleGrabber->SetMediaType(&am_media_type);

	// Get connection information.
	// This must be done after the Graph is created
	// by RenderFile.
	hr = pSampleGrabber->GetConnectedMediaType(&am_media_type);
	VIDEOINFOHEADER *pVideoInfoHeader =
		(VIDEOINFOHEADER *)am_media_type.pbFormat;

	// Print the width and height of the image.
	// This is just to make the sample understandable.
	// This is not a required feature.
	printf("size = %dx%d\n",
		pVideoInfoHeader->bmiHeader.biWidth,
		pVideoInfoHeader->bmiHeader.biHeight);

	// Print the data size.
	// This is just for understanding too.
	printf("sample size = %d\n",
		am_media_type.lSampleSize);

	// Configure SampleGrabber to do grabbing.
	// Buffer data can not be obtained if you
	// do not use SetBufferSamples.
	// You can use SetBufferSamples after Run() too.
	hr = pSampleGrabber->SetBufferSamples(TRUE);

	// VideoWindow Setting
	//http://telnet.or.kr/directx/htm/ivideowindowinterface.htm
	pVW->put_AutoShow(OAFALSE);
	//pVW->SetWindowPosition(0, 0, 0, 0);

	// Start playing
	pMC->Run();

	// Block execution
	MessageBox(NULL,
		"Block Execution",
		"Block",
		MB_OK);

	//Sleep(10000);

	// JPG will be get after "OK" or "1000msec" is pressed

	// prepare buffer
	long nBufferSize = am_media_type.lSampleSize;
	//long *pBuffer = (long *)malloc(nBufferSize); // use malloc
	vector<long> pBuffer(nBufferSize);

	// grab image data.
	//hr = pSampleGrabber->GetCurrentBuffer(&nBufferSize, pBuffer); // use malloc
	hr = pSampleGrabber->GetCurrentBuffer(&nBufferSize, pBuffer.data());

	// Save image data as JPG.
	// This is just to make this sample easily understandable.
	//
	HANDLE fh;
	DWORD nWritten;

	char filename[12];
	sprintf(filename, "result.jpg");

	// save Format
	fh = CreateFile(filename,
		GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	// JPG
	//WriteFile(fh, pBuffer, nBufferSize, &nWritten, NULL); // use malloc
	WriteFile(fh, pBuffer.data(), nBufferSize, &nWritten, NULL);

	//free(pBuffer); // use malloc

	// Release
	pSampleGrabber->Release();
	pSgrabberF->Release();
	pMC->Release();
	pGraph->Release();
	pCapture->Release();

	// finish COM
	CoUninitialize();

	return hr;
}

//int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hInstP, LPSTR lpCmdLine, int nCmdShow)
int main()
{
	HRESULT hr;

	hr = GetInterfaces();

	// Initialize the Capture Graph Builder
	hr = pCapture->SetFiltergraph(pGraph);

	vector<string> names = { "UVC Camera", "ABKO APC930 QHD WEBCAM" };
	/*vector<string> names;
	if (SUCCEEDED(hr)) {
		DisplayDeviceInformation(names);
	}
	for (int i = 0; i < names.size(); i++) {
		printf("%d : %s\n", i, names[i].c_str());
	}
	*/

	IBaseFilter *pSrc = NULL;
	hr = VideoCapture(names[1], &pSrc);
	hr = Read(pSrc);

	ReleaseInterfaces();
}
