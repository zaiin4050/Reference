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

#include <string>
#include <iostream>
#include <time.h>
#include <chrono>
#include <sstream>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Windowscodecs.lib")

using namespace std;
using namespace chrono;

// GLOBAL POINTER

IGraphBuilder * pGraph = NULL;
ICaptureGraphBuilder2 * pCapture = NULL;
IBaseFilter *pSrc = NULL;
IVideoWindow  * pVW = NULL;
IMediaControl * pMC = NULL;
IMediaEventEx * pME = NULL;

// Enumerator
IMoniker* pMoniker = NULL;
ICreateDevEnum *pDevEnum = NULL;
IEnumMoniker *pClassEnum = NULL;

IBaseFilter *pSrcFilter = NULL;

// Capture
IBaseFilter *pMux = NULL;

// NullRender
IBaseFilter *pNullRenderer = NULL;

// Added for the SampleGrabber
ISampleGrabber *pSampleGrabber = NULL;
IBaseFilter *pSgrabberF = NULL;
IPin *pSrcPin = NULL;
IPin *pGrabPin = NULL;
AM_MEDIA_TYPE am_media_type;

const std::string currentDateTime() {
	time_t raw_time;
	struct tm* tm;
	raw_time = time(NULL);

	tm = localtime(&raw_time);

	string year = to_string(tm->tm_year + 1900);
	string mon = to_string(tm->tm_mon + 1);
	string day = to_string(tm->tm_mday);
	string hour = to_string(tm->tm_hour);
	string mint = to_string(tm->tm_min);
	string sec = to_string(tm->tm_sec);

	if (mon.length() < 2) {
		mon = "0" + mon;
	}
	if (day.length() < 2) {
		day = "0" + day;
	}
	if (hour.length() < 2) {
		hour = "0" + hour;
	}
	if (mint.length() < 2) {
		mint = "0" + mint;
	}
	if (sec.length() < 2) {
		sec = "0" + sec;
	}

	string buf = year + "_" + mon + day + "_" + hour + mint + sec;

	return buf;
}

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
//static HRESULT SettingCam(IBaseFilter ** ppSrcFilter)
static HRESULT FindDevice(vector<string> &names)
{
	IEnumMoniker *pEnum;

	HRESULT hr = S_OK;
	IBaseFilter * pSrc = NULL;
	IMoniker* pMoniker = NULL;
	ICreateDevEnum *pDevEnum = NULL;
	IEnumMoniker *pClassEnum = NULL;

	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
		IID_ICreateDevEnum, (void **)&pDevEnum);

	//vector<string> names;

	if (SUCCEEDED(hr))
	{
		hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pClassEnum, 0);
		if (SUCCEEDED(hr)) {
			ULONG cFetched;
			int num = 1;
			while (pClassEnum->Next(1, &pMoniker, &cFetched) == S_OK)
			{
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
						//printf("%d : %S\n", num, varName.bstrVal);
						char* name = ConvertWCtoC(varName.bstrVal);
						names.push_back(name);
						delete[] name;
						VariantClear(&varName);
						/*++num;*/
					}
					//To create an instance of the filter, do the following:
					IBaseFilter *pSrc;
					hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter,
						(void**)&pSrc);
				}
				pMoniker->Release();
				pPropBag->Release();
			}
			pClassEnum->Release();
			pDevEnum->Release();
		}
		return hr;
	}
}

//	if (SUCCEEDED(hr))
//	{
//		hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pClassEnum, 0);
//		hr = pClassEnum->Next(1, &pMoniker, NULL); // 마지막 카메라
//		pMoniker->Release();
//		hr = pClassEnum->Next(1, &pMoniker, NULL); // 그 앞의 카메라
//		hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&pSrc);
//
//		if (hr == S_FALSE)
//		{
//			//Msg(TEXT("Unable to access video capture device!"));
//			hr = E_FAIL;
//		}
//	}
//	if (SUCCEEDED(hr))
//	{
//		*ppSrcFilter = pSrc;
//		(*ppSrcFilter)->AddRef();
//	}
//	pSrc->Release();
//	pMoniker->Release();
//	pDevEnum->Release();
//	pClassEnum->Release();
//
//	return hr;
//}

static HRESULT Run(int cam_num, int total_num)
//int main()
{
	HRESULT hr;


	// NullRender
	hr = CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void **)&pNullRenderer);

	// Create Filtergraph
	hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
		IID_IGraphBuilder, (void **)&pGraph);

	// Create CaptureGraphBuilder2
	hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL,
		CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2,
		(void **)&pCapture);

	// Initialize the Capture Graph Builder
	hr = pCapture->SetFiltergraph(pGraph);

	//Create SampleGrabber
	hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (LPVOID *)&pSgrabberF);

	// Get interfaces
	hr = pSgrabberF->QueryInterface(IID_ISampleGrabber, (LPVOID *)&pSampleGrabber);
	hr = pGraph->QueryInterface(IID_IMediaControl, (void **)&pMC);
	hr = pGraph->QueryInterface(IID_IMediaEvent, (void **)&pME);
	hr = pGraph->QueryInterface(IID_IVideoWindow, (void **)&pVW);

	// Find video device
	//hr = SettingCam(&pSrc);

	// create enumerator
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
		IID_ICreateDevEnum, (void **)&pDevEnum);
	hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pClassEnum, 0);

	// cam setting
	int loop = total_num - cam_num;
	for (int i = 0; i < loop; i++) {
		hr = pClassEnum->Next(1, &pMoniker, NULL);
		pMoniker->Release();
	}
	hr = pClassEnum->Next(1, &pMoniker, NULL);

	hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void **)&pSrc);

	hr = pGraph->AddFilter(pSrc, L"Video Capture");
	hr = pGraph->AddFilter(pSgrabberF, L"Sample Grabber");
	hr = pGraph->AddFilter(pNullRenderer, L"Null Renderer");

	hr = pGraph->Connect(pSrcPin, pGrabPin);

	// Renderstream methods ( Capture )
	hr = pCapture->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, pSrc, NULL, NULL);

	//SampleGrabber
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

	//// Print the width and height of the image.
	//// This is just to make the sample understandable.
	//// This is not a required feature.
	printf("size = %dx%d\n",
		pVideoInfoHeader->bmiHeader.biWidth,
		pVideoInfoHeader->bmiHeader.biHeight);

	//// Print the data size.
	//// This is just for understanding too.
	printf("sample size = %d\n",
		am_media_type.lSampleSize);

	//// Configure SampleGrabber to do grabbing.
	//// Buffer data can not be obtained if you
	//// do not use SetBufferSamples.
	//// You can use SetBufferSamples after Run() too.
	hr = pSampleGrabber->SetBufferSamples(TRUE);

	pVW->put_Height(648);
	pVW->put_Width(1152);

	// Start playing
	pMC->Run();

	// Block execution
	//MessageBox(NULL,
	//	"Block Execution",
	//	"Block",
	//	MB_OK);
	return hr;
}
HRESULT VideoCapture(int idx, vector<uint8_t> pBuffer, long nBufferSize)
{
	HRESULT hr;

	string save_time = currentDateTime();

	stringstream ss;
	ss << save_time << "_" << idx << ".jpg";

	// JPG will be get after "OK" or "1000msec" is pressed

	// grab image data.
	//hr = pSampleGrabber->GetCurrentBuffer(&nBufferSize, pBuffer); // use malloc
	hr = pSampleGrabber->GetCurrentBuffer(&nBufferSize, (long*)pBuffer.data());

	// Save image data as JPG.
	// This is just to make this sample easily understandable.
	//
	HANDLE fh;
	DWORD nWritten;

	string filename = ss.str();
	/*char filename[15];
	sprintf(filename, "result", "%d", cam_num, ".jpg");*/

	// save Format
	fh = CreateFile(filename.c_str(),
		GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	// JPG
	//WriteFile(fh, pBuffer, nBufferSize, &nWritten, NULL); // use malloc
	WriteFile(fh, pBuffer.data(), nBufferSize, &nWritten, NULL);

	//free(pBuffer); // use malloc

	printf("save %d\n", idx);

	return hr;
}

int main()
{
	HRESULT hr;
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	vector<string> names;
	hr = FindDevice(names);

	for (int i = 0; i < names.size(); i++) {
		printf("%d : %s\n",i, names[i].c_str());
	}
	
	int total = names.size();

	int cam;
	cin >> cam;

	Run(cam, total);

	// prepare buffer
	long nBufferSize = 1920*1080*3;
	//long *pBuffer = (long *)malloc(nBufferSize); // use malloc
	vector<uint8_t> pBuffer(nBufferSize);

	string esc;
	int idx = 0;
	while (1) {
		//Block execution
		MessageBox(NULL,
		"Block Execution",
		"Block",
		MB_OK);

		hr = VideoCapture(idx, pBuffer, nBufferSize);
		idx += 1;
	}
	

	// Release
	pSampleGrabber->Release();
	pSgrabberF->Release();
	pMC->Release();
	pGraph->Release();

	// finish COM
	CoUninitialize();

}
