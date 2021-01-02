
#include <vcl.h>

#include <inifiles.hpp>

#include <mmsystem.h>
#include <time.h>
#include <stdio.h>
#include <fastmath.h>

#pragma hdrstop

#include "Unit1.h"

#define BROADCAST_WOL_IP		"255.255.255.255"
#define BROADCAST_WOL_PORT		9

#define MIN_WAV_SIZE          44 // bytes

#define ARRAY_SIZE(array)       (sizeof(array) / sizeof(array[0]))
#define SQR(x)                  ((x) * (x))
#define IROUND(x)               ((int)floor((x) + 0.5))
#define I64ROUND(x)             ((int64_t)floor((x) + 0.5))
#define MIN(a, b)               (((a) < (b)) ? (a) : (b))
#define MAX(a, b)               (((a) > (b)) ? (a) : (b))
#define ABS(x)                  (((x) >= 0) ? (x) : -(x))

#pragma package(smart_init)
#pragma resource "*.dfm"

TForm1 *Form1 = NULL;

__fastcall TForm1::TForm1(TComponent* Owner)
	: TForm(Owner)
{
}

void __fastcall TForm1::FormCreate(TObject *Sender)
{
	String s;

	Application->HintPause = 0;
	Application->HintHidePause = 30000; // 30 sec.

	{
		TVersion version;
		TForm1::GetBuildInfo(Application->ExeName, &version);
		s.printf("v%d.%d.%d.%d", version.MajorVer, version.MinorVer, version.ReleaseVer, version.BuildVer);
		#ifdef _DEBUG
			s += " debug";
		#endif
		Caption = Application->Title + " " + s;
	}

	{
		//char username[64];
		//DWORD size = sizeof(username);
		//if (::GetUserNameA(username, &size) != FALSE && size > 1)
		//	ini_filename = ChangeFileExt(Application->ExeName, "_" + String(username) + ".ini");
		//else
			ini_filename = ChangeFileExt(Application->ExeName, ".ini");
	}

	thread = NULL;

	{
//		srand(time(0));
		SYSTEMTIME time;
		::GetSystemTime(&time);
		srand(time.wMilliseconds);
	}

	::GetSystemInfo(&sys_info);

	this->DoubleBuffered  = true;
	Memo1->DoubleBuffered = true;

	// help stop flicker
	this->ControlStyle  = this->ControlStyle  << csOpaque;
	Memo1->ControlStyle = Memo1->ControlStyle << csOpaque;

	dest_detected_wav_filename = IncludeTrailingPathDelimiter(ExtractFilePath(Application->ExeName)) + "dong.wav";
	//dest_detected_wav_filename = work_dir + "dong.wav";

	Memo1->Clear();

	StatusLabel->Caption = "Resting      ";
	MACEditA->Text       = "";
	MACEditB->Text       = "";
	IPEdit->Text         = "";

	for (int i = 0; i < MAX_DEST; i++)
	{
		WOL[i].secs          = -1;

		ping[i].sock         = INVALID_SOCKET;
		ping[i].stage        = PING_STAGE_NONE;
		ping[i].id           = rand();
		ping[i].sequence     = rand();
		ping[i].source_port  = rand();
		ping[i].message_size = 32;		// The message size that the ICMP echo request should carry with it
		ping[i].timeout_ms   = 1000;	// Request time out for echo request (in milliseconds)
		//ping[i].count      = 3;		// Max number of pings
		ping[i].count        = -1;		// Ping until we get a reply
	}

	loadSettings();

	// initialise WinSock
	memset(&wsaData, 0, sizeof(wsaData));
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) == SOCKET_ERROR)
	{
		const int err = WSAGetLastError();
		s = "WSAStartup error [" + IntToStr(err) + "] " + errorToStr(err);
		MessageDlg(s, mtError, TMsgDlgButtons() << mbCancel, 0);
		Close();
	}
	// Memo1->Lines->Add(IntToHex(wsaData.wVersion, 4) + " " + IntToHex(wsaData.wHighVersion, 4));
	// LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2

	{	// first try to load the external file. if the external file is not present then use the built-in resourced file
		dest_detected_wav.resize(0);

		FILE *fin = fopen(AnsiString(dest_detected_wav_filename).c_str(), "rb");
		if (fin != NULL)
		{
			if (fseek(fin, 0, SEEK_END) == 0)
			{
				const size_t file_size = ftell(fin);
				if (file_size > MIN_WAV_SIZE)
				{
					if (fseek(fin, 0, SEEK_SET) == 0)
					{
						dest_detected_wav.resize(file_size);
						if (fread(&dest_detected_wav[0], 1, file_size, fin) != file_size)
							dest_detected_wav.resize(0);
					}
				}
			}
			fclose(fin);
		}

		if (dest_detected_wav.empty())
		{	// wav file not loaded .. load it from an included resource

			//for (int i = 0; i < 32767; i++)
			//	EnumResourceNamesA(HInstance, MAKEINTRESOURCE(i), (ENUMRESNAMEPROCA) &EnumResNameProc, (LONG_PTR)i);
			//	EnumResourceNamesA(HInstance, RT_RCDATA, (ENUMRESNAMEPROCA) &EnumResNameProc, (LONG_PTR)i);

			const HMODULE handle = GetModuleHandle(NULL);
			if (handle != NULL)
			{
				const HRSRC res_info = FindResource(handle, "DONG_WAV", RT_RCDATA);
				if (res_info != NULL)
				{
					const HGLOBAL res_data = LoadResource(handle, res_info);
					if (res_data != NULL)
					{
						const DWORD dwSize = SizeofResource(handle, res_info);
						if (dwSize > MIN_WAV_SIZE)
						{
							const VOID *p_res_data = LockResource(res_data);
							if (p_res_data != NULL)
							{
								dest_detected_wav.resize(dwSize);
								memmove(&dest_detected_wav[0], p_res_data, dwSize);
							}
						}
					}
				}
			}
		}
	}

	::PostMessage(this->Handle, WM_INIT_GUI, 0, 0);
}

void __fastcall TForm1::FormDestroy(TObject *Sender)
{
	if (thread != NULL)
	{
		thread->Terminate();
		thread->WaitFor();
		delete thread;
		thread = NULL;
	}

	for (int i = 0; i < MAX_DEST; i++)
	{
		if (ping[i].sock != INVALID_SOCKET)
		{
			closesocket(ping[i].sock);
			ping[i].sock = INVALID_SOCKET;
		}
	}

	if (wsaData.wVersion != 0)
		if (WSACleanup() == SOCKET_ERROR)
			MessageDlg("WSACleanup() error " + IntToStr(WSAGetLastError()), mtError, TMsgDlgButtons() << mbCancel, 0);
}

void __fastcall TForm1::FormClose(TObject *Sender, TCloseAction &Action)
{
	if (thread != NULL)
	{
		thread->Terminate();
		thread->WaitFor();
		delete thread;
		thread = NULL;
	}

	saveSettings();
}

bool __fastcall TForm1::GetBuildInfo(String filename, TVersion *version)
{
	DWORD ver_info_size;
	char *ver_info;
	UINT buffer_size;
	LPVOID buffer;
	DWORD dummy;

	if (version == NULL || filename.IsEmpty())
		return false;

	memset(version, 0, sizeof(TVersion));

	ver_info_size = ::GetFileVersionInfoSizeA(filename.c_str(), &dummy);
	if (ver_info_size == 0)
		return false;

	ver_info = new char [ver_info_size];
	if (ver_info == NULL)
		return false;

	if (::GetFileVersionInfoA(filename.c_str(), 0, ver_info_size, ver_info) == FALSE)
	{
		delete [] ver_info;
		return false;
	}

	if (::VerQueryValue(ver_info, _T("\\"), &buffer, &buffer_size) == FALSE)
	{
		delete [] ver_info;
		return false;
	}

	PVSFixedFileInfo ver = (PVSFixedFileInfo)buffer;
	version->MajorVer   = (ver->dwFileVersionMS >> 16) & 0xFFFF;
	version->MinorVer   = (ver->dwFileVersionMS >>  0) & 0xFFFF;
	version->ReleaseVer = (ver->dwFileVersionLS >> 16) & 0xFFFF;
	version->BuildVer   = (ver->dwFileVersionLS >>  0) & 0xFFFF;

	delete [] ver_info;

	return true;
}

bool __fastcall TForm1::createPath(const char *path)
{
	DWORD attr;
	int len;
	char path_str[MAX_PATH];
	bool result = true;

	if (path == NULL)
		return false;

	len = strlen(path);
	if (len <= 0)
		return false;
	if (len > (int)sizeof(path_str) - 1)
		len = (int)sizeof(path_str) - 1;

	memset(path_str, 0, sizeof(path_str));
	memcpy(path_str, path, len);

	// remove trailing slashes
	while (len > 0 && (path_str[len - 1] == '\\'))
		path_str[--len] = '\0';

	if (path_str[0] == '\0')
		return true;

	attr = ::GetFileAttributes(path_str);
	if (0xFFFFFFFF == attr)		// folder doesn't exist yet - create it!
	{
		int i = len - 1;
		while (i > 0 && path_str[i] != '\\' && path_str[i] != ':')
			i--;
		if (i > 0)
		{	// create parent folders
			char c = path_str[i];
			if (c != ':')
			{
				path_str[i] = '\0';
				result = createPath(path_str);
				path_str[i] = c;
			}
		}
		// create folder
		if (path_str[len - 1] != ':')
			result = (result && ::CreateDirectory(path_str, NULL)) ? true : false;
	}
	else
	if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
	{  // something already exists, but is not a folder
		::SetLastError(ERROR_FILE_EXISTS);
		result = false;
	}

	return result;
}

String __fastcall TForm1::errorToStr(const int err)
{
	char msg_buf[256];
	msg_buf[0] = '\0';

// 	const DWORD res = ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL,
					err,
					MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
					msg_buf,
					sizeof(msg_buf),
					NULL);

	if (msg_buf[0] == '\0')
	{
		#if defined(__BORLANDC__) && (__BORLANDC__ < 0x0600)
//			if (res > 0)
				sprintf(msg_buf, "%d", err);
		#else
//			if (res > 0)
				sprintf_s(msg_buf, sizeof(msg_buf), "%d", err);
		#endif
	}

	return String(msg_buf);
}

String __fastcall TForm1::getLastErrorStr(const DWORD err)
{
	String str = "unknown error";
	char *buf = NULL;

	WORD prevErrorMode = ::SetErrorMode(SEM_FAILCRITICALERRORS);	// ignore critical errors

	HMODULE wnet_handle = ::GetModuleHandleA("wininet.dll");

	DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM;
	if (wnet_handle)
		flags |= FORMAT_MESSAGE_FROM_HMODULE;		// retrieve message from specified DLL

	::FormatMessageA(flags, wnet_handle, err, 0, (LPSTR)&buf, 0, NULL);
//	const DWORD res = ::FormatMessageA(flags, wnet_handle, err, 0, (LPSTR)&buf, 0, NULL);

	if (wnet_handle)
		::FreeLibrary(wnet_handle);

	if (buf)
	{
		str.printf("[%d] %s", err, buf);
		::LocalFree(buf);
	}

	::SetErrorMode(prevErrorMode);

	return str;
}

int __fastcall TForm1::getLastErrorStr(const DWORD err, void *err_str, int max_size)
{
	int str_len = 0;

	if (!err_str || max_size <= 0)
		return str_len;

	memset(err_str, 0, max_size);

	char *buf = NULL;

	WORD prevErrorMode = ::SetErrorMode(SEM_FAILCRITICALERRORS);	// ignore critical errors

	HMODULE wnet_handle = ::GetModuleHandleA("wininet.dll");

	DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM;
	if (wnet_handle)
		flags |= FORMAT_MESSAGE_FROM_HMODULE;		// retrieve message from specified DLL

	const DWORD res = ::FormatMessageA(flags, wnet_handle, err, 0, (LPSTR)&buf, 0, NULL);

	if (wnet_handle)
		::FreeLibrary(wnet_handle);

	if (buf != NULL)
	{
		#if defined(__BORLANDC__) && (__BORLANDC__ < 0x0600)
			if (res > 0)
				str_len = sprintf((char *)err_str, "[%d] %s", err, buf);
		#else
			if (res > 0)
				str_len = sprintf_s((char *)err_str, max_size - 1, "[%d] %s", err, buf);
		#endif

		::LocalFree(buf);
	}

	::SetErrorMode(prevErrorMode);

	return str_len;
}

void __fastcall TForm1::WMWindowPosChanging(TWMWindowPosChanging &msg)
{
	#define SWP_STATECHANGED            0x8000
	#define WINDOW_SNAP                 5

	const int thresh = WINDOW_SNAP;

	if (msg.WindowPos->flags & SWP_STATECHANGED)
	{
		if (msg.WindowPos->flags & SWP_FRAMECHANGED)
		{
			if (msg.WindowPos->x < 0 && msg.WindowPos->y < 0)
			{	// Window state is about to change to MAXIMIZED
				if ((msg.WindowPos->flags & (SWP_SHOWWINDOW | SWP_NOACTIVATE)) == (SWP_SHOWWINDOW | SWP_NOACTIVATE))
				{	// about to minimize
					return;
				}
				else
				{	// about to maximize
					return;
				}
			}
			else
			{	// about to normalize
			}
		}
	}

	if (msg.WindowPos->hwnd != this->Handle || Screen == NULL)
		return;

	const int dtLeft   = Screen->DesktopRect.left;
	//const int dtRight  = Screen->DesktopRect.right;
	const int dtTop    = Screen->DesktopRect.top;
	const int dtBottom = Screen->DesktopRect.bottom;
	const int dtWidth  = Screen->DesktopRect.Width();
	const int dtHeight = Screen->DesktopRect.Height();

	//const int waLeft   = Screen->WorkAreaRect.left;
	//const int waRight  = Screen->WorkAreaRect.right;
	//const int waTop    = Screen->WorkAreaRect.top;
	//const int waBottom = Screen->WorkAreaRect.bottom;
	//const int waWidth  = Screen->WorkAreaRect.Width();
	//const int waHeight = Screen->WorkAreaRect.Height();

	int x = msg.WindowPos->x;
	int y = msg.WindowPos->y;
	int w = msg.WindowPos->cx;
	int h = msg.WindowPos->cy;

	for (int i = 0; i < Screen->MonitorCount; i++)
	{	// sticky screen edges
		const int mLeft   = Screen->Monitors[i]->WorkareaRect.left;
		const int mRight  = Screen->Monitors[i]->WorkareaRect.right;
		const int mTop    = Screen->Monitors[i]->WorkareaRect.top;
		const int mBottom = Screen->Monitors[i]->WorkareaRect.bottom;
		const int mWidth  = Screen->Monitors[i]->WorkareaRect.Width();
		const int mHeight = Screen->Monitors[i]->WorkareaRect.Height();

		if (ABS(x - mLeft) < thresh)
				  x = mLeft;			// stick left to left side
		else
		if (ABS((x + w) - mRight) < thresh)
					x = mRight - w;	// stick right to right side

		if (ABS(y - mTop) < thresh)
				  y = mTop;				// stick top to top side
		else
		if (ABS((y + h) - mBottom) < thresh)
					y = mBottom - h;	// stick bottom to bottm side

		// stick the right side to the right side of the screen if the left side is stuck to the left side of the screen
		if (x == mLeft)
			if ((w >= (mWidth - thresh)) && (w <= (mWidth + thresh)))
				w = mWidth;

		// stick the bottom to the bottom of the screen if the top is stuck to the top of the screen
		if (y == mTop)
			if ((h >= (mHeight - thresh)) && (h <= (mHeight + thresh)))
				h = mHeight;
	}
/*
	{	// sticky screen edges
		if (ABS(x - waLeft) < thresh)
			x = waLeft;			// stick left to left side
		else
		if (ABS((x + w) - waRight) < thresh)
			x = waRight - w;	// stick right to right side

		if (ABS(y - waTop) < thresh)
			y = waTop;			// stick top to top side
		else
		if (ABS((y + h) - waBottom) < thresh)
			y = waBottom - h;	// stick bottom to bottm side

		// stick the right side to the right side of the screen if the left side is stuck to the left side of the screen
		if (x == waLeft)
			if ((w >= (waWidth - thresh)) && (w <= (waWidth + thresh)))
				w = waWidth;

		// stick the bottom to the bottom of the screen if the top is stuck to the top of the screen
		if (y == waTop)
			if ((h >= (waHeight - thresh)) && (h <= (waHeight + thresh)))
				h = waHeight;
	}
*/
	// limit minimum size
	if (w < Constraints->MinWidth)
		 w = Constraints->MinWidth;
	if (h < Constraints->MinHeight)
		 h = Constraints->MinHeight;

	// limit maximum size
	if (w > Constraints->MaxWidth && Constraints->MaxWidth > Constraints->MinWidth)
		 w = Constraints->MaxWidth;
	if (h > Constraints->MaxHeight && Constraints->MaxHeight > Constraints->MinHeight)
		 h = Constraints->MaxHeight;

	// limit maximum size
	if (w > dtWidth)
		 w = dtWidth;
	if (h > dtHeight)
		 h = dtHeight;
/*
	if (Application->MainForm && this != Application->MainForm)
	{	// stick to our main form sides
		const TRect rect = Application->MainForm->BoundsRect;

		if (ABS(x - rect.left) < thresh)
			x = rect.left;			// stick to left to left side
		else
		if (ABS((x + w) - rect.left) < thresh)
			x = rect.left - w;	// stick right to left side
		else
		if (ABS(x - rect.right) < thresh)
			x = rect.right;		// stick to left to right side
		else
		if (ABS((x + w) - rect.right) < thresh)
			x = rect.right - w;	// stick to right to right side

		if (ABS(y - rect.top) < thresh)
			y = rect.top;			// stick top to top side
		else
		if (ABS((y + h) - rect.top) < thresh)
			y = rect.top - h;		// stick bottom to top side
		else
		if (ABS(y - rect.bottom) < thresh)
			y = rect.bottom;		// stick top to bottom side
		else
		if (ABS((y + h) - rect.bottom) < thresh)
			y = rect.bottom - h;	// stick bottom to bottom side
	}
*/
	// stop it completely leaving the desktop area
	if (x < (dtLeft - Width + (dtWidth / 15)))
		 x =  dtLeft - Width + (dtWidth / 15);
	if (x > (dtWidth - (Screen->Width / 15)))
		 x =  dtWidth - (Screen->Width / 15);
	if (y < dtTop)
		 y = dtTop;
	if (y > (dtBottom - (dtHeight / 10)))
		 y =  dtBottom - (dtHeight / 10);

	msg.WindowPos->x  = x;
	msg.WindowPos->y  = y;
	msg.WindowPos->cx = w;
	msg.WindowPos->cy = h;
}

void __fastcall TForm1::WMInitGUI(TMessage &msg)
{
	String s;

//	if (hello_wav.size() > MIN_WAV_SIZE)
//		PlaySound(&hello_wav[0], NULL, SND_MEMORY | SND_NODEFAULT | SND_NOWAIT | SND_ASYNC);

	loadSettings();

	if (Application->MainForm)
		Application->MainForm->Update();

	Update();

//	s = FormatDateTime(" dddd dd mmm yyyy  hh:nn:ss", Now());
//	StatusBar1->Panels->Items[0]->Text = s;

	//	BringToFront();
//	::SetForegroundWindow(Handle);

//	Timer1->Enabled = true;

	// create & start the thread
	thread = new CThread(&ThreadProcess, tpNormal, true);
//	if (thread == NULL)
//		stop();

	startup_timer.mark();

	if (WakeOnStartCheckBox->Checked)
		wakeyWakey(0);
}

void __fastcall TForm1::loadSettings()
{
	int i;
	float f;
	String s;
	bool b;

	TIniFile *ini = new TIniFile(ini_filename);
	if (ini == NULL)
		return;

	Top    = ini->ReadInteger("MainForm", "Top",    Top);
	Left   = ini->ReadInteger("MainForm", "Left",   Left);
	Width  = ini->ReadInteger("MainForm", "Width",  Width);
	Height = ini->ReadInteger("MainForm", "Height", Height);

	IPEdit->Text   = ini->ReadString("Misc", "IP",   IPEdit->Text);
	MACEditA->Text = ini->ReadString("Misc", "MACA", MACEditA->Text);
	MACEditB->Text = ini->ReadString("Misc", "MACB", MACEditB->Text);

	dest_detected_wav_filename = ini->ReadString("Misc", "DEST_DETECTED_WAV", dest_detected_wav_filename);

	WakeOnStartCheckBox->Checked = ini->ReadBool("Misc", "WakeOnStart", WakeOnStartCheckBox->Checked);
	CloseOnWakeCheckBox->Checked = ini->ReadBool("Misc", "CloseOnWake", CloseOnWakeCheckBox->Checked);

	delete ini;
}

void __fastcall TForm1::saveSettings()
{
	String s;

	DeleteFile(ini_filename);

	TIniFile *ini = new TIniFile(ini_filename);
	if (ini == NULL)
		return;

	ini->WriteInteger("MainForm", "Top",    Top);
	ini->WriteInteger("MainForm", "Left",   Left);
	ini->WriteInteger("MainForm", "Width",  Width);
	ini->WriteInteger("MainForm", "Height", Height);

	ini->WriteString("Misc", "IP",   IPEdit->Text);
	ini->WriteString("Misc", "MACA", MACEditA->Text);
	ini->WriteString("Misc", "MACB", MACEditB->Text);

	ini->WriteString("Misc", "DEST_DETECTED_WAV", dest_detected_wav_filename);

	ini->WriteBool("Misc", "WakeOnStart", WakeOnStartCheckBox->Checked);
	ini->WriteBool("Misc", "CloseOnWake", CloseOnWakeCheckBox->Checked);

	delete ini;
}


void __fastcall TForm1::FormKeyDown(TObject *Sender, WORD &Key,
      TShiftState Shift)
{
	// https://docs.microsoft.com/en-us/windows/desktop/inputdev/virtual-key-codes
	switch (Key)
	{
		case VK_ESCAPE:
			Key = 0;
			//if (thread)
			//{
			//	::PostMessage(this->Handle, WM_CAPTURE_STOP, 0, 0);
			//}
			//else
				Close();
			break;
		case VK_SPACE:
			//Key = 0;
			break;
		case VK_PAUSE:
			//Key = 0;

			break;
//		case VK_DELETE:
//		case VK_PRIOR:		// page up
//		case VK_NEXT:		// page down
//		case VK_LEFT:		// left arrow
//		case VK_RIGHT:		// right arrow
//		case VK_UP:			// up arrow
//		case VK_DOWN:		// down arrow
//		case VK_F1:			// F1 key
	}
}

void __fastcall TForm1::ThreadProcess()
{
	if (thread != NULL)
		pingProcess();
}

bool __fastcall TForm1::isValidIP(String ip_str)
{
	int dots = 0;
	int numbers = 0;
	const char delim = '.';

	ip_str = ip_str.Trim();
	if (ip_str.IsEmpty())
		return false;

	for (int i = 1; i <= ip_str.Length(); )
	{
		int k = i;
		while (k <= ip_str.Length() && ip_str[k] != delim)
		{
			const char c = ip_str[k++];
			if (c < '0' || c > '9')
				return false;
		}
		if (k < ip_str.Length() && ip_str[k] != delim)
			return false;

		String s = ip_str.SubString(i, k - i);
		int num = -1;
		if (!TryStrToInt(s, num) || num < 0 || num > 255)
			return false;

		numbers++;

		if (k < ip_str.Length())
			dots++;

		i = k + 1;
	}

	return (numbers == 4 && dots == 3) ? true : false;
}

uint16_t __fastcall TForm1::calcChecksum(const void *pBuffer, int len)
{	// checksum for ICMP is calculated in the same way as for IP header

	const char *buffer = (const char *)pBuffer;
	uint32_t sum = 0;

	if (buffer && len > 0)
	{
		// make 16 bit words out of every two adjacent 8 bit words in the packet and add them up
		for (int i = 0; i < len; i += 2)
		{
			const uint16_t word = ((buffer[i + 0] << 8) & 0xFF00) + (buffer[i + 1] & 0xFF);
			sum += (uint32_t)word;
		}

		// take only 16 bits out of the 32 bit sum and add up the carries
		while (sum >> 16)
			sum = (sum & 0xFFFF) + (sum >> 16);
	}

	return (uint16_t)~sum;
}

bool __fastcall TForm1::validateChecksum(const void *pBuffer, int len)
{
	const char *buffer = (const char *)pBuffer;
	uint32_t sum = 0;

	if (buffer && len > 0)
	{
		// make 16 bit words out of every two adjacent 8 bit words in the packet and add them up
		for (int i = 0; i < len; i += 2)
		{
			const uint16_t word = ((buffer[i + 0] << 8) & 0xFF00) + (buffer [i + 1] & 0xFF);
			sum += (uint32_t)word;
		}

		// take only 16 bits out of the 32 bit sum and add up the carries
		while (sum >> 16)
			sum = (sum & 0xFFFF) + (sum >> 16);
	}

	// to validate the checksum on the received message we don't complement the sum of one's complement
	// One's complement the result
	//sum = ~sum;

	// the sum of one's complement should be 0xFFFF
	return ((uint16_t)sum == 0xFFFF) ? true : false;
}

void __fastcall TForm1::addMemoLine(String s)
{
	if (s.IsEmpty())
	{
		Memo1->Lines->Add(s);
	}
	else
	{
		//String dt_str = FormatDateTime("yyyy mm dd hh:nn:ss.zzz", Now());
		String dt_str = FormatDateTime("hh:nn:ss.zzz", Now());
		Memo1->Lines->Add(dt_str + "  " + s);
	}
}

bool __fastcall TForm1::resolveIP(String remoteHost, String &IPAddress)
{
	hostent *pHostent = gethostbyname(AnsiString(remoteHost).c_str());
	if (pHostent == NULL)
	{
		const int err = WSAGetLastError();
		String s = "An error occured in gethostbyname operation: error [" + IntToStr(err) + "] " + errorToStr(err);
		addMemoLine(s);
		return false;
	}

	in_addr in;
	memcpy(&in, pHostent->h_addr_list [0], sizeof(in_addr));
	IPAddress = String(inet_ntoa(in));

	return true;
}

bool __fastcall TForm1::StrToMAC(String str, uint8_t *mac)
{
	int colons = 0;
	int numbers = 0;
//	const char delim = ':';

	str = str.Trim();

	if (str.IsEmpty() || mac == NULL)
		return false;

	for (int i = 1; i <= str.Length(); )
	{
		int k = i;
		while (k <= str.Length() && str[k] != ':')
		{
			const char c = str[k++];
			if ((c < '0' || c > '9') && (c < 'a' || c > 'f') && (c < 'A' || c > 'F'))
				return false;
		}
		if (k < str.Length() && str[k] != ':')
			return false;

		String s = str.SubString(i, k - i);
		int num = -1;
		if (!TryStrToInt("0x" + s, num) || num < 0x00 || num > 0xff)
			return false;

		if (numbers < 6)
			mac[numbers] = num;
		numbers++;

		if (k < str.Length())
			colons++;

		i = k + 1;
	}

	return (numbers == 6 && colons == 5) ? true : false;
}

void __fastcall TForm1::finish(int index)
{
	if (index < 0 || index >= MAX_DEST)
		return;

	ping[index].stage = PING_STAGE_NONE;

	if (ping[index].sock != INVALID_SOCKET)
	{
		closesocket(ping[index].sock);
		ping[index].sock = INVALID_SOCKET;
	}

	WOL[index].secs = -1;

	switch (index)
	{
		case 0:
			WOLButton->Caption   = "Wake Up";
			StatusLabel->Caption = "Resting      ";
			MACEditA->Enabled    = true;
			MACEditB->Enabled    = true;
			IPEdit->Enabled      = true;
			break;

		default:
			break;
	}
}

bool __fastcall TForm1::wolSend(String mac_str1, String mac_str2, int index, int repeat_broadcasts)
{
	uint8_t mac_addr[2][6] = {0};

	if (index < 0 || index >= MAX_DEST)
		return false;

	mac_str1 = mac_str1.Trim();
	mac_str2 = mac_str2.Trim();

	if (repeat_broadcasts <  1) repeat_broadcasts = 1;
	else
	if (repeat_broadcasts > 10) repeat_broadcasts = 10;

	bool ok_mac[2];
	ok_mac[0] = StrToMAC(mac_str1, mac_addr[0]);
	ok_mac[1] = StrToMAC(mac_str2, mac_addr[1]);
	if (!ok_mac[0] && !ok_mac[1])
	{
		addMemoLine("error: invalid MAC addr(s)");
		return false;
	}

	WOL[index].mac1 = mac_str1;
	WOL[index].mac2 = mac_str2;

	bool sent = false;

	for (int mac = 0; mac < 2; mac++)
	{
		if (!ok_mac[mac])
			continue;

		SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock == INVALID_SOCKET)
		{
			const int err = WSAGetLastError();
			String s = "WOL socket error [" + IntToStr(err) + "] " + errorToStr(err);
			addMemoLine(s);
			continue;
		}

		const int optval = 1;
		int res = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&optval, sizeof(optval));
		if (res < 0)
		{
			const int err = WSAGetLastError();
			String s = "WOL set socket opt error [" + IntToStr(err) + "] " + errorToStr(err);
			addMemoLine(s);
			continue;
		}

		struct sockaddr_in addr;
		addr.sin_family           = AF_INET;
		addr.sin_addr.S_un.S_addr = inet_addr(BROADCAST_WOL_IP);
		addr.sin_port             = htons(BROADCAST_WOL_PORT);

		uint8_t packet[6 + (6 * 16) + 6];
		int k = 0;

		// ff:ff:ff:ff:ff:ff
		for (int i = 0; i < 6; i++)
			packet[k++] = 0xFF;

		// mac address repeated 16 times
		for (int i = 0; i < 16; i++)
			for (int j = 0; j < 6; j++)
				packet[k++] = mac_addr[mac][j];

		// 4 or 6 byte password
//		for (int i = 0; i < 6; i++)
//			packet[k++] = 0x00;

		sent = true;

		switch (index)
		{
			case 0:
				StatusLabel->Caption = "0 seconds      ";
				StatusLabel->Update();
				break;
			default:
				break;
		}

		for (int i = 0; i < repeat_broadcasts; i++)
		{
			String s1;
			s1.printf("WOL %02X:%02X:%02X:%02X:%02X:%02X port-%u .. ", mac_addr[mac][0], mac_addr[mac][1], mac_addr[mac][2], mac_addr[mac][3], mac_addr[mac][4], mac_addr[mac][5], ntohs(addr.sin_port));
			res = sendto(sock, packet, k, 0, (struct sockaddr *)&addr, sizeof(addr));
			if (res < 0)
			{
				const int err = WSAGetLastError();
				String s = s1 + "error [" + IntToStr(err) + "] " + errorToStr(err);
				addMemoLine(s);
			}
			else
				addMemoLine(s1 + "sent");
		}

		closesocket(sock);
	}

	if (sent)
		WOL[index].repeat_timer.mark();

	return sent;
}

bool __fastcall TForm1::pingStart(String addr, int index)
{
	if (index < 0 || index >= MAX_DEST)
		return false;

	if (ping[index].stage != PING_STAGE_NONE)
	{
		ping[index].stage = PING_STAGE_NONE;

		if (ping[index].sock != INVALID_SOCKET)
		{
			closesocket(ping[index].sock);
			ping[index].sock = INVALID_SOCKET;
		}
	}

	ping[index].addr = addr.Trim();
	if (ping[index].addr.IsEmpty())
		return false;

	if (!isValidIP(ping[index].addr))
	{	// not an IP address
		if (!resolveIP(ping[index].addr, ping[index].ip))
		{
			addMemoLine("Unable to resolve hostname for " + ping[index].addr);
			return false;
		}
	}
	else
		ping[index].ip = ping[index].addr;

	if (ping[index].ip == "127.0.0.1")
		return false;

	ping[index].stage = PING_STAGE_INIT;

	//while (ping[index].stage != PING_STAGE_NONE)
	//	pingProcess();

	return true;
}

void __fastcall TForm1::pingProcess()
{
	for (int index = 0; index < MAX_DEST; index++)
	{
		if (!thread)
			return;

		if (WOL[index].secs >= 0)
		{
			const double secs = WOL[index].hires_timer.secs(false);
			const double diff = secs - WOL[index].secs;
			if (diff >= 1)
			{	// update on-screen timer
				WOL[index].secs += 1;
				switch (index)
				{
					case 0:
						StatusLabel->Caption = IntToStr(IROUND(secs)) + " seconds      ";
						StatusLabel->Update();
						break;
					default:
						break;
				}
			}

			if (ping[index].stage != PING_STAGE_NONE && secs >= 120)
			{	// been pinging for 2 minutes .. time to give up
				addMemoLine("stopped pinging");
				finish(index);
			}

			// send another WOL incase the previous ones were not heard or seen
			if (WOL[index].repeat_timer.secs(false) >= 5)
				if (wolSend(WOL[index].mac1, WOL[index].mac2, index, 1))
					WOL[index].repeat_timer.mark();
		}

		switch (ping[index].stage)
		{
			default:
				addMemoLine("error: unknown ping state [" + IntToStr(index) + "]");
				finish(index);
				break;

			case PING_STAGE_NONE:
				if (ping[index].sock != INVALID_SOCKET)
				{
					closesocket(ping[index].sock);
					ping[index].sock = INVALID_SOCKET;
				}
				Sleep(1);
				break;

			case PING_STAGE_INIT:	// init ping
				ping[index].tx_buffer.resize(sizeof(ICMPheader) + ping[index].message_size);
				ping[index].rx_buffer.resize(1500);

				ping[index].total_round_trip_time = 0;

				ping[index].packets_tx = 0;
				ping[index].packets_rx = 0;

				ping[index].dest.sin_addr.S_un.S_addr = inet_addr(AnsiString(ping[index].ip).c_str());
				ping[index].dest.sin_family = AF_INET;
				ping[index].dest.sin_port = ++ping[index].source_port;		// random source port

				ping[index].tx_header.nId       = htons(++ping[index].id);
				ping[index].tx_header.byCode    = 0;		// zero for ICMP echo and reply messages
				ping[index].tx_header.nSequence = htons(++ping[index].sequence);
				ping[index].tx_header.byType    = 8;		// ICMP echo message .. https://www.iana.org/assignments/icmp-parameters/icmp-parameters.xhtml

				// message bytes - can be anything you want
				for (int i = 0; i < ping[index].message_size; i++)
					ping[index].tx_buffer[sizeof(ICMPheader) + i] = rand();

				// create a raw ICMP socket
				ping[index].sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
				if (ping[index].sock == INVALID_SOCKET)
				{
					const int err = WSAGetLastError();
					String s2 = "ping tx socket create error [" + IntToStr(err) + "] " + errorToStr(err);
					addMemoLine(s2);
					finish(index);
					break;
				}

				// onto next stage
				ping[index].stage = PING_STAGE_TX;

				break;

			case PING_STAGE_TX:	// send a ping
				if (ping[index].sock == INVALID_SOCKET)
				{
					finish(index);
					break;
				}

				if (ping[index].count >= 0 && ping[index].packets_tx >= ping[index].count)
				{
					ping[index].stage = PING_STAGE_DONE;
					break;
				}

				{
					// message bytes - can be anything you want
					//for (int i = 0; i < ping[index].message_size; i++)
					//	ping[index].tx_buffer[sizeof(ICMPheader) + i] = rand();

					// ICMP header
					ping[index].tx_header.nSequence = htons(++ping[index].sequence);
					ping[index].tx_header.nChecksum = 0;
					memcpy(&ping[index].tx_buffer[0], &ping[index].tx_header, sizeof(ICMPheader));
					ping[index].tx_header.nChecksum = htons(calcChecksum(&ping[index].tx_buffer[0], sizeof(ICMPheader) + ping[index].message_size));
					memcpy(&ping[index].tx_buffer[0], &ping[index].tx_header, sizeof(ICMPheader));

					const int num_bytes = sizeof(ICMPheader) + ping[index].message_size;

					String s = "ping tx " + ping[index].ip + " " + IntToStr(ping[index].packets_tx) + " " + IntToStr(num_bytes) + " .. ";

					const int res = sendto(ping[index].sock, &ping[index].tx_buffer[0], num_bytes, 0, (SOCKADDR *)&ping[index].dest, sizeof(SOCKADDR_IN));
					if (res == SOCKET_ERROR || res != num_bytes)
					{
						const int err = WSAGetLastError();
						String s2 = s + "error [" + IntToStr(err) + "] " + errorToStr(err);
						addMemoLine(s2);
						finish(index);
						break;
					}

					// save the time at which the ICMP echo message was sent
					ping[index].hires_timer.mark();

					ping[index].packets_tx++;

					ping[index].stage = PING_STAGE_RX;

					addMemoLine(s + "sent");
				}

			case PING_STAGE_RX:	// waiting for pong
				if (ping[index].sock == INVALID_SOCKET)
				{
					finish(index);
					break;
				}

				{
					String s;
					int res;
					fd_set fdRead;
					FD_ZERO(&fdRead);
					FD_SET(ping[index].sock, &fdRead);

					timeval time_interval = {0, 0};
					time_interval.tv_usec = (ping[index].hires_timer.millisecs(false) < 9) ? 10000 : 0;	// hang around for 10ms after the ping was initially sent
					//time_interval.tv_usec = ping[index].timeout_ms * 1000;

					res = select(0, &fdRead, NULL, NULL, &time_interval);
					if (res == SOCKET_ERROR || res < 0)
					{
						const int err = WSAGetLastError();
						String s2 = "ping rx select error [" + IntToStr(err) + "] " + errorToStr(err);
						addMemoLine(s2);
						finish(index);
						break;
					}

					if (res == 0)
					{
						if (ping[index].hires_timer.millisecs(false) >= ping[index].timeout_ms)	// timed out ?
						{
							//addMemoLine("ping rx time out 1");
							ping[index].stage = (ping[index].count < 0 || ping[index].packets_tx < ping[index].count) ? PING_STAGE_TX : PING_STAGE_DONE;
						}
						break;
					}

					if (!FD_ISSET(ping[index].sock, &fdRead))
					{
						if (ping[index].hires_timer.millisecs(false) >= ping[index].timeout_ms)	// timed out ?
						{
							//addMemoLine("ping rx time out 1");
							ping[index].stage = (ping[index].count < 0 || ping[index].packets_tx < ping[index].count) ? PING_STAGE_TX : PING_STAGE_DONE;
						}
						break;
					}

					res = recvfrom(ping[index].sock, &ping[index].rx_buffer[0], ping[index].rx_buffer.size(), 0, 0, 0);
					if (res == SOCKET_ERROR || res < 0)
					{
						const int err = WSAGetLastError();
						String s2 = "ping rx recvfrom error [" + IntToStr(err) + "] " + errorToStr(err);
						addMemoLine(s2);
						finish(index);
						break;
					}

					const double round_trip_time = ping[index].hires_timer.secs(false);

					// construct the IP header from the response
					IPheader ipHdr;
					memcpy(&ipHdr, &ping[index].rx_buffer[0], sizeof(IPheader));

					// ICMP message length is calculated by subtracting the IP header size from the total bytes received
					const int message_len = res - sizeof(IPheader);

					// skip the header
					const uint8_t *pICMPbuffer = &ping[index].rx_buffer[sizeof(IPheader)];

					// extract the ICMP header
					ICMPheader recvHdr;
					memcpy(&recvHdr, pICMPbuffer, sizeof(ICMPheader));
					//recvHdr.nId       = ntohs(recvHdr.nId);
					//recvHdr.nSequence = ntohs(recvHdr.nSequence);
					recvHdr.nChecksum = ntohs(recvHdr.nChecksum);

					const int num_bytes = res - sizeof(ICMPheader) - sizeof(IPheader);

					if (ping[index].dest.sin_addr.S_un.S_addr != ipHdr.nSrcAddr.ip32)
						break;   // not for us

					if (ipHdr.byProtocol != 1)
						break;   // not ICMP

					if (recvHdr.byType != 0)
						break;	// not an echo .. https://www.iana.org/assignments/icmp-parameters/icmp-parameters.xhtml

					if (!validateChecksum(pICMPbuffer, message_len))
					{
						s.printf("ping rx error: checksum");
						addMemoLine(s);
						break;
					}

					s.printf("ping rx received len:%d proto:%u src:%u.%u.%u.%u dst:%u.%u.%u.%u tos:%u ttl:%u",
						res,
						ipHdr.byProtocol,
						ipHdr.nSrcAddr.ip8[0], ipHdr.nSrcAddr.ip8[1], ipHdr.nSrcAddr.ip8[2], ipHdr.nSrcAddr.ip8[3],
						ipHdr.nDestAddr.ip8[0], ipHdr.nDestAddr.ip8[1], ipHdr.nDestAddr.ip8[2], ipHdr.nDestAddr.ip8[3],
						ipHdr.byTos,
						ipHdr.byTtl);
					addMemoLine(s);

					if (recvHdr.nId != ping[index].tx_header.nId)
					{
						s.printf("ping rx error: ID [%d]", recvHdr.nId);
						addMemoLine(s);
						break;
					}

					if (recvHdr.nSequence != ping[index].tx_header.nSequence)
					{
						s.printf("ping rx error: sequence [%d]", recvHdr.nSequence);
						addMemoLine(s);
						break;
					}

					if (num_bytes != ping[index].message_size)
					{
						s.printf("ping rx error: size [%d]", num_bytes);
						addMemoLine(s);
						break;
					}

					if (memcmp(&ping[index].tx_buffer[sizeof(ICMPheader)], &ping[index].rx_buffer[sizeof(IPheader) + sizeof(ICMPheader)], num_bytes) != 0)
					{
						s.printf("ping rx error: data");
						addMemoLine(s);
						break;
					}

					// rx ping appears OK

					ping[index].total_round_trip_time += round_trip_time;
					ping[index].packets_rx++;

					s = "ping rx " + ping[index].ip + ", " + IntToStr(num_bytes) + " bytes, " + FloatToStrF(round_trip_time * 1000, ffFixed, 0, 3) + "ms, TTL " + IntToStr((int)ipHdr.byTtl);
					addMemoLine(s);

					if (ping[index].count < 0)
					{	// pong rx'ed .. stop pinging

						addMemoLine("woked up");

						finish(index);

						if (dest_detected_wav.size() > 0)
						{
							DWORD flags = SND_MEMORY | SND_NODEFAULT | SND_NOWAIT;
							flags |= CloseOnWakeCheckBox->Checked ? SND_SYNC : SND_ASYNC;
							if (::PlaySound(&dest_detected_wav[0], NULL, flags) == FALSE)
								addMemoLine("play sound error: " + IntToStr((int)GetLastError()));
						}

						//if (CloseOnWakeToggleSwitch->State == tssOn)
						//	::PostMessage(this->Handle, WM_CLOSE, 0, 0);

						break;
					}

					// keep pinging .. onto next stage
					ping[index].stage = (ping[index].packets_tx < ping[index].count) ? PING_STAGE_TX : PING_STAGE_DONE;

					break;
				}

			case PING_STAGE_DONE:
				{
					String s = "Ping " + ping[index].ip + " TX=" + IntToStr(ping[index].packets_tx) + "  RX=" + IntToStr(ping[index].packets_rx);
					if (ping[index].packets_rx > 0)
					{
						String s2;
						s2.printf("  Time=%0.3fms", (ping[index].total_round_trip_time * 1000) / ping[index].packets_rx);
						s += s2;
					}
					addMemoLine(s);
				}
				finish(index);
				break;
		}
	}

	if (startup_timer.secs(false) >= 5 && CloseOnWakeCheckBox->Checked)
	{
		bool close = true;
		for (int index = 0; index < MAX_DEST && close; index++)
			if (ping[index].stage != PING_STAGE_NONE)
				close = false;
		if (close)
			::PostMessage(this->Handle, WM_CLOSE, 0, 0);
	}
}

void __fastcall TForm1::wakeyWakey(const int index)
{
	if (index < 0 || index >= MAX_DEST)
		return;

	if (ping[index].stage != PING_STAGE_NONE)
	{
		finish(index);
		return;
	}

	StatusLabel->Caption = "Resting      ";
	addMemoLine("");

	if (wolSend(MACEditA->Text, MACEditB->Text, index, 1))
	{
		WOL[index].hires_timer.mark();
		WOL[index].secs = 0;

		if (pingStart(IPEdit->Text, index))
		{
			MACEditA->Enabled  = false;
			MACEditB->Enabled  = false;
			IPEdit->Enabled    = false;
			WOLButton->Caption = "Stop";
		}
	}
}

void __fastcall TForm1::WOLButtonClick(TObject *Sender)
{
	wakeyWakey(0);
}

void __fastcall TForm1::Memo1DblClick(TObject *Sender)
{
	Memo1->Clear();
}

