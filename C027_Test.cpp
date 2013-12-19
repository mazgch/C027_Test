// C027_TestConsole.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <setupapi.h>
#pragma comment(lib, "setupapi.lib")

#define BAUDRATE	115200					// baudrate
#define BUF_SZ		0x10000					// maximum buffer size
#define T_RETRY		100						// retry interval in milliseconds

#define LOGS        _T("LOGS")
#define DISK		_T("%C:\\")				// disk device path
#define COM			_T("\\\\.\\COM%d")		// serial device path

#define CRP			_T("CRP DISABLD")		// disk name of the LPC11U35 ISP
#define MBED		_T("MBED")				// disk name of the LPC11U35 MBED
#define SERIAL		_T("mbed Serial Port")	// serial device name of the LPC11U35 MBED uart

#define TGTFW		_T("~AUTORST.bin")		// LPC1768 firmware (autostart the target)
#define CRPENTER	_T("~AUTOCRP.bin")		// switch to the CRP disabled mode (enter ISP of LPC11U35)
#define CRPFW		_T("firmware.bin")		// existing LPC11U35 firmware
#define MBEDFW		_T("INTERFCE.bin")		// LPC11U35 firmware
#define HTML		_T("mbed.htm")			// mbed html file on the LPC11U35

/* The Interface image can be build from the following sources: 
   https://github.com/mazgch/CMSIS-DAP
*/
#define FILECRP     _T("lpc11u35.bin")

/* The Target image consist of a test software that executes 
   the modem and other components.
   http://mbed.org/teams/ublox/code/C027_ProductionTest/
*/
#define FILETGT     _T("lpc1768.bin")
 
/* Additionally the code relies on the fact that a second C027 is 
   connected to the DUT with CAN and Ethernet
   http://mbed.org/teams/ublox/code/C027_ProductionTestLoobback/
*/

/* The software relies on driver installed on the pc. If you dont 
   find the "mbed Serial Port" under Ports and the "MBED" Disk in 
   the device manager update the driver of the composite mbed 
   device again. 
   http://mbed.org/handbook/Windows-serial-configuration
*/

void Notice(const TCHAR* format, ...);
#ifndef _CONSOLE
void SetColor(COLORREF Color);
void saveReport(DWORD dw);
#define WHITE    RGB(0xFF,0xFF,0xFF)
#define YELLOW   RGB(0xFF,0xFF,0xC0)
#define RED      RGB(0xFF,0xC0,0xC0)
#define GREEN    RGB(0xC0,0xFF,0xC0)
#define BLUE     RGB(0xC0,0xC0,0xFF)
#else
void saveReport(DWORD dw) {}
void SetColor(WORD Color);
#define WHITE    (FOREGROUND_INTENSITY|FOREGROUND_RED|FOREGROUND_GREEN|FOREGROUND_BLUE)
#define YELLOW   (FOREGROUND_INTENSITY|FOREGROUND_RED|FOREGROUND_GREEN)
#define RED      (FOREGROUND_INTENSITY|FOREGROUND_RED)
#define GREEN    (FOREGROUND_INTENSITY|FOREGROUND_GREEN)
#define BLUE     (FOREGROUND_INTENSITY|FOREGROUND_BLUE)
#endif

// ------------------------------------------------------------------------------------------
// file buffer handling
// ------------------------------------------------------------------------------------------

// interface firmware buffer and version
DWORD dwCrpImage		= 0;
BYTE* pCrpImage			= NULL;
const char* pCrpVersion = NULL;
// target firmware buffer and version
DWORD dwTgtImage		= 0;
BYTE* pTgtImage			= NULL;
const char* pTgtVersion = NULL;
// html file buffer
DWORD dwHtml	= 0;
BYTE* pHtml		= NULL;
HANDLE hSerial	= INVALID_HANDLE_VALUE;

void freeFile(BYTE* &pBuffer, DWORD& dwSize)
{
	if (pBuffer)
		delete [] pBuffer;
	pBuffer = NULL;
	dwSize = 0;
}

int readFile(LPCTSTR name, BYTE* &pBuffer, DWORD& dwSize)
{
	DWORD dw;
	freeFile(pBuffer, dwSize);
	Notice(_T("Load File %s\r\n"), name);
	HANDLE h = CreateFile(name, GENERIC_READ, 0, 
				0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (h == INVALID_HANDLE_VALUE)
		return 0x10;
	dwSize = GetFileSize(h, NULL);
	if (!dwSize)
		return 0x20;
	pBuffer = new BYTE[dwSize];
	if (!pBuffer)
		return 0x30;
	if (!ReadFile(h, pBuffer, dwSize, &dw, NULL))
		return 0x40;
	if (dw != dwSize)
		return 0x50;
	if (!CloseHandle(h))
		return 0x60;
	return 0;
}

int writeFile(LPCTSTR name, BYTE* pBuffer, DWORD dwSize)
{
	DWORD dw;
	Notice(_T("Write File %s\r\n"), name);
	HANDLE h = CreateFile(name, GENERIC_WRITE, 0, 
				0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (h == INVALID_HANDLE_VALUE)
		return 0x10;
	if (!WriteFile(h, pBuffer, dwSize, &dw, NULL))
		return 0x20;
	if (dw != dwSize)
		return 0x30;
	if (!CloseHandle(h))
		return 0x40;
	return 0;
}

// ------------------------------------------------------------------------------------------
// serial port handling
// ------------------------------------------------------------------------------------------

void freeSerial(void)
{
	if (hSerial != INVALID_HANDLE_VALUE)
		CloseHandle(hSerial);
	hSerial = INVALID_HANDLE_VALUE;
}

int openSerial(int port)
{
	TCHAR s[0x100];
	freeSerial();
	_stprintf_s(s, ARRAYSIZE(s), COM, port);
	Notice(_T("Open Serial Port %s\r\n"), s);
	hSerial = CreateFile(s, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, 
				0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0 );
	if (hSerial == INVALID_HANDLE_VALUE)
		return 0x10;
	DCB dcb;
	memset(&dcb,0,sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);
	if (!BuildCommDCB(_T("baud=115200 parity=N data=8 stop=1"), &dcb))
		return 0x20;
	dcb.BaudRate = BAUDRATE;
	if (!SetCommState(hSerial, &dcb))
		return 0x30;
	COMMTIMEOUTS to = { MAXDWORD, 0, 0, 0, 0 };
	if (!SetCommTimeouts(hSerial, &to))
		return 0x40;
	if (!SetupComm(hSerial, BUF_SZ, BUF_SZ))
		return 0x50;
	if (!PurgeComm(hSerial, PURGE_RXABORT|PURGE_RXCLEAR))
		return 0x60;
	return 0;
}

int findSerial(LPCTSTR n)
{
	DWORD dw;
	GUID g;
	HDEVINFO h;
	if (!SetupDiClassGuidsFromName(_T("Ports"), &g, 1, &dw))
		return 0;
	if ((h = SetupDiGetClassDevs(&g, NULL, NULL, DIGCF_PRESENT)) == INVALID_HANDLE_VALUE)
		return 0;
	SP_DEVINFO_DATA d;
	d.cbSize = sizeof(SP_DEVINFO_DATA);
	int l = _tcslen(n);
	TCHAR s[0x100];
	int p = 0;
	for (int i = 0; SetupDiEnumDeviceInfo(h, i, &d) && !p; i ++)
	{
		DWORD sz;
		HKEY k = SetupDiOpenDevRegKey(h, &d, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
		if (k) 
		{
			sz = sizeof(s);
			if (SetupDiGetDeviceRegistryProperty(h, &d, SPDRP_DEVICEDESC, &dw, (PBYTE)s, sz, &sz) && 
				(dw == REG_SZ) && (_tcscmp(n, s) == 0))
			{
				sz = sizeof(s);
				if ((RegQueryValueEx(k, _T("PortName"), NULL, &dw, (LPBYTE)s, &sz) == ERROR_SUCCESS) && 
					(dw == REG_SZ) && (_tcsncmp(_T("COM"),s,3)==0))
				{
					p = _ttoi(s+3);
				}
			}
			RegCloseKey(k);
		}
	}
	SetupDiDestroyDeviceInfoList(h);
	return p;
}	

// ------------------------------------------------------------------------------------------
// version number handling
// ------------------------------------------------------------------------------------------

const char* findVersion(BYTE* pBuffer, DWORD dwSize, const char* pLabel)
{
	const char* p = (const char*)pBuffer;
	DWORD dwLabel = strlen(pLabel);
	for (; dwSize >= dwLabel; p ++, dwSize --)
	{
		if (0 == memcmp(p, pLabel, dwLabel))
			break;
	}
	if (dwSize < dwLabel)
		return NULL;
	const char *e = strstr(p, "\r\n");
	if (!e)	e = p + strlen(p);
	Notice(_T("Version: %.*hs\r\n"), e-p, p);
	return p;
}

int compareVersion(const char* pA, const char* pB)
{
	const char* eA = strstr(pA, "\r\n");
	if (!eA) eA = pA + strlen(pA);
	const char* eB = strstr(pB, "\r\n");
	if (!eB) eB = pB + strlen(pB);
	int iA = eA - pA;
	int iB = eB - pB;
	if (iA != iB)
		return 0x10;
	if (0 != memcmp(pA, pB, iA))
		return 0x20;
	return 0;
}

const char* getCrpVersion(BYTE* pBuffer, DWORD dwSize)
{
	return findVersion(pBuffer, dwSize, "<!-- Version: "/*0200 Build: Nov 21 2013 15:36:59 -->\r\n*/);
}

const char* getTgtVersion(BYTE* pBuffer, DWORD dwSize)
{
	return findVersion(pBuffer, dwSize, "TestVer    = "/*Nov 19 2013 15:23:38\"*/);
} 

// ------------------------------------------------------------------------------------------
// testing procedures
// ------------------------------------------------------------------------------------------

int handleMbedDisk(TCHAR letter)
{
	TCHAR s[256];
	TCHAR t[256];
	DWORD dw;
	bool ok;
	int port;
	
	_stprintf_s(s, ARRAYSIZE(s), DISK HTML, letter);
	ok = (INVALID_FILE_ATTRIBUTES != GetFileAttributes(s));
	if (ok)
	{
		_stprintf_s(t, ARRAYSIZE(t), DISK TGTFW, letter);
		ok = (INVALID_FILE_ATTRIBUTES == GetFileAttributes(t));
	}
	if (ok) 
	{
		port = findSerial(SERIAL);
		ok = (port > 0);
	}
	if (ok)
	{	
		SetColor(YELLOW);
		int e = readFile(s, pHtml, dwHtml);
		if (e)
			return 0x200 | e;
		const char* pHtmlVersion = getCrpVersion(pHtml, dwHtml);
		if (pHtmlVersion)
		{
			e = compareVersion(pHtmlVersion, pCrpVersion);
			freeFile(pHtml, dwHtml); // no longer needed
		}
		if (e || !pHtmlVersion)
		{
			BYTE ff[] = { 0xFF, 0xFF, 0xFf, 0xFF };
			_stprintf_s(t, ARRAYSIZE(t), DISK CRPENTER, letter);
			e = writeFile(t, ff, sizeof(ff));
			// if we fail here we should update the LPC11U35 firmware
			return 0;
		}

		// copy the image
		e = writeFile(t, pTgtImage, dwTgtImage);
		if (e)
			return 0x400 | e;

		Sleep(500); // time to start the stuff 

		e = openSerial(port);
		if (e)
			return 0x500 | e;
						
		DWORD dwCmd = strlen(pTgtVersion);
		Notice(_T("Send \"%hs\" to start the test\r\n"), pTgtVersion);
		if (!WriteFile(hSerial, pTgtVersion, dwCmd, &dw, NULL))
			return 0x600;
		if (dwCmd != dw)
			return 0x610;

		// now we can talk to the target
#ifdef _CONSOLE
		Notice(_T("Console: \"\r\n"));
#endif
		DWORD dwReport = 0;
		BYTE pReport[0x8000] = {0};
		BYTE* p = pReport;
		int t;
		for (t = 16000; (t > 0) && (dwReport < sizeof(pReport)-1); t -= T_RETRY)
		{
			if (!ReadFile(hSerial, p, sizeof(pReport)-1-dwReport, &dw, NULL))
				return 0x700;
			else if (dw)
			{
#ifdef _CONSOLE
				printf("%.*s", dw, p);
#endif
				dwReport += dw;
				p += dw;
				if ((dwReport > 9) && (0 == memcmp(p-9, "__EOF__\r\n", 9)))
					break;
			}
			Sleep(T_RETRY);
		}
#ifndef _CONSOLE
		Notice(_T("Console: \"%.*hs\"\r\n"), dwReport, pReport);
#else
		Notice(_T("\"\r\n"));
#endif
		if (t<= 0)
			return 0x710;
		const char* pReportVersion = getTgtVersion(pReport, dwReport);
		if (!pReportVersion)
			return 0x720;
		e = compareVersion(pReportVersion, pTgtVersion);
		if (e)
			return 0x730;
		
		freeSerial();
		
		if (!strstr((char*)pReport, "TestPassed = OK"))
			return 0x800;
	
		SetColor(GREEN);
		Notice(_T("Test passed, next device?\r\n"));
		return 0;
	}
	return -1;
}

int handleCrpDisk(TCHAR letter)
{
	int e;
	TCHAR s[256];
	_stprintf_s(s, ARRAYSIZE(s), DISK CRPFW, letter);
	if (INVALID_FILE_ATTRIBUTES != GetFileAttributes(s))
	{
		SetColor(YELLOW);
		Notice(_T("Delete \"%s\"\r\n"), s);
		if (!DeleteFile(s))
			return 0x100;
	}
	_stprintf_s(s, ARRAYSIZE(s), DISK MBEDFW, letter);
	if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(s))
	{
		SetColor(YELLOW);
		e = writeFile(s, pCrpImage, dwCrpImage);
		if (e)
			return 0x200 | e;
		SetColor(BLUE);
		Notice(_T("Waiting for Reboot as %s\r\n"), MBED);
		return -2;
	}
	return -1;
}

int init(LPCTSTR pFwCrp, LPCTSTR pFwTgt)
{
	int e;
	SetColor(YELLOW);
	e = readFile(pFwCrp, pCrpImage, dwCrpImage);
	if (e)
		return 0x100 | e;		
	pCrpVersion = getCrpVersion(pCrpImage, dwCrpImage);
	if (!pCrpVersion)			
		return 0x200;
	e = readFile(pFwTgt, pTgtImage, dwTgtImage);
	if (e)
		return 0x300 | e;
	pTgtVersion = getTgtVersion(pTgtImage, dwTgtImage);
	if (!pTgtVersion)			
		return 0x400;
	SetColor(WHITE);
	Notice(_T("Connect a Device\r\n"));
	return 0;
}

void cleanup(void)
{
	freeFile(pCrpImage, dwCrpImage);
	pCrpVersion = NULL;
	freeFile(pTgtImage, dwTgtImage);
	pTgtVersion = NULL;
	freeFile(pHtml, dwHtml);
	freeSerial();
}

int task(void)
{
	int e = -1;
	for (TCHAR letter = _T('C'); (letter <= _T('Z')) && (e == -1); letter ++)
	{
		SHFILEINFO shFile; 
		TCHAR s[16];
		_stprintf_s(s, ARRAYSIZE(s), DISK, letter);
		if (INVALID_FILE_ATTRIBUTES != GetFileAttributes(s) && 
			SHGetFileInfo(s,FILE_ATTRIBUTE_NORMAL,&shFile,sizeof(SHFILEINFO),SHGFI_DISPLAYNAME))
		{					
			if (0 == _tcsncmp(CRP, shFile.szDisplayName, 11))
			{
				Notice(NULL);
				e = handleCrpDisk(letter);
				if (e > 0) 
				{
					SetColor(RED);				
					Notice(_T("Error %08X in %s mode\r\n"), e, CRP);
				}
			}
			else if (0 == _tcsncmp(MBED, shFile.szDisplayName, 4))
			{
				Notice(NULL);
				e = handleMbedDisk(letter);
				if (e > 0)
				{
					SetColor(RED);				
					Notice(_T("Error %08X in %s mode\r\n"), e, MBED);
				}
			}
		}
	}
	return e;
}

#ifdef _CONSOLE  // ---------------------------------------------------------

int _tmain(int argc, _TCHAR* argv[])
{
	BOOL done = FALSE;
	int e = init((argc > 1) ? argv[1] : FILECRP, 
			     (argc > 2) ? argv[2] : FILETGT);
	if (e) return 0x10000 | e;
	while (!done) 
	{
		e = task();
		done = (e > 0);
		Sleep(T_RETRY);
	}
#ifdef _DEBUG
	Notice(_T("Done, press any key\r\n"));
	_getch();
#endif
	return 0;
}

void Notice(const TCHAR* format, ...)
{
	static DWORD start = GetTickCount();
	DWORD now = GetTickCount();
	if (!format)
		start = now;
	else
	{
		va_list args;
		va_start (args, format);
		_tprintf(_T("%5.2f : "), 0.001 * (now-start));
		_vtprintf_s(format, args);
		va_end (args);
	}
}

void SetColor(WORD Color)
{
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, Color);
}

#else // _CONSOLE ----------------------------------------------

#include "resource.h"		// main symbols

#pragma comment(linker, \
  "\"/manifestdependency:type='Win32' "\
  "name='Microsoft.Windows.Common-Controls' "\
  "version='6.0.0.0' "\
  "processorArchitecture='*' "\
  "publicKeyToken='6595b64144ccf1df' "\
  "language='*'\"")

#pragma comment(lib, "ComCtl32.lib")

enum { TIMER_ID = 100, IDD = IDD_DLG };
HWND g_hDlg = NULL;
HANDLE g_hThread;
HBRUSH g_hBrush;
	
DWORD WINAPI Thread( LPVOID lpParam )
{
	return task();
}

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  int id;
  switch(uMsg)
  {
  case WM_COMMAND:
    switch(LOWORD(wParam))
    {
    case IDCANCEL:
      SendMessage(hDlg, WM_CLOSE, 0, 0);
      return TRUE;
    }
    break;

  case WM_TIMER:
	if (TIMER_ID == wParam)
	{
		DWORD dw;
		if (g_hThread == NULL)
			g_hThread = CreateThread( NULL, 0, Thread, NULL, 0, NULL);
		else if (GetExitCodeThread(g_hThread, &dw) && (dw != STILL_ACTIVE))
		{
			if ((dw != -1) && (dw != -2))
				saveReport(dw);
			CloseHandle(g_hThread);
			g_hThread = NULL;
		}
	}
    return TRUE;
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORSTATIC:
	id = GetDlgCtrlID((HWND)lParam);
	if (id == IDC_EDIT)
	{
		SetBkMode((HDC)wParam,TRANSPARENT);
		return (BOOL)g_hBrush;
	} 
	return NULL;
  case WM_CLOSE:
    DestroyWindow(hDlg);
    return TRUE;

  case WM_DESTROY:
    PostQuitMessage(0);
    return TRUE;
  }

  return FALSE;
}

int WINAPI _tWinMain(HINSTANCE hInst, HINSTANCE h0, LPTSTR lpCmdLine, int nCmdShow)
{
  MSG msg;
  BOOL ret;

  InitCommonControls();
  CreateDirectory(LOGS, NULL);
  g_hDlg = CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_DLG), 0, DialogProc, 0);
  ShowWindow(g_hDlg, nCmdShow);
  init(FILECRP, FILETGT);
  SetTimer(g_hDlg, TIMER_ID, T_RETRY, NULL);

  while((ret = GetMessage(&msg, 0, 0, 0)) != 0) {
    if(ret == -1)
      return -1;

    if(!IsDialogMessage(g_hDlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  
  cleanup();

  return 0;
}

static TCHAR g_str[0x1000] = _T("");
static int g_i = 0;

void saveReport(DWORD dw)
{
	TCHAR d[32];
	TCHAR s[256];
	time_t rt;
	struct tm ti;
	time(&rt);
	localtime_s(&ti, &rt);
	_tcsftime(d, ARRAYSIZE(d), _T("%Y%m%d_%H%M%S"),&ti);
	if (dw == 0)
		_stprintf_s(s, ARRAYSIZE(s), _T("%s\\%s_OK.txt"), LOGS, d);
	else 
		_stprintf_s(s, ARRAYSIZE(s), _T("%s\\%s_ERR%05X.txt"), LOGS, d, dw);
	writeFile(s, (BYTE*)g_str, g_i*sizeof(TCHAR));
}

void Notice(const TCHAR* format, ...)
{
	static DWORD start = GetTickCount();
	DWORD now = GetTickCount();
	if (!format)
	{
		start = now;
		*g_str = _T('\0');
		g_i = 0;
	}
	else
	{
		va_list args;
		va_start (args, format);
		g_i += _stprintf_s(&g_str[g_i], ARRAYSIZE(g_str) - g_i, 
				_T("%5.2f : "), 0.001 * (now-start));
		g_i += _vstprintf_s(&g_str[g_i], ARRAYSIZE(g_str) - g_i,
				format, args);
		SetDlgItemText(g_hDlg, IDC_EDIT, g_str);
		va_end (args);
	}
}

void SetColor(COLORREF Color)
{
	if (g_hBrush != NULL)
		DeleteObject(g_hBrush);
	g_hBrush = CreateSolidBrush(Color);
	InvalidateRect(g_hDlg, NULL, TRUE);
	UpdateWindow(g_hDlg);
}

#endif // _CONSOLE ----------------------------------------------
