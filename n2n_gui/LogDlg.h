#pragma once
#include "afxwin.h"


#define  ON_SHOWLOG_MSG		(WM_USER+1)

// CLogDlg �Ի���

class CLogDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CLogDlg)

public:
	CLogDlg(CWnd* pParent = NULL);   // ��׼���캯��
	virtual ~CLogDlg();

// �Ի�������
	enum { IDD = IDD_LOG_DLG };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV ֧��

	DECLARE_MESSAGE_MAP()

	LRESULT OnShowLogMsg(WPARAM w, LPARAM l);
public:
	CEdit m_Log;
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedBtnClrLog();
};
