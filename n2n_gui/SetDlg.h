#pragma once


// CSetDlg �Ի���

class CSetDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CSetDlg)

public:
	CSetDlg(CWnd* pParent = NULL);   // ��׼���캯��
	CSetDlg(bool _Hide,CWnd* pParent = NULL);   // ��׼���캯��
	virtual ~CSetDlg();

// �Ի�������
	enum { IDD = IDD_SET_DLG };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV ֧��

	DECLARE_MESSAGE_MAP()
public:
	bool bHide;
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCheckResend();
	virtual BOOL OnInitDialog();
};
