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
vector <ICaptureGraphBuilder2 * > pCapture(4);

// Added for the SampleGrabber
vector <ISampleGrabber *> pSampleGrabber(4);
vector <IBaseFilter *> pSgrabberF(4);

vector<IVideoWindow  *> pVW(4);
vector <IMediaControl *> pMC(4);
vector < IMediaEventEx *> pME(4);
AM_MEDIA_TYPE am_media_type;

int height = 1080;
int width = 1920;

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

	}
	return hr;
}
static void ReleaseInterfaces(int cam_num)
{	
	for (int i = 0; i < cam_num; i++) {
		pMC[i]->Release();
		pVW[i]->Release();
		pME[i]->Release();
		pSampleGrabber[i]->Release();
		pSgrabberF[i]->Release();
		pGraph[i]->Release();
	}
}
static HRESULT AddSampleGrabber(vector<IBaseFilter *> pSrc)
{
	HRESULT hr;
	for (int i = 0; i < pSrc.size(); i++) {
		//// cam resolution setting ( not resize -> crop)
		//AM_MEDIA_TYPE *mt;
		//IAMStreamConfig *pConfig = NULL;
		//hr = pCapture[i]->FindInterface(&PIN_CATEGORY_CAPTURE, 0, pSrc[i], IID_IAMStreamConfig, (void**)&pConfig);
		//pConfig->GetFormat(&mt);
		//VIDEOINFOHEADER *pVih = (VIDEOINFOHEADER*)mt->pbFormat;
		//pVih->bmiHeader.biSizeImage = DIBSIZE(pVih->bmiHeader);
		//pVih->bmiHeader.biWidth = width;
		//pVih->bmiHeader.biHeight = height;
		////pVih->AvgTimePerFrame = 1200000;
		//pConfig->SetFormat(mt);

		//pConfig->Release();

		// Initialize the Capture Graph Builder
		hr = pCapture[i]->SetFiltergraph(pGraph[i]);

		hr = pGraph[i]->AddFilter(pSrc[i], L"Video Capture");
		hr = pGraph[i]->AddFilter(pSgrabberF[i], L"Sample Grabber");

		// Renderstream methods ( Capture )
		hr = pCapture[i]->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, pSrc[i], NULL, NULL);

		if (SUCCEEDED(hr)) {			
			hr = pSampleGrabber[i]->GetConnectedMediaType(&am_media_type);
			VIDEOINFOHEADER *pVideoHeader =
				(VIDEOINFOHEADER *)am_media_type.pbFormat;
			// Print the width and height of the image.
			// This is just to make the sample understandable.
			// This is not a required feature.
			//printf("size = %dx%d\n",
			//	pVideoHeader->bmiHeader.biWidth,
			//	pVideoHeader->bmiHeader.biHeight);

			//Print the data size.
			//This is just for understanding too.
			//printf("sample size = %d\n",
			//	am_media_type.lSampleSize);

			// Configure SampleGrabber to do grabbing.
			// Buffer data can not be obtained if you
			// do not use SetBufferSamples.
			// You can use SetBufferSamples after Run() too.
			hr = pSampleGrabber[i]->SetBufferSamples(TRUE);
		}
	}
	return hr;
}
static HRESULT Run(int cam_num)
{
	HRESULT hr;

	// Start playing
	for (int i = 0; i < cam_num; i++) {
		// VideoWindow Setting
		// http://telnet.or.kr/directx/htm/ivideowindowinterface
		//pVW[i]->SetWindowPosition(0, 0, 0, 0);
		//pVW[i]->put_Height(480);
		//pVW[i]->put_Width(640);
		pVW[i]->put_AutoShow(OAFALSE);
		
		//--------------------------------------
		//printf("%d RUN\n", i);
		hr = pMC[i]->Run();	// run while program die
		if (FAILED(hr)) {
			return hr;
		}
	}
	// skipframe
	Sleep(1000);

	return hr;
}
static HRESULT Read(vector<vector<long>> &pBuffer, int idx)
//int main()
{
	HRESULT hr;
	// prepare buffer
	for (int i = 0; i < pBuffer.size(); i++) {
		//printf("%d READ\n", i);
		long nBufferSize =width* height *3;
		vector<long> Buf(nBufferSize);
		//vector<long> pBuf(nBufferSize);
		// grab image data.
		//hr = pSampleGrabber->GetCurrentBuffer(&nBufferSize, pBuffer); // use malloc
		hr = pSampleGrabber[i]->GetCurrentBuffer(&nBufferSize, Buf.data());
		
		// Save image data as JPG.
		// This is just to make this sample easily understandable.
		//
		HANDLE fh;
		DWORD nWritten;

		char filename[15];

		sprintf(filename, "result_%d_%d.jpg", idx, i);

		// save Format
		fh = CreateFile(filename,
			GENERIC_WRITE, 0, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		// JPG
		//WriteFile(fh, pBuffer, nBufferSize, &nWritten, NULL); // use malloc
		WriteFile(fh, Buf.data(), nBufferSize, &nWritten, NULL);

		//pBuffer.push_back(pBuf);
		//free(pBuffer); // use malloc
		int ImageWidth = width;
		int ImageHeight = height;
		int DecodedBufferSize = ImageWidth * ImageHeight * 3;
		Win32DecodeJpeg(nBufferSize, Buf.data(), DecodedBufferSize, pBuffer[i].data());

		//char filename[20];

		sprintf(filename, "result_%d_%d.bin", idx, i);

		ofstream ofs(filename, ios::binary);
		ofs.write((char*)pBuffer[i].data(), DecodedBufferSize);
		ofs.close();
		
		// Release

	}
	// finish COM
	//CoUninitialize();

	return hr;
}

int main()
{
	HRESULT hr;
	//int time = atoi(argv[1]);

	vector<string> names = { "UVC Camera", "HD USB Camera" };   // { "HANA_1001", "HANA_1002", "HANA_1003", "HANA_1004" }; // , "ABKO APC930 QHD WEBCAM", 

	int cam_num = names.size();
	hr = GetInterfaces(cam_num);
	if (SUCCEEDED(hr)) {
		vector<IBaseFilter *> pSrc(cam_num);

		for (int i = 0; i < names.size(); i++) {
			hr = VideoCapture(names[i], &pSrc[i]);
			//printf("%d VideoCapture\n", i);
		}

		hr = AddSampleGrabber(pSrc);
		long nBufferSize = height * width * 3;
		vector<vector<long>> pBuffer(cam_num, vector<long>(nBufferSize));
		hr = Run(cam_num);
		
		// capture 10
		if (SUCCEEDED(hr)) {
			for (int i = 0; i < 10; i++) {
				hr = Read(pBuffer, i);

			}
		}	
	}
	ReleaseInterfaces(cam_num);
}
