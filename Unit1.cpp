
#include <vcl.h>

#include <inifiles.hpp>

#include <mmsystem.h>
#include <time.h>
#include <stdio.h>
#include <fastmath.h>
//#include <stdio.h>

#pragma hdrstop

#include "Unit1.h"

#define BROADCAST_WOL_IP		"255.255.255.255"
#define BROADCAST_WOL_PORT		9

#define WOL_REPEAT_SECS       8.0

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
			m_ini_filename = ChangeFileExt(Application->ExeName, ".ini");
	}

	m_thread = NULL;

	{
//		srand(time(0));
		SYSTEMTIME time;
		::GetSystemTime(&time);
		srand(time.wMilliseconds);
	}

	::GetSystemInfo(&m_sys_info);

	this->DoubleBuffered  = true;
	Memo1->DoubleBuffered = true;

	// help stop flicker
	this->ControlStyle  = this->ControlStyle  << csOpaque;
	Memo1->ControlStyle = Memo1->ControlStyle << csOpaque;

	m_dest_detected_wav_filename = IncludeTrailingPathDelimiter(ExtractFilePath(Application->ExeName)) + "dong.wav";
	//m_dest_detected_wav_filename = work_dir + "dong.wav";

	Memo1->Clear();

	DeviceComboBox->Clear();
	StatusLabel->Caption = "Resting      ";
	MACEditA->Text       = "";
	MACEditB->Text       = "";
	AddrEdit->Text       = "";

	loadSettings();

	// initialise WinSock
	memset(&m_wsaData, 0, sizeof(m_wsaData));
	if (WSAStartup(MAKEWORD(2, 2), &m_wsaData) == SOCKET_ERROR)
	{
		const int err = WSAGetLastError();
		s = "WSAStartup error [" + IntToStr(err) + "] " + errorToStr(err);
		MessageDlg(s, mtError, TMsgDlgButtons() << mbCancel, 0);
		Close();
	}
	//printfCommMessage("%04X.%04X", m_wsaData.wVersion, m_wsaData.wHighVersion);
	//LOBYTE(m_wsaData.wVersion) != 2 || HIBYTE(m_wsaData.wVersion) != 2

	{	// first try to load the external file. if the external file is not present then use the built-in resourced file
		m_dest_detected_wav.resize(0);

		FILE *fin = fopen(AnsiString(m_dest_detected_wav_filename).c_str(), "rb");
		if (fin != NULL)
		{
			if (fseek(fin, 0, SEEK_END) == 0)
			{
				const size_t file_size = ftell(fin);
				if (file_size > MIN_WAV_SIZE)
				{
					if (fseek(fin, 0, SEEK_SET) == 0)
					{
						m_dest_detected_wav.resize(file_size);
						if (fread(&m_dest_detected_wav[0], 1, file_size, fin) != file_size)
							m_dest_detected_wav.resize(0);
					}
				}
			}
			fclose(fin);
		}

		if (m_dest_detected_wav.empty())
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
								m_dest_detected_wav.resize(dwSize);
								memmove(&m_dest_detected_wav[0], p_res_data, dwSize);
							}
						}
					}
				}
			}
		}
	}

	Timer1->Enabled = true;

	::PostMessage(this->Handle, WM_INIT_GUI, 0, 0);
}

void __fastcall TForm1::FormDestroy(TObject *Sender)
{
	Timer1->Enabled = false;
	
	if (m_thread != NULL)
	{
		m_thread->Terminate();
		m_thread->WaitFor();
		delete m_thread;
		m_thread = NULL;
	}

	for (unsigned int i = 0; i < m_device.size(); i++)
	{
		if (m_device[i].ping.sock != INVALID_SOCKET)
		{
			closesocket(m_device[i].ping.sock);
			m_device[i].ping.sock = INVALID_SOCKET;
		}
	}

	if (m_wsaData.wVersion != 0)
		if (WSACleanup() == SOCKET_ERROR)
			MessageDlg("WSACleanup() error " + IntToStr(WSAGetLastError()), mtError, TMsgDlgButtons() << mbCancel, 0);
}

void __fastcall TForm1::FormClose(TObject *Sender, TCloseAction &Action)
{
	if (m_thread != NULL)
	{
		m_thread->Terminate();
		m_thread->WaitFor();
		delete m_thread;
		m_thread = NULL;
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
	m_thread = new CThread(&ThreadProcess, tpNormal, true);
//	if (m_thread == NULL)
//		stop();

	m_startup_timer.mark();

	for (unsigned int i = 0; i < m_device.size(); i++)
		if (m_device[i].wake_on_start)
			pingStart(i);
}

void __fastcall TForm1::WMUpdateStatus(TMessage &msg)
{
	const unsigned int index = msg.WParam;
	if (index < m_device.size())
	{
		const double secs = m_device[index].hires_timer.secs(false);
		StatusLabel->Caption = IntToStr(IROUND(secs)) + " seconds      ";
		StatusLabel->Update();
	}
}

void __fastcall TForm1::updateDeviceComboBox()
{
	DeviceComboBox->Items->BeginUpdate();
		DeviceComboBox->Clear();
		for (unsigned int i = 0; i < m_device.size() ; i++)
			DeviceComboBox->Items->AddObject(m_device[i].name, (TObject *)i);
	DeviceComboBox->Items->EndUpdate();
}

void __fastcall TForm1::loadSettings()
{
	int i;
	float f;
	String s;
	bool b;

	TIniFile *ini = new TIniFile(m_ini_filename);
	if (ini == NULL)
		return;

	Top    = ini->ReadInteger("MainForm", "Top",    Top);
	Left   = ini->ReadInteger("MainForm", "Left",   Left);
	Width  = ini->ReadInteger("MainForm", "Width",  Width);
	Height = ini->ReadInteger("MainForm", "Height", Height);

	m_dest_detected_wav_filename = ini->ReadString("Misc", "DEST_DETECTED_WAV", m_dest_detected_wav_filename);

	CloseOnWakeCheckBox->Checked     = ini->ReadBool("Misc", "CloseOnWake",     CloseOnWakeCheckBox->Checked);
	PlaySoundOnWakeCheckBox->Checked = ini->ReadBool("Misc", "PlaySoundOnWake", PlaySoundOnWakeCheckBox->Checked);

	m_device.clear();
	for (unsigned int device_num = 0; ; device_num++)
	{
		String section = "Device_" + IntToStr(device_num);
		if (!ini->SectionExists(section))
			break;

		String name = ini->ReadString(section, "NAME", "");
		if (!name.IsEmpty())
		{
			Device device;
			device.name          = name;
			device.addr          = ini->ReadString(section, "ADDR", "");
			device.mac_1         = ini->ReadString(section, "MAC1", "");
			device.mac_2         = ini->ReadString(section, "MAC2", "");
			device.wake_on_start = ini->ReadBool(section, "WakeOnStart", device.wake_on_start);
			m_device.push_back(device);
		}
	}

	updateDeviceComboBox();

	const int device_num = ini->ReadInteger("Device", "Selected", DeviceComboBox->ItemIndex);
	if (DeviceComboBox->Items->Count > device_num)
	{
		DeviceComboBox->ItemIndex = device_num;
		DeviceComboBoxSelect(DeviceComboBox);
	}
	else
	if (DeviceComboBox->Items->Count > 0)
	{
		DeviceComboBox->ItemIndex = 0;
		DeviceComboBoxSelect(DeviceComboBox);
	}

	delete ini;
}

void __fastcall TForm1::saveSettings()
{
	String s;

	DeleteFile(m_ini_filename);

	TIniFile *ini = new TIniFile(m_ini_filename);
	if (ini == NULL)
		return;

	ini->WriteInteger("MainForm", "Top",    Top);
	ini->WriteInteger("MainForm", "Left",   Left);
	ini->WriteInteger("MainForm", "Width",  Width);
	ini->WriteInteger("MainForm", "Height", Height);

	ini->WriteString("Misc", "DEST_DETECTED_WAV", m_dest_detected_wav_filename);

	ini->WriteBool("Misc", "CloseOnWake",     CloseOnWakeCheckBox->Checked);
	ini->WriteBool("Misc", "PlaySoundOnWake", PlaySoundOnWakeCheckBox->Checked);

	for (unsigned int device_num = 0; device_num < m_device.size(); device_num++)
	{
		String section = "Device_" + IntToStr(device_num);
		ini->WriteString(section, "NAME", m_device[device_num].name);
		ini->WriteString(section, "ADDR", m_device[device_num].addr);
		ini->WriteString(section, "MAC1", m_device[device_num].mac_1);
		ini->WriteString(section, "MAC2", m_device[device_num].mac_2);
		ini->WriteBool(section, "WakeOnStart", m_device[device_num].wake_on_start);
	}

	ini->WriteInteger("Device", "Selected", DeviceComboBox->ItemIndex);

	delete ini;
}

void __fastcall TForm1::clearCommMessages()
{
	CCriticalSection cs(m_messages.cs);
	m_messages.list.resize(0);
}

unsigned int __fastcall TForm1::commMessagesCount()
{
	CCriticalSection cs(m_messages.cs);
	return m_messages.list.size();
}

void TForm1::printfCommMessage(const char *fmt, ...)
{
	if (fmt == NULL)
		return;

	va_list ap;
	char tmp;

	va_start(ap, fmt);
		int buf_size = vsnprintf(&tmp, 0, fmt, ap);
	va_end(ap);

	if (buf_size == 0)
		return;

	if (buf_size == -1)
		buf_size = 512;

	char *buf = new char [buf_size + 1];
	if (buf == NULL)
		return;

	va_start(ap, fmt);
		vsnprintf(buf, buf_size + 1, fmt, ap);
	va_end(ap);

	String s = String(buf);

	delete buf;

	if (!s.IsEmpty())
	{
		CCriticalSection cs(m_messages.cs);
		m_messages.list.push_back(s);
	}
}

void __fastcall TForm1::pushCommMessage(String s)
{
	if (!s.IsEmpty())
	{
		CCriticalSection cs(m_messages.cs);
		m_messages.list.push_back(s);
	}
}

String __fastcall TForm1::pullCommMessage()
{
	String s;
	CCriticalSection cs(m_messages.cs);
	if (!m_messages.list.empty())
	{
		s = m_messages.list[0];
		m_messages.list.erase(m_messages.list.begin() + 0);
	}
	return s;
}

void __fastcall TForm1::FormKeyDown(TObject *Sender, WORD &Key,
      TShiftState Shift)
{
	// https://docs.microsoft.com/en-us/windows/desktop/inputdev/virtual-key-codes
	switch (Key)
	{
		case VK_ESCAPE:
			Key = 0;
			//if (m_thread)
			//{
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
	if (m_thread != NULL)
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
		m_messages.list.push_back(s);
	}
	else
	{
		//String dt_str = FormatDateTime("yyyy mm dd hh:nn:ss.zzz", Now());
		String dt_str = FormatDateTime("hh:nn:ss.zzz", Now());
		m_messages.list.push_back(dt_str + "  " + s);
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

void __fastcall TForm1::finish(const unsigned int index, const t_ping_stage ping_stage)
{
	if (index >= m_device.size())
		return;

	Device *device = &m_device[index];

	device->ping.stage = ping_stage;

	if (device->ping.sock != INVALID_SOCKET)
	{
		closesocket(device->ping.sock);
		device->ping.sock = INVALID_SOCKET;
	}

	device->secs = -1;
	device->ping.woke_up_timer.mark();

	WOLButton->Caption           = "Wake Up";
	DeviceComboBox->Enabled      = true;
	StatusLabel->Caption         = "Resting      ";
	MACEditA->Enabled            = true;
	MACEditB->Enabled            = true;
	AddrEdit->Enabled            = true;
	WakeOnStartCheckBox->Enabled = true;
}

bool __fastcall TForm1::wolSend(const unsigned int index, int repeat_broadcasts)
{
	uint8_t mac_addr[2][6] = {0};

	if (index >= m_device.size())
		return false;

	Device *device = &m_device[index];

	String mac_str1 = device->mac_1.Trim();
	String mac_str2 = device->mac_2.Trim();

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

		for (int i = 0; i < repeat_broadcasts; i++)
		{
			String s;
			s.printf("tx WOL %02X:%02X:%02X:%02X:%02X:%02X port-%u", mac_addr[mac][0], mac_addr[mac][1], mac_addr[mac][2], mac_addr[mac][3], mac_addr[mac][4], mac_addr[mac][5], ntohs(addr.sin_port));
			res = sendto(sock, packet, k, 0, (struct sockaddr *)&addr, sizeof(addr));
			if (res < 0)
			{
				const int err = WSAGetLastError();
				s += "error [" + IntToStr(err) + "] " + errorToStr(err);
			}
			addMemoLine(s);
		}

		closesocket(sock);
	}

	if (sent)
		device->repeat_timer.mark();

	return sent;
}

bool __fastcall TForm1::pingStart(const unsigned int index)
{
	if (index >= m_device.size())
		return false;

	if (m_device[index].ping.stage != PING_STAGE_NONE)
	{
		m_device[index].ping.stage = PING_STAGE_NONE;

		if (m_device[index].ping.sock != INVALID_SOCKET)
		{
			closesocket(m_device[index].ping.sock);
			m_device[index].ping.sock = INVALID_SOCKET;
		}
	}

	String addr = m_device[index].addr.Trim();
	if (addr.IsEmpty())
		return false;

	if (!isValidIP(addr))
	{	// not an IP address - must be a DNS address
		if (!resolveIP(addr, m_device[index].ping.ip))
		{
			addMemoLine("Unable to resolve hostname for " + addr);
			return false;
		}
	}
	else
		m_device[index].ping.ip = addr;

	if (m_device[index].ping.ip == "127.0.0.1")
		return false;

	m_device[index].ping.stage = PING_STAGE_INIT;

	//while (m_device[index].ping.stage != PING_STAGE_NONE)
	//	pingProcess();

	return true;
}

void __fastcall TForm1::pingProcess()
{
	for (unsigned int index = 0; index < m_device.size(); index++)
	{
		if (!m_thread)
			return;

		Device *device = &m_device[index];

		const double secs = device->hires_timer.secs(false);
		const double diff = secs - device->secs;

		if (device->ping.stage != PING_STAGE_NONE && device->ping.stage != PING_STAGE_WOKE_UP)
		{
			if (diff >= 1.0)
			{	// update on-screen timer
				device->secs += 1.0;
				::PostMessage(this->Handle, WM_UPDATE_STATUS, index, 0);
			}
		}

		switch (device->ping.stage)
		{
			default:
				addMemoLine("error: unknown ping state [" + IntToStr(index) + "]");
				finish(index, PING_STAGE_NONE);
				break;

			case PING_STAGE_NONE:
			case PING_STAGE_WOKE_UP:
				if (device->ping.sock != INVALID_SOCKET)
				{
					closesocket(device->ping.sock);
					device->ping.sock = INVALID_SOCKET;
				}
				Sleep(1);
				break;

			case PING_STAGE_INIT:	// init ping
				device->ping.tx_buffer.resize(sizeof(ICMPheader) + device->ping.message_size);
				device->ping.rx_buffer.resize(1500);

				device->ping.total_round_trip_time = 0;

				device->ping.packets_tx = 0;
				device->ping.packets_rx = 0;

				device->ping.dest.sin_addr.S_un.S_addr = inet_addr(AnsiString(device->ping.ip).c_str());
				device->ping.dest.sin_family = AF_INET;
				device->ping.dest.sin_port = ++device->ping.source_port;		// random source port

				device->ping.tx_header.nId       = htons(++device->ping.id);
				device->ping.tx_header.byCode    = 0;		// zero for ICMP echo and reply messages
				device->ping.tx_header.nSequence = htons(++device->ping.sequence);
				device->ping.tx_header.byType    = 8;		// ICMP echo message .. https://www.iana.org/assignments/icmp-parameters/icmp-parameters.xhtml

				// message bytes - can be anything you want
				for (int i = 0; i < device->ping.message_size; i++)
					device->ping.tx_buffer[sizeof(ICMPheader) + i] = rand();

				// create a raw ICMP socket
				device->ping.sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
				if (device->ping.sock == INVALID_SOCKET)
				{
					const int err = WSAGetLastError();
					String s2 = "tx ping socket create error [" + IntToStr(err) + "] " + errorToStr(err);
					addMemoLine(s2);
					finish(index, PING_STAGE_NONE);
					break;
				}

				// onto next stage
				device->ping.stage = PING_STAGE_TX;

				break;

			case PING_STAGE_TX:	// send a ping
				if (device->ping.sock == INVALID_SOCKET)
				{
					finish(index, PING_STAGE_NONE);
					break;
				}

				if (device->ping.count >= 0 && device->ping.packets_tx >= device->ping.count)
				{
					device->ping.stage = PING_STAGE_DONE;
					break;
				}

				if (secs >= 120.0)
				{	// been pinging for 2 minutes .. time to give up
					addMemoLine("no response - stopped pinging");
					finish(index, PING_STAGE_NONE);
					break;
				}

				// send another WOL incase the previous ones weren't heard
				if (device->repeat_timer.secs(false) >= WOL_REPEAT_SECS)
					if (wolSend(index, 1))
						device->repeat_timer.mark();

				{
					// message bytes - can be anything you want
					//for (int i = 0; i < device->ping.message_size; i++)
					//	device->ping.tx_buffer[sizeof(ICMPheader) + i] = rand();

					// ICMP header
					device->ping.tx_header.nSequence = htons(++device->ping.sequence);
					device->ping.tx_header.nChecksum = 0;
					memcpy(&device->ping.tx_buffer[0], &device->ping.tx_header, sizeof(ICMPheader));
					device->ping.tx_header.nChecksum = htons(calcChecksum(&device->ping.tx_buffer[0], sizeof(ICMPheader) + device->ping.message_size));
					memcpy(&device->ping.tx_buffer[0], &device->ping.tx_header, sizeof(ICMPheader));

					const int num_bytes = sizeof(ICMPheader) + device->ping.message_size;

					String s = "tx ping " + device->ping.ip + " " + IntToStr(device->ping.packets_tx) + " " + IntToStr(num_bytes);

					const int res = sendto(device->ping.sock, &device->ping.tx_buffer[0], num_bytes, 0, (SOCKADDR *)&device->ping.dest, sizeof(SOCKADDR_IN));
					if (res == SOCKET_ERROR || res != num_bytes)
					{
						const int err = WSAGetLastError();
						s += " .. error [" + IntToStr(err) + "] " + errorToStr(err);
						addMemoLine(s);
						finish(index, PING_STAGE_NONE);
						break;
					}
					else
						addMemoLine(s);

					// save the time at which the ICMP echo message was sent
					device->ping.hires_timer.mark();

					device->ping.packets_tx++;

					device->ping.stage = PING_STAGE_RX;
				}

			case PING_STAGE_RX:	// waiting for pong
				if (device->ping.sock == INVALID_SOCKET)
				{
					finish(index, PING_STAGE_NONE);
					break;
				}

				{
					String s;
					int res;
					fd_set fdRead;
					FD_ZERO(&fdRead);
					FD_SET(device->ping.sock, &fdRead);

					timeval time_interval = {0, 0};
					time_interval.tv_usec = (device->ping.hires_timer.millisecs(false) < 9) ? 10000 : 0;	// hang around for 10ms after the ping was initially sent
					//time_interval.tv_usec = device->ping.timeout_ms * 1000;

					res = select(0, &fdRead, NULL, NULL, &time_interval);
					if (res == SOCKET_ERROR || res < 0)
					{
						const int err = WSAGetLastError();
						String s2 = "rx ping select error [" + IntToStr(err) + "] " + errorToStr(err);
						addMemoLine(s2);
						finish(index, PING_STAGE_NONE);
						break;
					}

					if (res == 0)
					{
						if (device->ping.hires_timer.millisecs(false) >= device->ping.timeout_ms)	// timed out ?
						{
							//addMemoLine("rx ping time out 1");
							device->ping.stage = (device->ping.count < 0 || device->ping.packets_tx < device->ping.count) ? PING_STAGE_TX : PING_STAGE_DONE;
						}
						break;
					}

					if (!FD_ISSET(device->ping.sock, &fdRead))
					{
						if (device->ping.hires_timer.millisecs(false) >= device->ping.timeout_ms)	// timed out ?
						{
							//addMemoLine("rx ping time out 1");
							device->ping.stage = (device->ping.count < 0 || device->ping.packets_tx < device->ping.count) ? PING_STAGE_TX : PING_STAGE_DONE;
						}
						break;
					}

					res = recvfrom(device->ping.sock, &device->ping.rx_buffer[0], device->ping.rx_buffer.size(), 0, 0, 0);
					if (res == SOCKET_ERROR || res < 0)
					{
						const int err = WSAGetLastError();
						String s2 = "rx ping recvfrom error [" + IntToStr(err) + "] " + errorToStr(err);
						addMemoLine(s2);
						finish(index, PING_STAGE_NONE);
						break;
					}

					const double round_trip_time = device->ping.hires_timer.secs(false);

					// construct the IP header from the response
					IPheader ipHdr;
					memcpy(&ipHdr, &device->ping.rx_buffer[0], sizeof(IPheader));

					// ICMP message length is calculated by subtracting the IP header size from the total bytes received
					const int message_len = res - sizeof(IPheader);

					// skip the header
					const uint8_t *pICMPbuffer = &device->ping.rx_buffer[sizeof(IPheader)];

					// extract the ICMP header
					ICMPheader recvHdr;
					memcpy(&recvHdr, pICMPbuffer, sizeof(ICMPheader));
					//recvHdr.nId       = ntohs(recvHdr.nId);
					//recvHdr.nSequence = ntohs(recvHdr.nSequence);
					recvHdr.nChecksum = ntohs(recvHdr.nChecksum);

					const int num_bytes = res - sizeof(ICMPheader) - sizeof(IPheader);

					if (device->ping.dest.sin_addr.S_un.S_addr != ipHdr.nSrcAddr.ip32)
						break;   // not for us

					if (ipHdr.byProtocol != 1)
						break;   // not ICMP

					if (recvHdr.byType != 0)
						break;	// not an echo .. https://www.iana.org/assignments/icmp-parameters/icmp-parameters.xhtml

					if (!validateChecksum(pICMPbuffer, message_len))
					{
						s.printf("rx ping error: checksum");
						addMemoLine(s);
						break;
					}

					s.printf("rx ping received len:%d proto:%u src:%u.%u.%u.%u dst:%u.%u.%u.%u tos:%u ttl:%u",
						res,
						ipHdr.byProtocol,
						ipHdr.nSrcAddr.ip8[0], ipHdr.nSrcAddr.ip8[1], ipHdr.nSrcAddr.ip8[2], ipHdr.nSrcAddr.ip8[3],
						ipHdr.nDestAddr.ip8[0], ipHdr.nDestAddr.ip8[1], ipHdr.nDestAddr.ip8[2], ipHdr.nDestAddr.ip8[3],
						ipHdr.byTos,
						ipHdr.byTtl);
					addMemoLine(s);

					if (recvHdr.nId != device->ping.tx_header.nId)
					{
						s.printf("rx ping error: ID [%d]", recvHdr.nId);
						addMemoLine(s);
						break;
					}

					if (recvHdr.nSequence != device->ping.tx_header.nSequence)
					{
						s.printf("rx ping error: sequence [%d]", recvHdr.nSequence);
						addMemoLine(s);
						break;
					}

					if (num_bytes != device->ping.message_size)
					{
						s.printf("rx ping error: size [%d]", num_bytes);
						addMemoLine(s);
						break;
					}

					if (memcmp(&device->ping.tx_buffer[sizeof(ICMPheader)], &device->ping.rx_buffer[sizeof(IPheader) + sizeof(ICMPheader)], num_bytes) != 0)
					{
						s.printf("rx ping error: data");
						addMemoLine(s);
						break;
					}

					// rx ping appears OK

					device->ping.total_round_trip_time += round_trip_time;
					device->ping.packets_rx++;

					device->ping.woke_up_timer.mark();

					s = "rx ping " + device->ping.ip + ", " + IntToStr(num_bytes) + " bytes, " + FloatToStrF(round_trip_time * 1000, ffFixed, 0, 3) + "ms, TTL " + IntToStr((int)ipHdr.byTtl);
					addMemoLine(s);

					if (device->ping.count < 0)
					{	// pong rx'ed .. stop pinging

						addMemoLine("woked up");

						finish(index, PING_STAGE_WOKE_UP);

						if (m_dest_detected_wav.size() > 0 && PlaySoundOnWakeCheckBox->Checked)
						{
							DWORD flags;
							//if (m_startup_timer.secs(false) < (5 - 2))
								flags = SND_MEMORY | SND_NODEFAULT | SND_NOWAIT | SND_ASYNC;	// don't wait till the sound has finished
							//else
							//	flags = SND_MEMORY | SND_NODEFAULT | SND_NOWAIT | SND_SYNC;		// wait till the sound has finished
							if (::PlaySound(&m_dest_detected_wav[0], NULL, flags) == FALSE)
								addMemoLine("play sound error: " + IntToStr((int)GetLastError()));
						}

						//if (CloseOnWakeToggleSwitch->State == tssOn)
						//	::PostMessage(this->Handle, WM_CLOSE, 0, 0);

						break;
					}

					// keep pinging .. onto next stage
					device->ping.stage = (device->ping.packets_tx < device->ping.count) ? PING_STAGE_TX : PING_STAGE_DONE;

					break;
				}

			case PING_STAGE_DONE:
				{
					String s = "Ping " + device->ping.ip + " TX=" + IntToStr(device->ping.packets_tx) + "  RX=" + IntToStr(device->ping.packets_rx);
					if (device->ping.packets_rx > 0)
					{
						String s2;
						s2.printf("  Time=%0.3fms", (device->ping.total_round_trip_time * 1000) / device->ping.packets_rx);
						s += s2;
					}
					addMemoLine(s);
				}
				finish(index, PING_STAGE_NONE);
				break;
		}
	}

	if (CloseOnWakeCheckBox->Checked)
	{
		bool close = false;
		for (unsigned int i = 0; i < m_device.size(); i++)
		{
			Device *device = &m_device[i];
			if (device->ping.stage == PING_STAGE_WOKE_UP && device->ping.woke_up_timer.secs(false) >= 3.0)
				close = true;
		}
		if (close)
			::PostMessage(this->Handle, WM_CLOSE, 0, 0);		// close this program
	}
}

void __fastcall TForm1::wakeyWakey(const unsigned int index)
{
	if (index >= m_device.size())
		return;

	Device *device = &m_device[index];

	if (device->ping.stage != PING_STAGE_NONE)
		finish(index, PING_STAGE_NONE);

	StatusLabel->Caption = "Resting      ";
	addMemoLine("");

	if (wolSend(index, 1))
	{
		device->hires_timer.mark();
		device->secs = 0;

		StatusLabel->Caption = "0 seconds      ";
		StatusLabel->Update();

		if (pingStart(index))
		{
			DeviceComboBox->Enabled      = false;
			MACEditA->Enabled            = false;
			MACEditB->Enabled            = false;
			AddrEdit->Enabled            = false;
			WakeOnStartCheckBox->Enabled = false;
			WOLButton->Caption           = "Stop";
		}
	}
}

void __fastcall TForm1::WOLButtonClick(TObject *Sender)
{
	const int device_num = DeviceComboBox->ItemIndex;
	if (device_num < 0)
		return;

	wakeyWakey(device_num);
}

void __fastcall TForm1::Memo1DblClick(TObject *Sender)
{
	Memo1->Clear();
}

void __fastcall TForm1::Timer1Timer(TObject *Sender)
{
	{
		int num_done = 0;
		int num;
		while ((num = commMessagesCount()) > 0)
		{
			String s = pullCommMessage();
			Memo1->Lines->Add(s);
			if (++num_done >= 10)	// max of 10 lines in one go - to give time for the rest of the system to do it's thing
				break;
		}
	}
}

void __fastcall TForm1::DeviceComboBoxChange(TObject *Sender)
{
	String s = DeviceComboBox->Text.Trim().LowerCase();

	// check to see if it exists
	unsigned int i = 0;
	while (i < m_device.size())
	{
		if (m_device[i].name.LowerCase() == s)
			break;
		i++;
	}

	if (i < m_device.size())
	{	// it exists
		AddrEdit->Text               = m_device[i].addr.Trim();
		MACEditA->Text               = m_device[i].mac_1.Trim();
		MACEditB->Text               = m_device[i].mac_2.Trim();
		WakeOnStartCheckBox->Checked = m_device[i].wake_on_start;
	}
}

void __fastcall TForm1::SaveButtonClick(TObject *Sender)
{
	String s = DeviceComboBox->Text.Trim();
	if (s.IsEmpty())
		return;
		
	// check to see if it already exists
	unsigned int i = 0;
	while (i < m_device.size())
	{
		if (m_device[i].name.LowerCase() == s.LowerCase())
			break;
		i++;
	}

	if (i < m_device.size())
	{	// it already exists .. update the entry
		m_device[i].name          = s;
		m_device[i].addr          = AddrEdit->Text.Trim();
		m_device[i].mac_1         = MACEditA->Text.Trim();
		m_device[i].mac_2         = MACEditB->Text.Trim();
		m_device[i].wake_on_start = WakeOnStartCheckBox->Checked;
	}
	else
	{	// add a new entry
		Device device;
		device.name          = s;
		device.addr          = AddrEdit->Text.Trim();
		device.mac_1         = MACEditA->Text.Trim();
		device.mac_2         = MACEditB->Text.Trim();
		device.wake_on_start = WakeOnStartCheckBox->Checked;
		m_device.push_back(device);

		DeviceComboBox->Items->BeginUpdate();
			DeviceComboBox->Items->AddObject(s, (TObject *)i);
		DeviceComboBox->Items->EndUpdate();
		DeviceComboBox->ItemIndex = i - 1;
	}
}

void __fastcall TForm1::DeleteButtonClick(TObject *Sender)
{
	String s = DeviceComboBox->Text.Trim().LowerCase();
	if (s.IsEmpty())
		return;

	const int device_num = DeviceComboBox->ItemIndex;

	for (unsigned int i = 0; i < m_device.size(); )
	{
		if (m_device[i].name.LowerCase() == s)
			m_device.erase(m_device.begin() + i);
		else
			i++;
	}

	updateDeviceComboBox();

	if (device_num >= 0)
	{
		if (DeviceComboBox->Items->Count > device_num)
		{
			DeviceComboBox->ItemIndex = device_num;
			DeviceComboBoxSelect(DeviceComboBox);
		}
		else
		if (DeviceComboBox->Items->Count > 0)
		{
			DeviceComboBox->ItemIndex = 0;
			DeviceComboBoxSelect(DeviceComboBox);
		}
	}
	else
	{
		if (DeviceComboBox->Items->Count > 0)
		{
			DeviceComboBox->ItemIndex = 0;
			DeviceComboBoxSelect(DeviceComboBox);
		}
	}
}

void __fastcall TForm1::DeviceComboBoxSelect(TObject *Sender)
{
	String s = DeviceComboBox->Text.Trim().LowerCase();

	// check to see if it exists
	unsigned int i = 0;
	while (i < m_device.size())
	{
		if (m_device[i].name.LowerCase() == s)
			break;
		i++;
	}

	if (i < m_device.size())
	{	// it exists
		MACEditA->Text               = m_device[i].mac_1.Trim();
		MACEditB->Text               = m_device[i].mac_2.Trim();
		AddrEdit->Text               = m_device[i].addr.Trim();
		WakeOnStartCheckBox->Checked = m_device[i].wake_on_start;
	}
}

void __fastcall TForm1::CloseOnWakeCheckBoxClick(TObject *Sender)
{
	for (unsigned int i = 0; i < m_device.size(); i++)
	{
		Device *device = &m_device[i];
		device->ping.woke_up_timer.mark();
	}
}

void __fastcall TForm1::WakeOnStartCheckBoxClick(TObject *Sender)
{
	SaveButtonClick(SaveButton);
}


void __fastcall TForm1::AddrEditKeyDown(TObject *Sender, WORD &Key,
		TShiftState Shift)
{
	SaveButtonClick(SaveButton);
}

void __fastcall TForm1::MACEditBKeyDown(TObject *Sender, WORD &Key,
		TShiftState Shift)
{
	SaveButtonClick(SaveButton);
}

void __fastcall TForm1::MACEditAKeyDown(TObject *Sender, WORD &Key,
      TShiftState Shift)
{
	SaveButtonClick(SaveButton);
}

void __fastcall TForm1::DeviceComboBoxKeyDown(TObject *Sender, WORD &Key,
      TShiftState Shift)
{
	if (Key == VK_RETURN)
	{
		Key = 0;

		String s = DeviceComboBox->Text.Trim();
		if (!s.IsEmpty())
		{
			SaveButtonClick(SaveButton);

			// find it
			unsigned int i = 0;
			while (i < m_device.size())
			{
				if (m_device[i].name.LowerCase() == s.LowerCase())
					break;
				i++;
			}
			if (i < m_device.size())
				DeviceComboBox->ItemIndex = i;
		}
	}
}

