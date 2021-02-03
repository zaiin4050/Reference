#define _CRT_SECURE_NO_WARNINGS
#include <Wincodec.h>
#include <stdio.h>
#include <fstream>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Windowscodecs.lib")

void
Win32DecodeJpeg(unsigned int ImageDataSize, void *ImageData,
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
	
using namespace std;

int
	main()
{
	int ImageWidth = 1920;
	int ImageHeight = 1080;

	int DecodedBufferSize = ImageWidth * ImageHeight * 3;
	void *DecodedBuffer = malloc(DecodedBufferSize);

	FILE *ImageFile = fopen("result222.jpg", "rb");

	fseek(ImageFile, 0, SEEK_END);
	int ImageSize = ftell(ImageFile);
	rewind(ImageFile);

	void *ImageData = malloc(ImageSize);
	fread(ImageData, 1, ImageSize, ImageFile);

	fclose(ImageFile);

	Win32DecodeJpeg(ImageSize, ImageData, DecodedBufferSize, DecodedBuffer);

	ofstream ofs("result222.bin", ios::binary);
	ofs.write((char*)DecodedBuffer, DecodedBufferSize);
	ofs.close();
}
