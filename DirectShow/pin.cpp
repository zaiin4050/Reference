#include <stdio.h>

#include <dshow.h>
#include "qedit.h" // for SampleGrabber
#include <Windows.h>
#include <atlconv.h>
HRESULT GetPin(IBaseFilter *pFilter, PIN_DIRECTION PinDir, IPin **ppPin)
{
	IEnumPins  *pEnum = NULL;
	IPin       *pPin = NULL;
	HRESULT    hr;

	if (ppPin == NULL)
	{
		return E_POINTER;
	}

	hr = pFilter->EnumPins(&pEnum);
	if (FAILED(hr))
	{
		return hr;
	}
	while (pEnum->Next(1, &pPin, 0) == S_OK)
	{
		PIN_DIRECTION PinDirThis;
		hr = pPin->QueryDirection(&PinDirThis);
		if (FAILED(hr))
		{
			pPin->Release();
			pEnum->Release();
			return hr;
		}
		if (PinDir == PinDirThis)
		{
			// Found a match. Return the IPin pointer to the caller.
			*ppPin = pPin;
			pEnum->Release();
			return S_OK;
		}
		// Release the pin for the next time through the loop.
		pPin->Release();
	}
	// No more pins. We did not find a match.
	pEnum->Release();
	return E_FAIL;
}

HRESULT GetUnconnectedPin(
	IBaseFilter *pFilter,   // 필터의 포인터.
	PIN_DIRECTION PinDir,   // 검색하는 핀의 방향.
	IPin **ppPin)           // 핀의 포인터를 받는다.
{
	*ppPin = 0;
	IEnumPins *pEnum = 0;
	IPin *pPin = 0;
	HRESULT hr = pFilter->EnumPins(&pEnum);
	if (FAILED(hr))
	{
		return hr;
	}
	while (pEnum->Next(1, &pPin, NULL) == S_OK)
	{
		PIN_DIRECTION ThisPinDir;
		pPin->QueryDirection(&ThisPinDir);
		if (ThisPinDir == PinDir)
		{
			IPin *pTmp = 0;
			hr = pPin->ConnectedTo(&pTmp);
			if (SUCCEEDED(hr))  // 이미 접속 끝나, 필요한 핀은 아니다.
			{
				pTmp->Release();
			}
			else  // 미접속, 이것이 필요한 핀이다.
			{
				pEnum->Release();
				*ppPin = pPin;
				return S_OK;
			}
		}
		pPin->Release();
	}
	pEnum->Release();
	// 일치하는 핀이 발견되지 않았다.
	return E_FAIL;
}

HRESULT ConnectFilters(
	IGraphBuilder *pGraph, // 필터 그래프 매니저.
	IPin *pOut,            // 업 스트림 필터의 출력 핀.
	IBaseFilter *pDest)    // 다운 스트림 필터.
{
	if ((pGraph == NULL) || (pOut == NULL) || (pDest == NULL))
	{
		return E_POINTER;
	}
#ifdef debug
	PIN_DIRECTION PinDir;
	pOut->QueryDirection(&PinDir);
	_ASSERTE(PinDir == PINDIR_OUTPUT);
#endif

	// 다운 스트림 필터의 입력 핀을 검색한다.
	IPin *pIn = 0;
	HRESULT hr = GetUnconnectedPin(pDest, PINDIR_INPUT, &pIn);
	if (FAILED(hr))
	{
		return hr;
	}
	// 접속을 시험한다.
	hr = pGraph->Connect(pOut, pIn);
	pIn->Release();
	return hr;
}
