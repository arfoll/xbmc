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

#include "DVDPlayerSubtitle.h"
#include "DVDClock.h"
#include "utils/log.h"
#ifdef _LINUX
#include "config.h"
#endif

using namespace std;

CDVDPlayerSubtitle::CDVDPlayerSubtitle(CDVDOverlayContainer* pOverlayContainer)
{
  m_pOverlayContainer = pOverlayContainer;

  m_pSubtitleFileParser = NULL;
  m_pSubtitleStream = NULL;
  m_pOverlayCodec = NULL;
  m_lastPts = DVD_NOPTS_VALUE;
}

CDVDPlayerSubtitle::~CDVDPlayerSubtitle()
{
  CloseStream(false);
}


void CDVDPlayerSubtitle::Flush()
{
  SendMessage(new CDVDMsg(CDVDMsg::GENERAL_FLUSH));
}

void CDVDPlayerSubtitle::SendMessage(CDVDMsg* pMsg)
{
}

bool CDVDPlayerSubtitle::OpenStream(CDVDStreamInfo &hints, string &filename)
{
  return false;
}

void CDVDPlayerSubtitle::CloseStream(bool flush)
{
  if(m_pSubtitleStream)
    SAFE_DELETE(m_pSubtitleStream);
  if(m_pSubtitleFileParser)
    SAFE_DELETE(m_pSubtitleFileParser);
  if(m_pOverlayCodec)
    SAFE_DELETE(m_pOverlayCodec);

  m_dvdspus.FlushCurrentPacket();

  if(flush)
    m_pOverlayContainer->Clear();
}

void CDVDPlayerSubtitle::Process(double pts)
{
  if (m_pSubtitleFileParser)
  {
    if(pts == DVD_NOPTS_VALUE)
      return;

    if (pts < m_lastPts)
      m_pOverlayContainer->Clear();

    if(m_pOverlayContainer->GetSize() >= 5)
      return;

    m_lastPts = pts;
  }
}

bool CDVDPlayerSubtitle::AcceptsData()
{
  // FIXME : This may still be causing problems + magic number :(
  return m_pOverlayContainer->GetSize() < 5;
}

bool CDVDPlayerSubtitle::GetCurrentSubtitle(CStdString& strSubtitle, double pts)
{
  return false; 
}
