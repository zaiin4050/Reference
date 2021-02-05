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

using namespace std;

// NullRender
IBaseFilter *pNullRenderer = NULL;

// Added for the ICaptureGraphBuilder2
vector <IGraphBuilder *> pGraph(4);
vector <ICaptureGraphBuilder2 * > pCapture(2);

// Added for the SampleGrabber
vector <ISampleGrabber *> pSampleGrabber(2);
vector <IBaseFilter *> pSgrabberF(2);

vector<IVideoWindow  *> pVW(2);
vector <IMediaControl *> pMC(2);
vector < IMediaEventEx *> pME(2);
AM_MEDIA_TYPE am_media_type;

static void Win32DecodeJpeg(unsigned int ImageDataSize, void *ImageData,
	unsigned int DestSize, void *Dest)
{
	// IWICImagingFactory is a structure containing the function pointers of the WIC API
	static IWICImagingFactory *IWICFactory;
	if (IWICFactory == NULL)
	{
		/*if (CoInitializeEx(NULL, COINIT_MULTITHREADED) != S_OK)
		{
			printf("failed to initialize the COM library\n");
			return;
		}*/

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
						char* name = ConvertWCtoC(varName.bstrVal);
						if (name == Devname) {
							printf("%s\n", name);
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
static HRESULT GetInterfaces(int Cam_num)
{
	HRESULT hr;
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	// Create Filtergraph
	for (int i = 0; i < Cam_num; i++) {
		hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
			IID_IGraphBuilder, (void **)&pGraph[i]);

		// Create CaptureGraphBuilder2
		hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL,
			CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2,
			(void **)&pCapture[i]);

		//Create SampleGrabber
		hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
			IID_IBaseFilter, (LPVOID *)&pSgrabberF[i]);

		// Videowindow

		// Get interfaces
		hr = pSgrabberF[i]->QueryInterface(IID_ISampleGrabber, (LPVOID *)&pSampleGrabber[i]);
		hr = pGraph[i]->QueryInterface(IID_IMediaControl, (void **)&pMC[i]);
		hr = pGraph[i]->QueryInterface(IID_IMediaEvent, (void **)&pME[i]);
		hr = pGraph[i]->QueryInterface(IID_IVideoWindow, (void **)&pVW[i]);

		// Initialize the Capture Graph Builder
		hr = pCapture[i]->SetFiltergraph(pGraph[i]);
	}
	return hr;
}
static void ReleaseInterfaces() 
{	
	for (int i = 0; i < pMC.size(); i++) {
		pMC[i]->Release();
		pVW[i]->Release();
		pME[i]->Release();
	}
}
static HRESULT AddSampleGrabber(vector<IBaseFilter *> pSrc)
{
	HRESULT hr;
	for (int i = 0; i < pSrc.size(); i++) {
		hr = pGraph[i]->AddFilter(pSrc[i], L"Video Capture");
		hr = pGraph[i]->AddFilter(pSgrabberF[i], L"Sample Grabber");

		// Renderstream methods ( Capture )
		hr = pCapture[i]->RenderStream(&PIN_CATEGORY_CAPTURE, 0, pSrc[i], NULL, NULL);
		if (SUCCEEDED(hr)) {
			// SampleGrabber
			ZeroMemory(&am_media_type, sizeof(am_media_type));
			am_media_type.majortype = MEDIATYPE_Video;
			am_media_type.subtype = MEDIASUBTYPE_RGB24;
			am_media_type.formattype = FORMAT_VideoInfo;
			pSampleGrabber[i]->SetMediaType(&am_media_type);

			// Get connection information.
			// This must be done after the Graph is created
			// by RenderFile.
			hr = pSampleGrabber[i]->GetConnectedMediaType(&am_media_type);
			//VIDEOINFOHEADER *pVideoInfoHeader =
			//	(VIDEOINFOHEADER *)am_media_type.pbFormat;

			// Print the width and height of the image.
			// This is just to make the sample understandable.
			// This is not a required feature.
			//printf("size = %dx%d\n",
			//	pVideoInfoHeader->bmiHeader.biWidth,
			//	pVideoInfoHeader->bmiHeader.biHeight);

			// Print the data size.
			// This is just for understanding too.
			//printf("sample size = %d\n",
			//	am_media_type.lSampleSize);

			// Configure SampleGrabber to do grabbing.
			// Buffer data can not be obtained if you
			// do not use SetBufferSamples.
			// You can use SetBufferSamples after Run() too.
			hr = pSampleGrabber[i]->SetBufferSamples(TRUE);
			printf("%s\n", "addsamplegrabber");
		}
	}
	return hr;
}
static HRESULT Run()
{
	HRESULT hr;

	//--------------------------------------
	// VideoWindow Setting
	//http://telnet.or.kr/directx/htm/ivideowindowinterface.htm
	//pVW->put_AutoShow(OAFALSE);
	//pVW->SetWindowPosition(0, 0, 0, 0);

	// Start playing
	for (int i = 0; i < pMC.size(); i++) {
		hr = pMC[i]->Run();
		if (FAILED(hr)) {
			return hr;
		}
	}

	//Block execution
	MessageBox(NULL,
		"Block Execution",
		"Block",
		MB_OK);

	//Sleep(10000);

	// JPG will be get after "OK" or "1000msec" is pressed
	return hr;
}
static HRESULT Read(vector<vector<long>> &pBuffer)
//int main()
{
	HRESULT hr;
	// prepare buffer
	for (int i = 0; i < pSampleGrabber.size(); i++) {
		long nBufferSize = 1920 * 1080 * 3;
		vector<long> Buf(nBufferSize);
		//vector<long> pBuf(nBufferSize);
		// grab image data.
		//hr = pSampleGrabber->GetCurrentBuffer(&nBufferSize, pBuffer); // use malloc
		hr = pSampleGrabber[i]->GetCurrentBuffer(&nBufferSize, Buf.data());
		if (SUCCEEDED(hr)) {
			// Save image data as JPG.
		// This is just to make this sample easily understandable.
		//
		//HANDLE fh;
		//DWORD nWritten;

		//char filename[15];

		//sprintf(filename, "result_%d.jpg", i);

		// save Format
		//fh = CreateFile(filename,
		//	GENERIC_WRITE, 0, NULL,
		//	CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		// JPG
		//WriteFile(fh, pBuffer, nBufferSize, &nWritten, NULL); // use malloc
		//WriteFile(fh, pBuffer[i].data(), nBufferSize, &nWritten, NULL);

		//pBuffer.push_back(pBuf);
		//free(pBuffer); // use malloc
			int ImageWidth = 1920;
			int ImageHeight = 1080;
			int DecodedBufferSize = ImageWidth * ImageHeight * 3;
			Win32DecodeJpeg(nBufferSize, Buf.data(), DecodedBufferSize, pBuffer[i].data());

			char filename[15];

			sprintf(filename, "result_%d.bin", i);

			ofstream ofs(filename, ios::binary);
			ofs.write((char*)pBuffer[i].data(), DecodedBufferSize);
			ofs.close();
		}
		
		// Release
		pSampleGrabber[i]->Release();
		pSgrabberF[i]->Release();
		pGraph[i]->Release();
	}
	// finish COM
	//CoUninitialize();

	return hr;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hInstP, LPSTR lpCmdLine, int nCmdShow)
//int main()
{
	HRESULT hr;

	vector<string> names = { "UVC Camera", "HD USB Camera" }; // , "ABKO APC930 QHD WEBCAM"

	int cam_num = names.size();
	hr = GetInterfaces(cam_num);
	if (SUCCEEDED(hr)) {
		vector<IBaseFilter *> pSrc(cam_num);

		for (int i = 0; i < names.size(); i++) {
			hr = VideoCapture(names[i], &pSrc[i]);
		}

		hr = AddSampleGrabber(pSrc);
		if (SUCCEEDED(hr)) {
			long nBufferSize = 1920 * 1080 * 3;
			vector<vector<long>> pBuffer(cam_num, vector<long>(nBufferSize));

			hr = Run();

			hr = Read(pBuffer);
		}	
	}
	ReleaseInterfaces();
}
