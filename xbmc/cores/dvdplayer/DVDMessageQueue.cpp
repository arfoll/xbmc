/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "DVDMessageQueue.h"
#include "utils/log.h"
#include "SingleLock.h"
#include "DVDClock.h"
#include "MathUtils.h"

using namespace std;

CDVDMessageQueue::CDVDMessageQueue(const string &owner)
{
  m_owner = owner;
  m_iDataSize     = 0;
  m_bAbortRequest = false;
  m_bInitialized  = false;
  m_bCaching      = false;
  m_bEmptied      = true;

  m_TimeBack      = DVD_NOPTS_VALUE;
  m_TimeFront     = DVD_NOPTS_VALUE;
  m_TimeSize      = 1.0 / 4.0; /* 4 seconds */
  m_hEvent = CreateEvent(NULL, true, false, NULL);
}

CDVDMessageQueue::~CDVDMessageQueue()
{
  // remove all remaining messages
  Flush();

  CloseHandle(m_hEvent);
}

void CDVDMessageQueue::Init()
{
  m_iDataSize     = 0;
  m_bAbortRequest = false;
  m_bEmptied      = true;
  m_bInitialized  = true;
  m_TimeBack      = DVD_NOPTS_VALUE;
  m_TimeFront     = DVD_NOPTS_VALUE;
}

void CDVDMessageQueue::Flush(CDVDMsg::Message type)
{
  CSingleLock lock(m_section);

  for(SList::iterator it = m_list.begin(); it != m_list.end();)
  {
    if (it->message->IsType(type) ||  type == CDVDMsg::NONE)
      it = m_list.erase(it);
    else
      it++;
  }

  if (type == CDVDMsg::DEMUXER_PACKET ||  type == CDVDMsg::NONE)
  {
    m_iDataSize = 0;
    m_TimeBack  = DVD_NOPTS_VALUE;
    m_TimeFront = DVD_NOPTS_VALUE;
    m_bEmptied = true;
  }
}

void CDVDMessageQueue::Abort()
{
  CSingleLock lock(m_section);

  m_bAbortRequest = true;

  SetEvent(m_hEvent); // inform waiter for abort action
}

void CDVDMessageQueue::End()
{
  CSingleLock lock(m_section);

  Flush();

  m_bInitialized  = false;
  m_iDataSize     = 0;
  m_bAbortRequest = false;
}


MsgQueueReturnCode CDVDMessageQueue::Put(CDVDMsg* pMsg, int priority)
{
  return MSGQ_INVALID_MSG;
}

MsgQueueReturnCode CDVDMessageQueue::Get(CDVDMsg** pMsg, unsigned int iTimeoutInMilliSeconds, int &priority)
{
  return MSGQ_NOT_INITIALIZED;
}


unsigned CDVDMessageQueue::GetPacketCount(CDVDMsg::Message type)
{
  CSingleLock lock(m_section);

  if (!m_bInitialized)
    return 0;

  unsigned count = 0;
  for(SList::iterator it = m_list.begin(); it != m_list.end();it++)
  {
    if(it->message->IsType(type))
      count++;
  }

  return count;
}

void CDVDMessageQueue::WaitUntilEmpty()
{
    CLog::Log(LOGNOTICE, "CDVDMessageQueue(%s)::WaitUntilEmpty", m_owner.c_str());
    CDVDMsgGeneralSynchronize* msg = new CDVDMsgGeneralSynchronize(40000, 0);
    Put(msg->Acquire());
    msg->Wait(&m_bAbortRequest, 0);
    msg->Release();
}

int CDVDMessageQueue::GetLevel() const
{
  if(m_iDataSize > m_iMaxDataSize)
    return 100;
  if(m_iDataSize == 0)
    return 0;

  if(m_TimeBack  == DVD_NOPTS_VALUE
  || m_TimeFront == DVD_NOPTS_VALUE
  || m_TimeFront <= m_TimeBack)
    return min(100, 100 * m_iDataSize / m_iMaxDataSize);

  return min(100, MathUtils::round_int(100.0 * m_TimeSize * (m_TimeFront - m_TimeBack) / DVD_TIME_BASE ));
}
