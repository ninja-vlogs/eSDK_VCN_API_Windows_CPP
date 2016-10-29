/*Copyright 2015 Huawei Technologies Co., Ltd. All rights reserved.
eSDK is licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
		http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.*/

#include "NetSource.h"
#include "ChannelInfo.h"
#include "IVSCommon.h"
#include "ivs_log.h"
#include "_BaseThread.h"
#include "Connection.h"
#include "eSDK_Securec.h"

CNetSource::CNetSource(void)
    : m_pChannelInfo(NULL)
	, m_pSourceThread(NULL)
    , m_pRecvBuf(NULL)
    , m_ulRecvBufLen(0)
    , m_ulLastSendNatTick(0)
	, m_ulLastCheckTimeoutTick(0)
    , m_bRun(false)
{
   
}

CNetSource::~CNetSource(void)
{
    Release();
    m_ulLastSendNatTick = 0;
	m_ulLastCheckTimeoutTick = 0;
    m_bRun = false;
}

// ������ѭ���ӿڶ��壬������ɹ���ѭ���߼�;
void CNetSource::MainLoop()
{
	while (true)//lint !e716
	{
		// ����;
		{
			/*
			 * ������״̬�����жϲ���Ҫ������modify by w00210470
			 */
			//CAutoLock lock(m_RunFlagMutex);
			if (!m_bRun)
			{
				IVS_LOG(IVS_LOG_DEBUG, "Main loop", "m_bRun is false, thread will be exit.");
				break;
			}
		}

		// ��Խ;
		(void)CheckNatSend();

		// ����;
		ProcessRecevData();
	}
}

// ֹͣ����ѭ���ӿڶ��壬������ͣ����;
void CNetSource::kill()
{
	//ֹͣ�߳�;
	//CAutoLock lock(m_RunFlagMutex);
	m_bRun = false;
}

int CNetSource::Init(CChannelInfo* pChannelInfo)
{
	(void)IVS_NEW(m_pRecvBuf, RTP_MEDIA_BUF_LEN);
	if (NULL != m_pRecvBuf)
	{
		eSDK_MEMSET(m_pRecvBuf, 0, RTP_MEDIA_BUF_LEN);
	}
	else
	{
		IVS_LOG(IVS_LOG_ERR, "Init", "Alloc recveive buffer memory failed.");
		return IVS_ALLOC_MEMORY_ERROR;
	}

    if (NULL == pChannelInfo)
    {
        IVS_LOG(IVS_LOG_ERR, "Init", "Input param error, pChannelInfo is NULL");
        return IVS_PARA_INVALID;
    }
    m_pChannelInfo = pChannelInfo;
    return IVS_SUCCEED;
}

void CNetSource::Release()
{
    try
    {
		IVS_DELETE(m_pSourceThread);

        m_pChannelInfo = NULL;
        IVS_DELETE(m_pRecvBuf, MUILI);//lint !e1551
        m_pRecvBuf = NULL; 
        m_ulRecvBufLen = 0;
    }
    catch(...)
    {
    }
}

void CNetSource::ProcMediaPacket()
{
    if (NULL == m_pRecvBuf)
    {
        IVS_LOG(IVS_LOG_ERR, "ProcMediaPacket", "Source process packet error, receive buffer is null.");  
        return;
    }
    
    NET_DATA_CALLBACK cbDataCallBack = NULL;  //�������ݻص�����;
    void* pUserData = NULL;       //�û�����;
    m_pChannelInfo->GetDataCallBack(cbDataCallBack, pUserData);
    if (NULL == cbDataCallBack)
    {
        IVS_LOG(IVS_LOG_ERR, "ProcMediaPacket", "data call back pointer is null.");  
        return;
    }

    cbDataCallBack(m_pRecvBuf, m_ulRecvBufLen, pUserData);
    return;		
}

//���������쳣�ϱ�;
void CNetSource::DoExceptionCallBack(int iMsgType, void*  pParam)
{
    if (NULL == m_pChannelInfo)
    {
        IVS_LOG(IVS_LOG_ERR, "DoExceptionCallBack", "Channel info is NULL.");  
        return;
    }
    NET_EXCEPTION_CALLBACK cbExceptionCallBack = NULL;
    m_pChannelInfo->GetExceptionCallBack(cbExceptionCallBack);

    if (NULL != cbExceptionCallBack)
    { 
        cbExceptionCallBack(0, iMsgType, pParam, NULL); // ��ģ�鲻��Ҫ��עͨ����;
    }
}

int CNetSource::CheckTimeOut()
{
    if (NULL == m_pChannelInfo)
    {
        IVS_LOG(IVS_LOG_ERR, "CheckTimeOut", "Channel info is NULL.");  
        return IVS_FAIL;
    }

	unsigned long ulInterval = GetTickCount() - m_ulLastCheckTimeoutTick;
	IVS_LOG(IVS_LOG_DEBUG, "CheckTimeOut", "ulInterval[%lu].", ulInterval); 

    //�жϳ�ʱ�Ƿ����30��;
    if (ulInterval >= RECV_TOTAL_TIMEOUT_TIME)
    {
        //ý������ʱ�쳣�ϱ�;
        unsigned int ulChannel = m_pChannelInfo->GetChannelNo();
        unsigned int* pChannelNo = &ulChannel;
        DoExceptionCallBack(IVS_PLAYER_RET_RECV_DATA_TIMEOUT, (void*)pChannelNo);

        m_ulLastCheckTimeoutTick = GetTickCount();
    }

    return IVS_SUCCEED;
}

// ����Ƿ�����NAT��Խ����,ÿ��30s�����������nat��Ϣ;
int CNetSource::CheckNatSend()
{
	if (m_ulLastSendNatTick == 0)
	{
		m_ulLastSendNatTick = GetTickCount();
		return IVS_SUCCEED;
	}

	unsigned long ulInterval = GetTickCount() - m_ulLastSendNatTick;
	//�жϳ�ʱ�Ƿ����20��;
	if (ulInterval < SEND_NAT_INTERVAL)
	{
		return IVS_FAIL;
	}

	m_ulLastSendNatTick = GetTickCount();
	return IVS_SUCCEED;
}

// ��������;
int CNetSource::Start()
{
	IVS_LOG(IVS_LOG_DEBUG, "Start", "Start thread.");  
	int iRet = IVS_SUCCEED;
	// ���������������߳�;
	try
	{
		CAutoLock lock(m_RunFlagMutex);
		m_bRun = true;
		m_pSourceThread = new _BaseThread(this, 0);
	}
	catch(...)
	{
		m_pSourceThread = NULL;
		iRet = IVS_ALLOC_MEMORY_ERROR;
		IVS_LOG(iRet, "Start", "Create net source thread error");
	}

    // ��������ʱ������������ʱ��NAT��Խ�ĳ�ʼ����ʱ��;
    m_ulLastCheckTimeoutTick = GetTickCount();
    //m_ulLastSendNatTick = GetTickCount();
    return iRet;
}

// ֹͣ����;
void CNetSource::Stop()
{
	IVS_LOG(IVS_LOG_DEBUG, "Stop", "stop thread.");  
	
	try
	{
		IVS_DELETE(m_pSourceThread);
	}
	catch (...)
	{
		IVS_LOG(IVS_LOG_ERR, "Stop", "delete source thread throw exception.");  
	}
	
    return;
}