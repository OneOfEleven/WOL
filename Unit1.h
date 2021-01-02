
#ifndef Unit1H
#define Unit1H

#include <Classes.hpp>
#include <Controls.hpp>
#include <StdCtrls.hpp>
#include <Forms.hpp>

#define VC_EXTRALEAN
#define WIN32_EXTRA_LEAN
#define WIN32_LEAN_AND_MEAN

#include <stdint.h>
#include <vector>

#include <winsock2.h>

//#include <windows.h>
#include <Windows.hpp>

//#include <wininet.h>
#pragma comment (lib, "wininet.lib")

#include "CriticalSection.h"
#include "HighResolutionTick.h"

#define WM_INIT_GUI		(WM_USER + 100)

#define MAX_DEST        2

struct TVersion
{
	uint16_t MajorVer;
	uint16_t MinorVer;
	uint16_t ReleaseVer;
	uint16_t BuildVer;
	TVersion() : MajorVer(0), MinorVer(0), ReleaseVer(0), BuildVer(0) {}
};

struct ICMPheader
{
	uint8_t  byType;
	uint8_t  byCode;
	uint16_t nChecksum;
	uint16_t nId;
	uint16_t nSequence;
};

struct IPheader
{
	uint8_t  byVerLen;
	uint8_t  byTos;
	uint16_t nTotalLength;
	uint16_t nId;
	uint16_t nOffset;
	uint8_t  byTtl;
	uint8_t  byProtocol;
	uint16_t nChecksum;
	union
	{
		uint32_t ip32;
		uint8_t ip8[4];
	} nSrcAddr;
	union
	{
		uint32_t ip32;
		uint8_t ip8[4];
	} nDestAddr;
};

enum t_ping_stage
{
	PING_STAGE_NONE = 0,
	PING_STAGE_INIT = 1,
	PING_STAGE_TX   = 2,
	PING_STAGE_RX   = 3,
	PING_STAGE_DONE = 4
} t_ping_sstage;

typedef void __fastcall (__closure *mainForm_threadProcess)();

class CThread : public TThread
{
private:
	mainForm_threadProcess m_process;
protected:
	void __fastcall Execute()
	{
		while (!this->Terminated)
		{
			Sleep(0);
			if (m_process != NULL)
			{
				if (!m_sync)
					m_process();
				else
					Synchronize(m_process);
			}
		}
		ReturnValue = 0;
	}
public:
	__fastcall CThread(mainForm_threadProcess process, TThreadPriority priority, bool start) : TThread(!start)
	{
		m_process       = process;
		m_sync          = true;
		FreeOnTerminate = false;
		Priority        = priority;
	}
	virtual __fastcall ~CThread()
	{
		m_process = NULL;
		//if (m_mutex)
		//{
		//	WaitForSingleObject(m_mutex, 100);		// wait for upto 100ms
		//	CloseHandle(m_mutex);
		//	m_mutex = NULL;
		//}
	}
	bool m_sync;
};

class TForm1 : public TForm
{
__published:	// IDE-managed Components
	TButton *WOLButton;
	TLabel *StatusLabel;
	TLabel *Label2;
	TEdit *MACEditA;
	TLabel *Label1;
	TEdit *MACEditB;
	TLabel *Label3;
	TEdit *IPEdit;
	TMemo *Memo1;
	TCheckBox *WakeOnStartCheckBox;
	TCheckBox *CloseOnWakeCheckBox;
	void __fastcall FormCreate(TObject *Sender);
	void __fastcall FormDestroy(TObject *Sender);
	void __fastcall FormClose(TObject *Sender, TCloseAction &Action);
	void __fastcall FormKeyDown(TObject *Sender, WORD &Key,
			 TShiftState Shift);
	void __fastcall WOLButtonClick(TObject *Sender);
	void __fastcall Memo1DblClick(TObject *Sender);

private:	// User declarations
	String work_dir;
	String ini_filename;

	SYSTEM_INFO sys_info;

	String dest_detected_wav_filename;
	std::vector <uint8_t> dest_detected_wav;

	CHighResolutionTick startup_timer;

	WSADATA wsaData;

	struct
	{
		String              mac1;
		String              mac2;
		double              secs;
		CHighResolutionTick hires_timer;
		CHighResolutionTick repeat_timer;
	} WOL[MAX_DEST];

	struct
	{
		SOCKET                sock;
		String                addr;
		String                ip;
		int                   stage;
		int                   id;
		int                   sequence;
		uint16_t              source_port;
		int                   message_size;
		int                   timeout_ms;
		int                   count;
		double                total_round_trip_time;
		int                   packets_tx;
		int                   packets_rx;
		std::vector <uint8_t> tx_buffer;
		std::vector <uint8_t> rx_buffer;
		ICMPheader            tx_header;
		SOCKADDR_IN           dest;
		CHighResolutionTick   hires_timer;
	} ping[MAX_DEST];

	CThread *thread;

	CRITICAL_SECTION criticalSection;

	bool __fastcall GetBuildInfo(String filename, TVersion *version);

	bool __fastcall createPath(const char *path);

	String __fastcall errorToStr(const int err);
	String __fastcall getLastErrorStr(const DWORD err);
	int __fastcall getLastErrorStr(const DWORD err, void *err_str, int max_size);

	void __fastcall loadSettings();
	void __fastcall saveSettings();

	void __fastcall initGUI();

	void __fastcall WMWindowPosChanging(TWMWindowPosChanging &msg);
	void __fastcall WMInitGUI(TMessage &msg);

	void __fastcall addMemoLine(String s);

	void __fastcall ThreadProcess();

	bool __fastcall isValidIP(String ip_str);

	uint16_t __fastcall calcChecksum(const void *pBuffer, int len);
	bool __fastcall validateChecksum(const void *pBuffer, int len);
	bool __fastcall resolveIP(String remoteHost, String &IPAddress);

	bool __fastcall StrToMAC(String str, uint8_t *mac);

	void __fastcall finish(int index);

	bool __fastcall wolSend(String mac_str1, String mac_str2, int index, int repeat_broadcasts = 1);

	bool __fastcall pingStart(String addr, int index);
	void __fastcall pingProcess();

	void __fastcall wakeyWakey(const int index);

protected:

	#pragma option push -vi-
	BEGIN_MESSAGE_MAP
		VCL_MESSAGE_HANDLER(WM_WINDOWPOSCHANGING, TWMWindowPosMsg, WMWindowPosChanging);
		VCL_MESSAGE_HANDLER(WM_INIT_GUI, TMessage, WMInitGUI);
	END_MESSAGE_MAP(TForm)
	#pragma option pop

public:		// User declarations
	__fastcall TForm1(TComponent* Owner);
};

extern PACKAGE TForm1 *Form1;

#endif
