#include <stdio.h>

#include <dshow.h>
#include "qedit.h" // for SampleGrabber

#include <atlconv.h>

BSTR bstrStr = SysAllocString(L"E:\\BTS.mpg"); // const char ro BSTR
#define	FILENAME bstrStr

int
main()
{
	IGraphBuilder *pGraphBuilder;
	IMediaControl *pMediaControl;

	IBaseFilter *pSampleGrabberFilter;
	ISampleGrabber *pSampleGrabber;
	AM_MEDIA_TYPE am_media_type;

	// initialize COM
	CoInitialize(NULL);

	// create FilterGraph
	CoCreateInstance(CLSID_FilterGraph,
		NULL,
		CLSCTX_INPROC,
		IID_IGraphBuilder,
		(LPVOID *)&pGraphBuilder);

	// create SampleGrabber(Filter)
	CoCreateInstance(CLSID_SampleGrabber,
		NULL,
		CLSCTX_INPROC,
		IID_IBaseFilter,
		(LPVOID *)&pSampleGrabberFilter);

	// get ISampleGrabber interface from the Filter
	pSampleGrabberFilter->QueryInterface(IID_ISampleGrabber,
		(LPVOID *)&pSampleGrabber);

	// determine the format for connecting SampleGrabber.
	// You can configure the SampleGrabber insertion place
	// by changing the values in this structure.
	// If you use the values in this sample,
	// you can get the video frame data right before
	// it is displayed.
	ZeroMemory(&am_media_type, sizeof(am_media_type));
	am_media_type.majortype = MEDIATYPE_Video;
	am_media_type.subtype = MEDIASUBTYPE_RGB24;
	am_media_type.formattype = FORMAT_VideoInfo;
	pSampleGrabber->SetMediaType(&am_media_type);

	// add SampleGrabber Filter to the Graph
	pGraphBuilder->AddFilter(pSampleGrabberFilter,
		L"Sample Grabber");

	// get MediaControl
	pGraphBuilder->QueryInterface(IID_IMediaControl,
		(LPVOID *)&pMediaControl);

	// create Graph.
	// Graph that contains SampleGrabber
	// will be created automatically.
	pMediaControl->RenderFile(FILENAME);

	// Get connection information.
	// This must be done after the Graph is created
	// by RenderFile.
	pSampleGrabber->GetConnectedMediaType(&am_media_type);
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
	pSampleGrabber->SetBufferSamples(TRUE);

	// Start playing
	pMediaControl->Run();

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
	pSampleGrabber->GetCurrentBuffer(&nBufferSize,
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

	fh = CreateFile("result.bmp",
		GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	WriteFile(fh, &bmphdr, sizeof(bmphdr), &nWritten, NULL);
	WriteFile(fh,
		&pVideoInfoHeader->bmiHeader,
		sizeof(BITMAPINFOHEADER), &nWritten, NULL);
	WriteFile(fh, pBuffer, nBufferSize, &nWritten, NULL);
	CloseHandle(fh);

	free(pBuffer);

	// Release
	pSampleGrabber->Release();
	pSampleGrabberFilter->Release();
	pMediaControl->Release();
	pGraphBuilder->Release();

	// finish COM
	CoUninitialize();

	return 0;
}
