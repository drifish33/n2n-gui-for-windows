
// n2n_guiDlg.cpp : ʵ���ļ�
//

#include "stdafx.h"
#include "n2n_gui.h"
#include "n2n_guiDlg.h"
#include "afxdialogex.h"
#include "AddServerDlg.h"
#include "AddRouteDlg.h"
#include <NetCon.h>
#include <string>
#pragma comment( lib, "Iphlpapi.lib" )
#pragma comment( lib, "Mprapi.lib" )

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

char const Version[] = "V1.2.1";

int GetNtVersionNumbers()
{
	typedef void(__stdcall*NTPROC)(DWORD*, DWORD*, DWORD*);
	HINSTANCE hinst = LoadLibrary(TEXT("ntdll.dll"));//����DLL
	NTPROC GetNtVersionNumbers = (NTPROC)GetProcAddress(hinst, "RtlGetNtVersionNumbers");//��ȡ������ַ
	DWORD dwMajor, dwMinor, dwBuildNumber;
	GetNtVersionNumbers(&dwMajor, &dwMinor, &dwBuildNumber);
	FreeLibrary(hinst);
	return dwMajor;		//5:XP, 6:WIN7, 10:WIN10
}

void SafeGetNativeSystemInfo(LPSYSTEM_INFO lpSystemInfo)
{
	if (NULL == lpSystemInfo)
		return;
	typedef VOID (WINAPI *LPFN_GetNativeSystemInfo)(LPSYSTEM_INFO lpSystemInfo);
	LPFN_GetNativeSystemInfo nsInfo =
		(LPFN_GetNativeSystemInfo)GetProcAddress(GetModuleHandle(_T("kernel32")), "GetNativeSystemInfo");
	if (NULL != nsInfo)
	{
		nsInfo(lpSystemInfo);
	}
	else
	{
		GetSystemInfo(lpSystemInfo);
	}
}

int GetSystemBits()
{
	SYSTEM_INFO si;
	SafeGetNativeSystemInfo(&si);
	if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
		si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64 )
	{
		return 64;
	}
	return 86;
}

bool GetProfileServersInfo(char const *ProFile, char const *AppName, SERVER_Struct *pServer)
{
	char str[1024];
	int len=GetPrivateProfileString(AppName,"Server","",str,sizeof(str),ProFile);
	if (len==0 || len>=sizeof(pServer->Server)) return false;
	strcpy_s(pServer->Server,sizeof(pServer->Server),str);

	len=GetPrivateProfileString(AppName,"NetName","",str,sizeof(str),ProFile);
	if (len==0 || len>=sizeof(pServer->NetName)) return false;
	strcpy_s(pServer->NetName,sizeof(pServer->NetName),str);

	len=GetPrivateProfileString(AppName,"NetPasswd","",str,sizeof(str),ProFile);
	if (len>=sizeof(pServer->NetPasswd)) return false;
	strcpy_s(pServer->NetPasswd,sizeof(pServer->NetPasswd),str);

	len=GetPrivateProfileString(AppName,"LocalIP","",str,sizeof(str),ProFile);
	if (len>=16 || !StripToIp(str,pServer->IpAddr)) return false;

	len=GetPrivateProfileString(AppName,"Mask","",str,sizeof(str),ProFile);
	if (len>=16 || !StripToIp(str,pServer->IpMask)) return false;

	//·�ɱ�
	//�ȶ�ȡ����̬������
	CArray<SERVER_ROUTE_Struct,SERVER_ROUTE_Struct&>	Array;
	len=GetPrivateProfileString(AppName,"Route","",str,sizeof(str),ProFile);
	for (int n=0; n<len; )
	{
		char *pStart=str+n, *pEnd=strchr(pStart,';'),*pt;
		if (pEnd)
		{
			*pEnd++=0;
			n+=pEnd-pStart;
		}
		//������ʽ: 1 192.168.1.0/24 10.0.0.1 ��˾
		SERVER_ROUTE_Struct route={0};
		int enable,mask,ip[4],gate[4];
		int m=sscanf_s(pStart,"%d %d.%d.%d.%d/%d %d.%d.%d.%d ",&enable,ip,ip+1,ip+2,ip+3,&mask,gate,gate+1,gate+2,gate+3);
		if (m!=10 || enable>1 || enable<0 || mask<16 || mask>32) break;
		route.Enable=enable!=0;
		route.Mask=(UCHAR)mask;
		for (int i=0; i<4; i++)
		{
			route.Net[i]=ip[i];
			route.Gate[i]=gate[i];
		}
		pt=strrchr(pStart,' ');
		if (pt)
			strncpy_s(route.Note,sizeof(route.Note),pt+1,sizeof(route.Note)-1);

		Array.Add(route);
		if (pEnd==NULL) break;
	}
	pServer->RouteCnts=Array.GetCount();
	pServer->pRouteList=NULL;
	if (pServer->RouteCnts)
	{
		pServer->pRouteList = new SERVER_ROUTE_Struct[pServer->RouteCnts];
		if (pServer->pRouteList)
		{
			for (int i=0; i<pServer->RouteCnts; i++)
				pServer->pRouteList[i]=Array[i];
		}
		else
			pServer->RouteCnts=0;
	}

	return true;
}

HRESULT disShareNet(INetSharingManager* pNSM)
{
	INetConnection * pNC = NULL;
	INetSharingConfiguration * pNSC = NULL;
	IEnumVARIANT * pEV = NULL;
	IUnknown * pUnk = NULL;
	INetSharingEveryConnectionCollection * pNSECC = NULL;

	HRESULT hr = pNSM->get_EnumEveryConnection(&pNSECC);
	VARIANT v;
	VariantInit(&v);

	if (!pNSECC)
	{
		return NULL;
	}

	hr = pNSECC->get__NewEnum(&pUnk);
	if (pUnk)
	{
		hr = pUnk->QueryInterface(__uuidof(IEnumVARIANT), (void**)&pEV);
		pUnk->Release();
	}

	while (S_OK == pEV->Next(1, &v, NULL))
	{
		if (V_VT(&v) != VT_UNKNOWN) continue;
		V_UNKNOWN(&v)->QueryInterface(__uuidof(INetConnection), (void**)&pNC);
		if (pNC==NULL) continue;

		VARIANT_BOOL flag=0;
		hr = pNSM->get_INetSharingConfigurationForINetConnection(pNC, &pNSC);
		hr = pNSC->get_SharingEnabled(&flag);
		if (flag)
		{
			hr = pNSC->DisableSharing();
			Sleep(500);
		}
		pNSC->Release();
	}
	return hr;
}

HRESULT shareNet(INetSharingManager* pNSM, const char* srcName, const char* dstName)
{
	INetConnection * pNC = NULL;
	INetSharingConfiguration * pNSC = NULL;
	IEnumVARIANT * pEV = NULL;
	IUnknown * pUnk = NULL;
	INetSharingEveryConnectionCollection * pNSECC = NULL;

	HRESULT hr = pNSM->get_EnumEveryConnection(&pNSECC);
	VARIANT v;
	VariantInit(&v);

	if (!pNSECC)
	{
		return NULL;
	}

	hr = pNSECC->get__NewEnum(&pUnk);
	if (pUnk) 
	{
		hr = pUnk->QueryInterface(__uuidof(IEnumVARIANT), (void**)&pEV);
		pUnk->Release();
	}

	while (S_OK == pEV->Next(1, &v, NULL))
	{
		if (V_VT(&v) != VT_UNKNOWN) continue;
		V_UNKNOWN(&v)->QueryInterface(__uuidof(INetConnection), (void**)&pNC);
		if (pNC==NULL) continue;

		NETCON_PROPERTIES* pNP = NULL;
		pNC->GetProperties(&pNP);

		std::string tmpName = CW2A(pNP->pszwName);
		//printf("###### |%s| : |%s|\r\n", tmpName.c_str(),(char*)nicName);
		if (!strcmp(tmpName.c_str(), (char*)srcName))
		{
			//printf("**************find nic srcName : %s\r\n", (char*)srcName);
			hr = pNSM->get_INetSharingConfigurationForINetConnection(pNC, &pNSC);
			hr = pNSC->EnableSharing(ICSSHARINGTYPE_PUBLIC);
			pNSC->Release();
		}
		else if (!strcmp(tmpName.c_str(), (char*)dstName))
		{
			//	printf("**************find nic dstName : %s\r\n", (char*)dstName);
			hr = pNSM->get_INetSharingConfigurationForINetConnection(pNC, &pNSC);
			hr = pNSC->EnableSharing(ICSSHARINGTYPE_PRIVATE);
			pNSC->Release();
		}
	}
	return hr;
}

bool CheckshareNet(INetSharingManager* pNSM, const char* srcName, const char* dstName)
{
	INetConnection * pNC = NULL;
	INetSharingConfiguration * pNSC = NULL;
	IEnumVARIANT * pEV = NULL;
	IUnknown * pUnk = NULL;
	INetSharingEveryConnectionCollection * pNSECC = NULL;

	HRESULT hr = pNSM->get_EnumEveryConnection(&pNSECC);
	VARIANT v;
	VariantInit(&v);

	if (!pNSECC)
	{
		return false;
	}

	hr = pNSECC->get__NewEnum(&pUnk);
	if (pUnk)
	{
		hr = pUnk->QueryInterface(__uuidof(IEnumVARIANT), (void**)&pEV);
		pUnk->Release();
	}

	bool bsrcflag=false,bdstflag=false;
	while (S_OK == pEV->Next(1, &v, NULL))
	{
		if (V_VT(&v) != VT_UNKNOWN) continue;
		V_UNKNOWN(&v)->QueryInterface(__uuidof(INetConnection), (void**)&pNC);
		if (pNC==NULL) continue;

		NETCON_PROPERTIES* pNP = NULL;
		pNC->GetProperties(&pNP);

		std::string tmpName = CW2A(pNP->pszwName);
		//printf("###### |%s| : |%s|\r\n", tmpName.c_str(),(char*)nicName);
		VARIANT_BOOL flag=0;
		SHARINGCONNECTIONTYPE Type;
		if (!strcmp(tmpName.c_str(), (char*)srcName))
		{
			//printf("**************find nic srcName : %s\r\n", (char*)srcName);
			hr = pNSM->get_INetSharingConfigurationForINetConnection(pNC, &pNSC);
			hr = pNSC->get_SharingEnabled(&flag);
			hr = pNSC->get_SharingConnectionType(&Type);
			pNSC->Release();
			if (flag && Type==ICSSHARINGTYPE_PUBLIC) 
				bsrcflag=true;
		}
		else if (!strcmp(tmpName.c_str(), (char*)dstName))
		{
			//	printf("**************find nic dstName : %s\r\n", (char*)dstName);
			hr = pNSM->get_INetSharingConfigurationForINetConnection(pNC, &pNSC);
			hr = pNSC->get_SharingEnabled(&flag);
			hr = pNSC->get_SharingConnectionType(&Type);
			pNSC->Release();
			if (flag && Type==ICSSHARINGTYPE_PRIVATE) 
				bdstflag=true;
		}
		if (bsrcflag && bdstflag) return true;
	}
	return false;
}

NetAdapters_Struct *GetAdapters(int *Cnt)
{
	NetAdapters_Struct *NetAdapters = NULL;
	PIP_ADAPTER_INFO pIpAdapterInfo = NULL;
	unsigned long stSize = 0;

	HANDLE   hMprConfig;  
	DWORD dwRet=MprConfigServerConnect   (NULL,&hMprConfig);
	int nRel = GetAdaptersInfo(pIpAdapterInfo,&stSize);
	bool flag=false;
	if (ERROR_BUFFER_OVERFLOW == nRel)
	{
		//����������ص���ERROR_BUFFER_OVERFLOW
		//��˵��GetAdaptersInfo�������ݵ��ڴ�ռ䲻��,ͬʱ�䴫��stSize,��ʾ��Ҫ�Ŀռ��С
		//��Ҳ��˵��ΪʲôstSize����һ��������Ҳ��һ�������
		//���������ڴ�ռ������洢����������Ϣ
		pIpAdapterInfo = (PIP_ADAPTER_INFO)new BYTE[stSize];
		if (pIpAdapterInfo==NULL) return NULL;
		NetAdapters=new NetAdapters_Struct[stSize/sizeof(PIP_ADAPTER_INFO)];
		if (NetAdapters==NULL)
		{
			delete pIpAdapterInfo;
			return NULL;
		}
		//�ٴε���GetAdaptersInfo����,���pIpAdapterInfoָ�����
		nRel=GetAdaptersInfo(pIpAdapterInfo,&stSize);    
	}
	if (ERROR_SUCCESS == nRel)
	{
		int n=0;
		for (PIP_ADAPTER_INFO p=pIpAdapterInfo;p!=NULL;p=p->Next,n++)
		{
			wchar_t dBuf[100];
			WCHAR   szFriendName[256];  
			DWORD dBufSize=MultiByteToWideChar(CP_ACP, 0, p->AdapterName, strlen(p->AdapterName), dBuf, 100);
			dBuf[dBufSize]=0;
			dwRet=MprConfigGetFriendlyName(hMprConfig,dBuf,(PWCHAR)szFriendName,sizeof(szFriendName));  
			WideCharToMultiByte (CP_ACP,NULL,szFriendName,-1,NetAdapters[n].Name,sizeof(NetAdapters[n].Name),NULL,FALSE);
			strncpy_s(NetAdapters[n].Description,sizeof(NetAdapters[n].Description),p->Description,sizeof(NetAdapters[n].Description));
			NetAdapters[n].Index=p->Index;
		}
		delete pIpAdapterInfo;
		*Cnt=n;
		return NetAdapters;
	}
	delete NetAdapters;
	delete pIpAdapterInfo;
	return NULL;
}

HRESULT shareNet(const char* srcName)
{
	CoInitialize(NULL);
	CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);

	INetSharingManager * pNSM = NULL;
	HRESULT hr = ::CoCreateInstance(__uuidof(NetSharingManager),
		NULL,
		CLSCTX_ALL,
		__uuidof(INetSharingManager),
		(void**)&pNSM);

	if (pNSM)
	{
		if (srcName[0]==0)
			hr = disShareNet(pNSM);
		else
		{
			int Cnt=0;
			NetAdapters_Struct *pAdapters = GetAdapters(&Cnt);
			for (int n=0; n<Cnt; n++)
			{
				if (strncmp(pAdapters[n].Description,"TAP-Windows Adapter V9",22)==0)
				{
					if (CheckshareNet(pNSM, srcName, pAdapters[n].Name)) break;
					hr = disShareNet(pNSM);
					return shareNet(pNSM, srcName, pAdapters[n].Name);
				}
			}
		}
	}
	return hr;
}

bool CheckTapAdapters()
{
	int Cnt=0;
	NetAdapters_Struct *p=GetAdapters(&Cnt);
	if (p==NULL) return false;

	for (int i=0; i<Cnt; i++)
	{
		if (strncmp(p[i].Description,"TAP-Windows Adapter V9",22)==0)
		{
			delete p;
			return true;
		}
	}
	delete p;
	return false;
}

// ����Ӧ�ó��򡰹��ڡ��˵���� CAboutDlg �Ի���

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// �Ի�������
	enum { IDD = IDD_ABOUTBOX };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV ֧��

// ʵ��
protected:
	DECLARE_MESSAGE_MAP()
public:
	CString m_AbortString;
};

CAboutDlg::CAboutDlg() : CDialogEx(CAboutDlg::IDD)
{
	m_AbortString.Format("n2n gui for windows��%s ��\n�����������ʹ����ѣ�������֧�ּ����Ի������շѡ����ߣ�dwj00@163.com",Version);
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_STATIC1, m_AbortString);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// Cn2n_guiDlg �Ի���




Cn2n_guiDlg::Cn2n_guiDlg(CWnd* pParent /*=NULL*/)
	: CDialogEx(Cn2n_guiDlg::IDD, pParent)
	, SystemBits(0)
	, ConnectTick(0)
	, bAutoHide(false)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	hClientProcess= hClientRead= hServerProcess=0;
	SystemBits=GetSystemBits();
}

void Cn2n_guiDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_List);
	DDX_Control(pDX, IDC_STATIC_CONNECT_STATUS, m_ConnectStatus);
	DDX_Control(pDX, IDC_EDIT_LOG, m_Log);
}

BEGIN_MESSAGE_MAP(Cn2n_guiDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDOK, &Cn2n_guiDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDC_BTN_START_STOP, &Cn2n_guiDlg::OnBnClickedBtnStartStop)
	ON_CBN_SELCHANGE(IDC_COMBO_SERVERLIST, &Cn2n_guiDlg::OnCbnSelchangeComboServerlist)
	ON_BN_CLICKED(IDC_BTN_DEL_SERVER, &Cn2n_guiDlg::OnBnClickedBtnDelServer)
	ON_NOTIFY(NM_RCLICK, IDC_LIST1, &Cn2n_guiDlg::OnNMRClickList1)
	ON_BN_CLICKED(IDC_BTN_ADD_SERVER, &Cn2n_guiDlg::OnBnClickedBtnAddServer)
	ON_BN_CLICKED(IDC_BTN_SAVE, &Cn2n_guiDlg::OnBnClickedBtnSave)
	ON_BN_CLICKED(ID_MENU_SHOW, &Cn2n_guiDlg::OnMenuClickedShow)
	ON_BN_CLICKED(ID_MENU_ADD_ROUTE,&Cn2n_guiDlg::OnMenuClickedAddRoute)
	ON_BN_CLICKED(ID_MENU_DEL_ROUTE,&Cn2n_guiDlg::OnMenuClickedDelRoute)
	ON_BN_CLICKED(ID_MENU_EDIT_ROUTE,&Cn2n_guiDlg::OnMenuClickedEditRoute)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_BTN_LOG, &Cn2n_guiDlg::OnBnClickedBtnLog)
	ON_MESSAGE(ON_NOTIFY_ICON_MSG,&Cn2n_guiDlg::OnNotifyIconMsg)
	ON_MESSAGE(ON_SHOWLOG_MSG,&Cn2n_guiDlg::OnShowLogMsg)
	ON_BN_CLICKED(IDC_BTN_HIDE, &Cn2n_guiDlg::OnBnClickedBtnHide)
	ON_BN_CLICKED(IDC_BTN_EDIT_SERVER, &Cn2n_guiDlg::OnBnClickedBtnEditServer)
	ON_BN_CLICKED(IDC_BTN_SET, &Cn2n_guiDlg::OnBnClickedBtnSet)
	ON_BN_CLICKED(IDC_BTN_CLR_LOG, &Cn2n_guiDlg::OnBnClickedBtnClrLog)
END_MESSAGE_MAP()

// Cn2n_guiDlg ��Ϣ�������

BOOL Cn2n_guiDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// ��������...���˵�����ӵ�ϵͳ�˵��С�

	// IDM_ABOUTBOX ������ϵͳ���Χ�ڡ�
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// ���ô˶Ի����ͼ�ꡣ��Ӧ�ó��������ڲ��ǶԻ���ʱ����ܽ��Զ�
	//  ִ�д˲���
	SetIcon(m_hIcon, TRUE);			// ���ô�ͼ��
	SetIcon(m_hIcon, FALSE);		// ����Сͼ��

	// TODO: �ڴ���Ӷ���ĳ�ʼ������
	//
	CFont Font;
	Font.CreateFont(16,12,0,0,FW_THIN,false,false,false,
		CHINESEBIG5_CHARSET,OUT_CHARACTER_PRECIS,
		CLIP_CHARACTER_PRECIS,DEFAULT_QUALITY,
		FF_MODERN,"����");
	m_ConnectStatus.SetTextFont(Font);

	//�б��
	m_List.SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_CHECKBOXES);
	m_List.InsertColumn(0,"����",0,132);
	m_List.InsertColumn(1,"����",0,96);
	m_List.InsertColumn(2,"��ע",0,64);

	char str[2048],ProfilePath[MAX_PATH];
	sprintf_s(ProfilePath,sizeof(ProfilePath),"%sn2n.ini",ProPath);
	//
	bAutoHide=GetPrivateProfileInt("Config","AutoHide",0,ProfilePath)!=0;
	GetPrivateProfileString("Config","ReSendIf","",str,sizeof(str),ProfilePath);
	ReSendIf=str;
	GetPrivateProfileString("Config","Param","",str,sizeof(str),ProfilePath);
	m_OtherParam=str;
	//��ȡ�������б�
	CComboBox *pBox = (CComboBox*)GetDlgItem(IDC_COMBO_SERVERLIST);
	int len=GetPrivateProfileSectionNames(str,sizeof(str),ProfilePath);
	for (int i=0; i<len; )
	{
		char *p=str+i;
		i+=strlen(p)+1;
		if (memcmp(p,"SERVER_No",9)==0 && p[9]>='0' && p[9]<='9')
		{
			SERVER_Struct Host;
			if (GetProfileServersInfo(ProfilePath,p,&Host)) 
			{
				pBox->AddString(Host.Server);
				ServerArray.Add(Host);
			}
		}
	}
	//��ȡѡ��ķ�����
	if (pBox->GetCount()>0)
	{
		int Sel=GetPrivateProfileInt("Config","LastSel",0,ProfilePath);
		if (Sel>=pBox->GetCount()) Sel=0;
		pBox->SetCurSel(Sel);
		OnCbnSelchangeComboServerlist();
	}
	//�����
	int Enable=GetPrivateProfileInt("SERVER","Enable",0,ProfilePath);
	int Port=GetPrivateProfileInt("SERVER","Port",1235,ProfilePath);
	SetDlgItemInt(IDC_EDIT_SERVER_PORT,Port);
	((CButton*)GetDlgItem(IDC_CHECK_SERVER))->SetCheck(Enable);

	//�������
	m_Icon_Connected=AfxGetApp()->LoadIcon(IDI_ICON1);
	m_Icon_NoConnect=AfxGetApp()->LoadIcon(IDI_ICON2);
	m_Icon_ConnectErr=AfxGetApp()->LoadIcon(IDI_ICON3);
	if (!CheckTapAdapters()) 
	{
		SetDlgItemText(IDC_BTN_START_STOP,"��װ����");
		SetDlgItemText(IDC_STATIC_CONNECT_STATUS,"δ��װ");
		m_ConnectStatus.SetColor(RGB(255,0,0));
		((CStatic*)GetDlgItem(IDC_PIC_CONNECT))->SetIcon(m_Icon_ConnectErr);
	}
	else
	{
		m_ConnectStatus.SetColor(RGB(155,100,75));
		((CStatic*)GetDlgItem(IDC_PIC_CONNECT))->SetIcon(m_Icon_NoConnect);
	}

	//�������
	m_Nid.cbSize=(DWORD)sizeof(NOTIFYICONDATA);
	m_Nid.hWnd=m_hWnd;	
	m_Nid.uID=IDR_MAINFRAME;	
	m_Nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP ;	
	m_Nid.uCallbackMessage=ON_NOTIFY_ICON_MSG;		//�Զ������Ϣ����	
	m_Nid.hIcon=LoadIcon(AfxGetInstanceHandle(),MAKEINTRESOURCE(IDR_MAINFRAME));
	strcpy_s(m_Nid.szTip,sizeof(m_Nid.szTip),"n2n Gui δ����");//��Ϣ��ʾ��	
	Shell_NotifyIcon(NIM_ADD,&m_Nid);				//�����������ͼ��

	//������־����
	m_Log.SetLimitText(500*1024);
	PostMessage(WM_COMMAND,IDC_BTN_LOG);

	//��������
	bAutoConnect=GetPrivateProfileInt("Config","AutoConnect",0,ProfilePath)!=0;
	if (bAutoConnect)
		PostMessage(WM_COMMAND,IDC_BTN_START_STOP);

	return TRUE;  // ���ǽ��������õ��ؼ������򷵻� TRUE
}

void Cn2n_guiDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// �����Ի��������С����ť������Ҫ����Ĵ���
//  �����Ƹ�ͼ�ꡣ����ʹ���ĵ�/��ͼģ�͵� MFC Ӧ�ó���
//  �⽫�ɿ���Զ���ɡ�

void Cn2n_guiDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // ���ڻ��Ƶ��豸������

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// ʹͼ���ڹ����������о���
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// ����ͼ��
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

//���û��϶���С������ʱϵͳ���ô˺���ȡ�ù��
//��ʾ��
HCURSOR Cn2n_guiDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void Cn2n_guiDlg::OnBnClickedOk()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������

}

void Cn2n_guiDlg::SetRoute(bool bEnable)
{
	char Cmd[64],ip[24],gate[20],mask[20];
	UCHAR IP[4],Mask,MaskAddr[4];
	
	for (int i=0; i<m_List.GetItemCount();i++)
	{
		if (!m_List.GetCheck(i)) continue;
		m_List.GetItemText(i,0,ip,sizeof(ip));
		m_List.GetItemText(i,1,gate,sizeof(gate));
		if (StrNetaddrToIp(ip,IP,&Mask))
		{
			unsigned int val=0;
			for (int j=0; j<Mask; j++)
				val|=(1<<(31-j));
			MaskAddr[0]=(UCHAR)(val>>24),MaskAddr[1]=(UCHAR)((val>>16)&0xff);
			MaskAddr[2]=(UCHAR)((val>>8)&0xff),MaskAddr[3]=(UCHAR)(val&0xff);
			
			char *ptmp=strchr(ip,'/');
			if (ptmp) *ptmp=0;
			if (bEnable)
				sprintf_s(Cmd,sizeof(Cmd),"route add %s mask %s %s",ip,IpToStrip(MaskAddr,mask),gate);
			else
				sprintf_s(Cmd,sizeof(Cmd),"route delete %s",ip);
			TRACE("%s\r\n",Cmd);
			WinExec(Cmd,SW_HIDE);
		}
	}
	if (bEnable && m_List.GetItemCount()>0) 
		SendMessage(ON_SHOWLOG_MSG,(WPARAM)"----------------------���·�����.----------------------\r\n",0);
}

DWORD CALLBACK	ReadLogThread(LPVOID lp)
{
	Cn2n_guiDlg *pDlg = (Cn2n_guiDlg*)lp;
	bool bConnected=false;
	
	while (1)
	{
		char str[4096];
		DWORD bytesRead;

		if (ReadFile(pDlg->hClientRead,str,4095,&bytesRead,NULL)==NULL) break;
		str[bytesRead]=0;
		pDlg->SendMessage(ON_SHOWLOG_MSG,(WPARAM)str,0);
		//���ң�[OK] Edge Peer <<< ================ >>> Super Node
		if (!bConnected)
		{
			if (strstr(str,"[OK] Edge Peer <<< ================ >>> Super Node")!=NULL)
			{
				bConnected=true;
				pDlg->SetDlgItemText(IDC_STATIC_CONNECT_STATUS,"������");	
				pDlg->m_ConnectStatus.SetColor(RGB(0,200,0));
				((CStatic*)pDlg->GetDlgItem(IDC_PIC_CONNECT))->SetIcon(pDlg->m_Icon_Connected);
				strcpy_s(pDlg->m_Nid.szTip,sizeof(pDlg->m_Nid.szTip),"n2n Gui ������");	
				Shell_NotifyIcon(NIM_MODIFY,&pDlg->m_Nid);				//�޸�������ͼ��
				if (pDlg->bAutoHide) pDlg->PostMessage(WM_COMMAND,IDC_BTN_HIDE);
			}
		}
		else
		{
			if (strstr(str,"WARNING: TAP I/O operation aborted, restart later.")!=NULL)
			{
				//�������·��
//				pDlg->SendMessage(ON_SHOWLOG_MSG,(WPARAM)"�������·��...\r\n",0);
				pDlg->ConnectTick=0;
				pDlg->SetTimer(1,600,NULL);
			}
		}
	}
	TRACE("�߳��˳�\r\n");

	return 0;
}

bool Cn2n_guiDlg::StartN2nServer(int Port)
{
	char Cmd[MAX_PATH];
	STARTUPINFO si;
	PROCESS_INFORMATION pi;  
	ZeroMemory(&si, sizeof(si));
	ZeroMemory(&pi, sizeof(pi));
	si.wShowWindow = SW_HIDE;
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;

	int len=sprintf_s(Cmd,MAX_PATH,"%sn2n_client\\x%d\\supernode_v2_n2n.exe -l %d",ProPath,SystemBits,Port);
	TRACE("%s\r\n",Cmd);

	if (!CreateProcess(NULL, Cmd,NULL,NULL,TRUE,NULL,NULL,NULL,&si,&pi))
	{
		return FALSE;
	}
	hServerProcess = pi.hProcess;
	return true;
}

bool Cn2n_guiDlg::StartN2nClient(char * Cmdline)
{
	SECURITY_ATTRIBUTES sa;
	HANDLE hRead,hWrite;
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	if (!CreatePipe(&hRead,&hWrite,&sa,0))
		return FALSE;

	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	si.cb = sizeof(STARTUPINFO);
	GetStartupInfo(&si);
	si.hStdError = hWrite;		//�Ѵ������̵ı�׼��������ض��򵽹ܵ�����
	si.hStdOutput = hWrite;		//�Ѵ������̵ı�׼����ض��򵽹ܵ�����
	si.wShowWindow = SW_HIDE;
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	//�ؼ����裬CreateProcess�����������������MSDN
	if (!CreateProcess(NULL, Cmdline,NULL,NULL,TRUE,NULL,NULL,NULL,&si,&pi))
	{
		CloseHandle(hWrite);
		CloseHandle(hRead);
		return FALSE;
	}

	CloseHandle(hWrite);
	CreateThread(NULL,0,ReadLogThread,this,0,NULL);

	hClientProcess=pi.hProcess;
	hClientRead=hRead;

	return true;
}

void Cn2n_guiDlg::OnBnClickedBtnStartStop()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	char Name[sizeof(((SERVER_Struct*)0)->NetName)],Passwd[sizeof(((SERVER_Struct*)0)->NetPasswd)];
	char Server[sizeof(((SERVER_Struct*)0)->Server)];
	char ClinePath[MAX_PATH+100],str1[MAX_PATH+200],str2[20];
	UCHAR ip[4],mask[4];

	GetDlgItemText(IDC_BTN_START_STOP,Name,10);
	if (strcmp(Name,"��װ����")==0)
	{
		int sysver=GetNtVersionNumbers();
		char const *exefile=sysver==5 ? "tap-windows-9.9.2 for xp.exe":"tap-windows-9.21.2.exe";
//		if (!SHGetSpecialFolderPath(m_hWnd,str1,CSIDL_PROGRAM_FILES,false))
//			strcpy_s(str1,sizeof(str1),"C:\\Program Files");
		sprintf_s(ClinePath,MAX_PATH,"%sn2n_client\\%s",ProPath,exefile);
		TRACE("%s\r\n",ClinePath);
		ShellExecute(NULL,"open",ClinePath,"/S",NULL,SW_SHOWNORMAL);
		KillTimer(2);
		SetTimer(2,1000,NULL);
	}
	else if (strcmp(Name,"����")==0)
	{
		//----------------------------�����ͻ���-----------------------------
		CIPAddressCtrl *pIP = (CIPAddressCtrl*)GetDlgItem(IDC_IPADDRESS_IP);
		pIP->GetAddress(ip[0],ip[1],ip[2],ip[3]);
		pIP = (CIPAddressCtrl*)GetDlgItem(IDC_IPADDRESS_MASK);
		pIP->GetAddress(mask[0],mask[1],mask[2],mask[3]);
		if (GetDlgItemText(IDC_EDIT_NETNAME,Name,sizeof(Name))>0 && *((UINT*)ip)!=0 && *((UINT*)mask)!=0 &&
			GetDlgItemText(IDC_COMBO_SERVERLIST,Server,sizeof(Server))>0 && strchr(Server,':')!=NULL)
		{
			GetDlgItemText(IDC_EDIT_PASSWD,Passwd,sizeof(Passwd));
			//ִ��N2N
			int len=sprintf_s(ClinePath,MAX_PATH,"%sn2n_client\\x%d\\edge_v2_n2n.exe -a %s -s %s -c %s -l %s",
				ProPath,SystemBits,IpToStrip(ip,str1),IpToStrip(mask,str2),Name,Server);
			if (Passwd[0]!=0)
				len+=sprintf_s(ClinePath+len,sizeof(ClinePath)-len," -k %s",Passwd);
			if (ReSendIf!="")
			{
				len+=sprintf_s(ClinePath+len,sizeof(ClinePath)-len," %s","-r");
				HRESULT hr=shareNet(ReSendIf);
				SendMessage(ON_SHOWLOG_MSG,hr==S_OK ? (WPARAM)"���繲���ѿ���.\r\n":(WPARAM)"�������繲��ʧ��.\r\n");
			}
			if (!m_OtherParam.IsEmpty())
				len+=sprintf_s(ClinePath+len,sizeof(ClinePath)-len," %s",m_OtherParam);
			TRACE("%s\r\n",ClinePath);
			if (StartN2nClient(ClinePath))
			{
				ConnectTick=0;
				SetTimer(1,250,NULL);	//���·��,�ҵĵ����ϲ�����Ҫ��ʱ����3600ms��·�ɲ�����Ч,��ʱʱ�䵽�����·��
				SetDlgItemText(IDC_STATIC_CONNECT_STATUS,"��������");
				SendMessage(ON_SHOWLOG_MSG,(WPARAM)"----------------------N2N�ͻ�������...----------------------\r\n");
				sprintf_s(str1,sizeof(str1),"������:%s\r\n",ClinePath);
				SendMessage(ON_SHOWLOG_MSG,(WPARAM)str1);
			}
			else
				SendMessage(ON_SHOWLOG_MSG,(WPARAM)"----------------------N2N�ͻ���ʧ��.----------------------\r\n");
		}
		//----------------------------���������-----------------------------
		int bEnable = ((CButton*)GetDlgItem(IDC_CHECK_SERVER))->GetCheck();
		int Port = GetDlgItemInt(IDC_EDIT_SERVER_PORT);
		if (bEnable && Port>0 && Port<65535)
		{
			if (StartN2nServer(Port))
				SendMessage(ON_SHOWLOG_MSG,(WPARAM)"----------------------N2N���������...----------------------\r\n");
		}
		//���ÿؼ�
		for (int id=0; id<=10; id++)
			GetDlgItem(IDC_COMBO_SERVERLIST+id)->EnableWindow(FALSE);
		SetDlgItemText(IDC_BTN_START_STOP,"ֹͣ");
	}
	else
	{
		//ɾ��·��
		SetRoute(false);
		//�رս���
		if (hServerProcess!=0) 
		{
			TerminateProcess(hServerProcess,0);
			PostMessage(ON_SHOWLOG_MSG,(WPARAM)"----------------------N2N����˹ر�----------------------\r\n");
		}
		if (hClientProcess!=0) 
		{
			TerminateProcess(hClientProcess,0);
			Sleep(100);			//�ȴ��߳��˳�
			if (hClientRead!=0) CloseHandle(hClientRead);
			Sleep(100);
			hServerProcess=0;
			hClientProcess=0;
			hClientRead=0;
			SetDlgItemText(IDC_STATIC_CONNECT_STATUS,"δ����");
			m_ConnectStatus.SetColor(RGB(155,100,75));
			((CStatic*)GetDlgItem(IDC_PIC_CONNECT))->SetIcon(m_Icon_NoConnect);
			PostMessage(ON_SHOWLOG_MSG,(WPARAM)"----------------------N2N�ͻ��˹ر�----------------------\r\n\r\n");
			strcpy_s(m_Nid.szTip,sizeof(m_Nid.szTip),"n2n Gui δ����");
			Shell_NotifyIcon(NIM_MODIFY,&m_Nid);				//�޸�������ͼ��
			//
			KillTimer(1);
		}
		//���ÿؼ�
		for (int id=0; id<=10; id++)
			GetDlgItem(IDC_COMBO_SERVERLIST+id)->EnableWindow(TRUE);
		SetDlgItemText(IDC_BTN_START_STOP,"����");
	}
}

void Cn2n_guiDlg::ShowSelServer(SERVER_Struct const * pServer)
{
	char str[32];
	m_List.DeleteAllItems();
	for (int i=0; i<pServer->RouteCnts; i++)
	{
		SERVER_ROUTE_Struct *route=pServer->pRouteList+i;
		sprintf_s(str,sizeof(str),"%d.%d.%d.%d/%d",route->Net[0],route->Net[1],route->Net[2],route->Net[3],route->Mask);
		m_List.InsertItem(i,str);
		if (route->Enable) m_List.SetCheck(i);
		m_List.SetItemText(i,1,IpToStrip(route->Gate,str));
		m_List.SetItemText(i,2,route->Note);
	}
	SetDlgItemText(IDC_EDIT_NETNAME,pServer->NetName);
	SetDlgItemText(IDC_EDIT_PASSWD,pServer->NetPasswd);
	CIPAddressCtrl *pIP = (CIPAddressCtrl*)GetDlgItem(IDC_IPADDRESS_IP);
	pIP->SetAddress(pServer->IpAddr[0],pServer->IpAddr[1],pServer->IpAddr[2],pServer->IpAddr[3]);
	pIP = (CIPAddressCtrl*)GetDlgItem(IDC_IPADDRESS_MASK);
	pIP->SetAddress(pServer->IpMask[0],pServer->IpMask[1],pServer->IpMask[2],pServer->IpMask[3]);
}


void Cn2n_guiDlg::OnCbnSelchangeComboServerlist()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	int Sel=((CComboBox*)GetDlgItem(IDC_COMBO_SERVERLIST))->GetCurSel();
	if (Sel!=-1)
	{
		SERVER_Struct Host=ServerArray[Sel];
		ShowSelServer(&Host);
	}
	else
	{
		SetDlgItemText(IDC_EDIT_NETNAME,"");
		SetDlgItemText(IDC_EDIT_PASSWD,"");
		CIPAddressCtrl *pIP = (CIPAddressCtrl*)GetDlgItem(IDC_IPADDRESS_IP);
		pIP->SetAddress(0,0,0,0);
		pIP = (CIPAddressCtrl*)GetDlgItem(IDC_IPADDRESS_MASK);
		pIP->SetAddress(0,0,0,0);
		m_List.DeleteAllItems();
	}
}

void Cn2n_guiDlg::OnBnClickedBtnDelServer()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	CComboBox *pBox=(CComboBox*)GetDlgItem(IDC_COMBO_SERVERLIST);
	int sel=pBox->GetCurSel();
	if (sel!=-1)
	{
		pBox->DeleteString(sel);
		delete ServerArray[sel].pRouteList;
		ServerArray.RemoveAt(sel);
		if (sel>=pBox->GetCount() && sel>0) sel--;
		pBox->SetCurSel(sel);
		OnCbnSelchangeComboServerlist();
	}
}

void Cn2n_guiDlg::OnBnClickedBtnAddServer()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	CAddServerDlg dlg;
	if (dlg.DoModal()==IDOK)
	{
		CComboBox *pBox=(CComboBox*)GetDlgItem(IDC_COMBO_SERVERLIST);
		pBox->AddString(dlg.m_Edit);
		SERVER_Struct Host;
		memset(&Host,0,sizeof(Host));
		Host.IpMask[1]=Host.IpMask[0]=Host.IpMask[2]=255;
		strcpy_s(Host.Server,sizeof(Host.Server),dlg.m_Edit);
		ServerArray.Add(Host);
		int n=pBox->GetCount();
		pBox->SetCurSel(n-1);
		OnCbnSelchangeComboServerlist();
	}
}

void Cn2n_guiDlg::OnBnClickedBtnEditServer()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	char ServerAddr[sizeof(((SERVER_Struct*)0)->Server)];
	GetDlgItemText(IDC_COMBO_SERVERLIST,ServerAddr,sizeof(ServerAddr));
	CAddServerDlg dlg(ServerAddr);
	if (dlg.DoModal()==IDOK)
	{
		CComboBox *pBox=(CComboBox*)GetDlgItem(IDC_COMBO_SERVERLIST);
		int sel=pBox->GetCurSel();
		pBox->DeleteString(sel);
		pBox->InsertString(sel,dlg.m_Edit);
		SERVER_Struct &Host=ServerArray[sel];
		strcpy_s(Host.Server,dlg.m_Edit);
		pBox->SetCurSel(sel);
		OnCbnSelchangeComboServerlist();
	}
}

void Cn2n_guiDlg::OnNMRClickList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	int SelCnt=m_List.GetSelectedCount();

	CMenu Menu;
	Menu.LoadMenu(IDR_MENU1);
	CMenu *pSubMenu=Menu.GetSubMenu(SelCnt!=0 ? 0:1);
	CPoint point;
	GetCursorPos(&point);
	pSubMenu->TrackPopupMenu(TPM_LEFTALIGN|TPM_LEFTBUTTON|TPM_VERTICAL,point.x,point.y,this);

	*pResult = 0;
}

void Cn2n_guiDlg::OnCancel()
{
	// TODO: �ڴ����ר�ô����/����û���
	char Name[6];
	GetDlgItemText(IDC_BTN_START_STOP,Name,5);
	if (strcmp(Name,"ֹͣ")==0)
		OnBnClickedBtnStartStop();
	::Shell_NotifyIcon(NIM_DELETE,&m_Nid);  

	for (int i=0; i<ServerArray.GetCount(); i++)
	{
		if (ServerArray[i].pRouteList) delete ServerArray[i].pRouteList;
	}
	CDialogEx::OnCancel();
}

void Cn2n_guiDlg::OnBnClickedBtnSave()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	char str[20],ProFileName[MAX_PATH];
	sprintf_s(ProFileName,sizeof(ProFileName),"%sn2n.ini",ProPath);
	//ɾ����������
	DeleteFile(ProFileName);
	//�����
	int Enable=((CButton*)GetDlgItem(IDC_CHECK_SERVER))->GetCheck();
	int Port=GetDlgItemInt(IDC_EDIT_SERVER_PORT);
	if (Port==0)
	{
		MessageBox("���������˶˿ں�.");
		return;
	}
	WritePrivateProfileString("SERVER","Enable",Itoa(Enable,str),ProFileName);
	WritePrivateProfileString("SERVER","Port",Itoa(Port,str),ProFileName);
	//�������ò���
	int n=((CComboBox*)GetDlgItem(IDC_COMBO_SERVERLIST))->GetCurSel();
	if (n==-1) return;
	WritePrivateProfileString("Config","LastSel",Itoa(n,str),ProFileName);
	WritePrivateProfileString("Config","Param",m_OtherParam.IsEmpty() ? NULL:m_OtherParam,ProFileName);
	WritePrivateProfileString("Config","AutoHide",Itoa(bAutoHide,str),ProFileName);
	WritePrivateProfileString("Config","ReSendIf",ReSendIf,ProFileName);
	//��ȡ��ǰ����
	char Name[sizeof(((SERVER_Struct*)0)->NetName)],Passwd[sizeof(((SERVER_Struct*)0)->NetPasswd)];
	SERVER_Struct &NowHost=ServerArray[n];
	UCHAR ip[4],mask[4];
	if (GetDlgItemText(IDC_EDIT_NETNAME,Name,sizeof(Name))==0)
	{
		MessageBox("����д�����������ơ�");
		return;
	}
	GetDlgItemText(IDC_EDIT_PASSWD,Passwd,sizeof(Passwd));
	CIPAddressCtrl *pIP = (CIPAddressCtrl*)GetDlgItem(IDC_IPADDRESS_IP);
	pIP->GetAddress(ip[0],ip[1],ip[2],ip[3]);
	pIP = (CIPAddressCtrl*)GetDlgItem(IDC_IPADDRESS_MASK);
	pIP->GetAddress(mask[0],mask[1],mask[2],mask[3]);
	strcpy_s(NowHost.NetName,sizeof(NowHost.NetName),Name);
	strcpy_s(NowHost.NetPasswd,sizeof(NowHost.NetPasswd),Passwd);
	memcpy(NowHost.IpAddr,ip,4);
	memcpy(NowHost.IpMask,mask,4);
	n=m_List.GetItemCount();
	if (n!=NowHost.RouteCnts)
	{
		if (NowHost.pRouteList) 
			delete NowHost.pRouteList;
		NowHost.RouteCnts=0;
		if (n>0)
		{
			NowHost.pRouteList=new SERVER_ROUTE_Struct[n];
			if (NowHost.pRouteList) NowHost.RouteCnts=n;
		}
	}
	for (n=0; n<NowHost.RouteCnts; n++)
	{
		SERVER_ROUTE_Struct *proute=NowHost.pRouteList+n;
		proute->Enable=m_List.GetCheck(n)==TRUE;
		m_List.GetItemText(n,0,str,20);
		StrNetaddrToIp(str,proute->Net,&proute->Mask);
		m_List.GetItemText(n,1,str,16);
		StripToIp(str,proute->Gate);
		m_List.GetItemText(n,2,proute->Note,sizeof(proute->Note));
	}
	//����
	for (n=0; n<ServerArray.GetCount(); n++)
	{
		char str1[100];
		SERVER_Struct &Host=ServerArray[n];
		sprintf_s(str,sizeof(str),"SERVER_No%d",n+1);
		WritePrivateProfileString(str,"Server",Host.Server,ProFileName);
		WritePrivateProfileString(str,"NetName",Host.NetName,ProFileName);
		WritePrivateProfileString(str,"NetPasswd",Host.NetPasswd[0]==0 ? NULL:Host.NetPasswd,ProFileName);
		WritePrivateProfileString(str,"LocalIP",IpToStrip(Host.IpAddr,str1),ProFileName);
		WritePrivateProfileString(str,"Mask",IpToStrip(Host.IpMask,str1),ProFileName);
		CString strroute;
		for (int i=0; i<Host.RouteCnts; i++)
		{
			SERVER_ROUTE_Struct *proute=Host.pRouteList+i;
			sprintf_s(str1,sizeof(str1),";%d %d.%d.%d.%d/%d %d.%d.%d.%d %s",proute->Enable,
				proute->Net[0],proute->Net[1],proute->Net[2],proute->Net[3],proute->Mask,
				proute->Gate[0],proute->Gate[1],proute->Gate[2],proute->Gate[3],proute->Note);
			strroute+= i==0 ? str1+1 : str1;
		}
		char const *pr=NULL;
		if (!strroute.IsEmpty())
			pr=strroute;
		WritePrivateProfileString(str,"Route",pr,ProFileName);
	}
}


void Cn2n_guiDlg::OnTimer(UINT_PTR nIDEvent)
{
	// TODO: �ڴ������Ϣ�����������/�����Ĭ��ֵ
	if (nIDEvent==1)			//��ʱ��1�����·�ɺ�����ʱ��˸��250msһ��
	{
		char str[10];
		ConnectTick++;

		GetDlgItemText(IDC_STATIC_CONNECT_STATUS,str,sizeof(str));
		if (strcmp(str,"������")!=0)
			((CStatic*)GetDlgItem(IDC_PIC_CONNECT))->SetIcon(ConnectTick%2==0 ? m_Icon_NoConnect:NULL);
		else if (ConnectTick>=20)	//���·��Ҫ��ʱ3600ms���ϣ���ȻԾ������������
		{
			SetRoute(true);
			KillTimer(1);
		}
	}
	else if (nIDEvent==2)		//��ʱ��2��������װ���
	{
		if (CheckTapAdapters())
		{
			KillTimer(2);
			SetDlgItemText(IDC_BTN_START_STOP,"����");
			SetDlgItemText(IDC_STATIC_CONNECT_STATUS,"δ����");
			m_ConnectStatus.SetColor(RGB(155,100,75));
		}
	}
	CDialogEx::OnTimer(nIDEvent);
}


void Cn2n_guiDlg::OnBnClickedBtnLog()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	int const LogWidth=448;
	static bool flag=true;
	flag=!flag;
	if (flag)
	{
		SetDlgItemText(IDC_BTN_LOG,"������־");
		CRect rect;
		GetWindowRect(rect);
		rect.right+=LogWidth;
		MoveWindow(rect);
	}
	else
	{
		SetDlgItemText(IDC_BTN_LOG,"������־");
		CRect rect;
		GetWindowRect(rect);
		rect.right-=LogWidth;
		MoveWindow(rect);
	}
}

void Cn2n_guiDlg::OnBnClickedBtnHide()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	ShowWindow(SW_HIDE);
}


LRESULT Cn2n_guiDlg::OnNotifyIconMsg(WPARAM w, LPARAM l)
{ 
	if(w!=IDR_MAINFRAME) 
		return  LRESULT(); 
	switch(l) 
	{ 
	case WM_RBUTTONUP://�Ҽ�����ʱ������ݲ˵�������ֻ��һ�����رա� 
	{ 
		CMenu Menu;
		Menu.LoadMenu(IDR_MENU1);
		CMenu *pSubMenu=Menu.GetSubMenu(2);
		CPoint point;
		GetCursorPos(&point);
		pSubMenu->TrackPopupMenu(TPM_LEFTALIGN|TPM_LEFTBUTTON|TPM_VERTICAL,point.x,point.y,this);
	} 
		break; 
	case WM_LBUTTONDBLCLK://˫������Ĵ��� 
	{
		ShowWindow(SW_SHOW);//�򵥵���ʾ���������¶� 
	}
	}
	return LRESULT();
}


void Cn2n_guiDlg::OnMenuClickedShow(void)
{
	ShowWindow(SW_SHOW);//�򵥵���ʾ���������¶� 
}


void Cn2n_guiDlg::OnMenuClickedDelRoute(void)
{
	for (int n=m_List.GetItemCount()-1; n>=0; n--)
	{
		if (m_List.GetItemState(n,LVIS_SELECTED)&LVIS_SELECTED)
		{
			m_List.DeleteItem(n);
		}
	}
}


void Cn2n_guiDlg::OnMenuClickedEditRoute(void)
{
	if (m_List.GetSelectedCount()!=1) return;
	int n=m_List.GetSelectionMark();
	char netaddr[20];
	char gate[16];
	char note[sizeof(((SERVER_ROUTE_Struct*)0)->Note)];
	UCHAR Gate[4];
	m_List.GetItemText(n,0,netaddr,sizeof(netaddr));
	m_List.GetItemText(n,1,gate,sizeof(gate));
	m_List.GetItemText(n,2,note,sizeof(note));

	StripToIp(gate,Gate);
	CAddRouteDlg dlg(netaddr,Gate,note);
	if (dlg.DoModal()==IDOK)
	{
		m_List.SetItemText(n,0,dlg.NetAddr);
		m_List.SetItemText(n,2,dlg.m_Note);
		m_List.SetItemText(n,1,IpToStrip(dlg.GATE,gate));
	}
}

void Cn2n_guiDlg::OnMenuClickedAddRoute(void)
{
	CAddRouteDlg dlg;
	char gate[16];
	if (dlg.DoModal()==IDOK)
	{
		int n=m_List.GetItemCount();
		m_List.InsertItem(n,dlg.NetAddr);
		m_List.SetItemText(n,2,dlg.m_Note);
		m_List.SetItemText(n,1,IpToStrip(dlg.GATE,gate));
	}
}


void Cn2n_guiDlg::OnBnClickedBtnSet()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	char str[20],ProFileName[MAX_PATH];
	sprintf_s(ProFileName,sizeof(ProFileName),"%sn2n.ini",ProPath);
	CSetDlg dlg(bAutoHide,bAutoConnect,ReSendIf,m_OtherParam);
	if (dlg.DoModal()==IDOK)
	{
		bAutoHide=dlg.bHide;
		bAutoConnect=dlg.bConnect;
		if (ReSendIf!=dlg.ReSendIf)
		{
			ReSendIf=dlg.ReSendIf;
			if (ReSendIf=="")
			{
				shareNet(ReSendIf);
				MessageBox("�ر����繲�����.");
			}
		}
		m_OtherParam=dlg.m_OtherParam;
		char const *param=(!m_OtherParam.IsEmpty()&&m_OtherParam!="")?m_OtherParam:NULL;		//���Ϊʲô���У�
		WritePrivateProfileString("Config","AutoHide",Itoa(bAutoHide,str),ProFileName);
		WritePrivateProfileString("Config","AutoConnect",Itoa(bAutoConnect,str),ProFileName);
		WritePrivateProfileString("Config","ReSendIf",ReSendIf,ProFileName);
		WritePrivateProfileString("Config","Param",param,ProFileName);
	}
}


LRESULT Cn2n_guiDlg::OnShowLogMsg(WPARAM w, LPARAM l)
{
	m_Log.SetSel(INT_MAX,-1);
	m_Log.ReplaceSel((char*)w);
	return LRESULT();
}


void Cn2n_guiDlg::OnBnClickedBtnClrLog()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	SetDlgItemText(IDC_EDIT_LOG,"");
}
