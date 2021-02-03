#include <stdio.h>

#include <dshow.h>
#include "qedit.h" // for SampleGrabber

#include <atlconv.h>

int main()
{
	HRESULT hr;

	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
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

	// Capture
	IBaseFilter *pMux = NULL;

	// Added for the SampleGrabber
	ISampleGrabber *pSampleGrabber = NULL;
	IBaseFilter *pSgrabberF = NULL;
	IPin *pSrcPin = NULL;
	IPin *pGrabPin = NULL;
	AM_MEDIA_TYPE am_media_type;

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

	// create enumerator
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
		IID_ICreateDevEnum, (void **)&pDevEnum);

	hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pClassEnum, 0);
	hr = pClassEnum->Next(1, &pMoniker, NULL);
	hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void **)&pSrc);

	hr = pGraph->AddFilter(pSrc, L"Video Capture");
	hr = pGraph->AddFilter(pSgrabberF, L"Sample Grabber");

	/*pSrcPin = GetOutPin(pSrc, PINDIR_OUTPUT, 0);
	pGrabPin = GetInPin(pSgrabberF, PINDIR_INPUT, 0);*/

	hr = pGraph->Connect(pSrcPin, pGrabPin);

	// Renderstream methods ( Preview )
	hr = pCapture->RenderStream(&PIN_CATEGORY_PREVIEW, &MEDIATYPE_Video, pSrc, NULL, NULL);

	// Renderstream methods ( Capture )
	//hr = pCapture->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, pSrc, NULL, NULL);

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

	//	// Configure SampleGrabber to do grabbing.
//	// Buffer data can not be obtained if you
//	// do not use SetBufferSamples.
//	// You can use SetBufferSamples after Run() too.
	hr = pSampleGrabber->SetBufferSamples(TRUE);

	// Start playing
	pMC->Run();

	// Block execution
	MessageBox(NULL,
		"Block Execution",
		"Block",
		MB_OK);

	// BITMAP will be saved after OK is pressed

	// prepare buffer
	long nBufferSize = am_media_type.lSampleSize;
	long *pBuffer = (long *)malloc(nBufferSize);

	// grab image data.
	hr = pSampleGrabber->GetCurrentBuffer(&nBufferSize,
		pBuffer);

	//
	// Save image data as Bitmap.
	// This is just to make this sample easily understandable.
	//
	HANDLE fh;
	BITMAPFILEHEADER bmphdr;
	DWORD nWritten;

	memset(&bmphdr, 0, sizeof(bmphdr));

	bmphdr.bfType = ('M' << 8) | 'B';
	bmphdr.bfSize = sizeof(bmphdr) + sizeof(BITMAPINFOHEADER) + nBufferSize;
	bmphdr.bfOffBits = sizeof(bmphdr) + sizeof(BITMAPINFOHEADER);

	fh = CreateFile("result222.jpg",
		GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	//WriteFile(fh, &bmphdr, sizeof(bmphdr), &nWritten, NULL);
	//WriteFile(fh,
	//	&pVideoInfoHeader->bmiHeader,
	//	sizeof(BITMAPINFOHEADER), &nWritten, NULL);
	//WriteFile(fh, pBuffer, nBufferSize, &nWritten, NULL);
	//CloseHandle(fh);

	WriteFile(fh, pBuffer, nBufferSize, &nWritten, NULL);


	free(pBuffer);

	// Release
	pSampleGrabber->Release();
	pSgrabberF->Release();
	pMC->Release();
	pGraph->Release();

	// finish COM
	CoUninitialize();

	return 0;
}
