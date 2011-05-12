/*
 *      Copyright (C) 2005-2009 Team XBMC
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

#include "Settings.h"
#include "DVDPlayer.h"
#include "DVDPlayerTeletext.h"
#include "Application.h"
#include "utils/log.h"
#include "utils/SingleLock.h"

using namespace std;

const uint8_t rev_lut[32] =
{
  0x00,0x08,0x04,0x0c, /*  upper nibble */
  0x02,0x0a,0x06,0x0e,
  0x01,0x09,0x05,0x0d,
  0x03,0x0b,0x07,0x0f,
  0x00,0x80,0x40,0xc0, /*  lower nibble */
  0x20,0xa0,0x60,0xe0,
  0x10,0x90,0x50,0xd0,
  0x30,0xb0,0x70,0xf0
};

void CDVDTeletextTools::NextDec(int *i) /* skip to next decimal */
{
  (*i)++;

  if ((*i & 0x0F) > 0x09)
    *i += 0x06;

  if ((*i & 0xF0) > 0x90)
    *i += 0x60;

  if (*i > 0x899)
    *i = 0x100;
}

void CDVDTeletextTools::PrevDec(int *i)           /* counting down */
{
  (*i)--;

  if ((*i & 0x0F) > 0x09)
    *i -= 0x06;

  if ((*i & 0xF0) > 0x90)
    *i -= 0x60;

  if (*i < 0x100)
    *i = 0x899;
}

/* print hex-number into string, s points to last digit, caller has to provide enough space, no termination */
void CDVDTeletextTools::Hex2Str(char *s, unsigned int n)
{
  do {
    char c = (n & 0xF);
    *s-- = number2char(c);
    n >>= 4;
  } while (n);
}

signed int CDVDTeletextTools::deh24(unsigned char *p)
{
  int e = hamm24par[0][p[0]]
    ^ hamm24par[1][p[1]]
    ^ hamm24par[2][p[2]];

  int x = hamm24val[p[0]]
    + (p[1] & 127) * 16
    + (p[2] & 127) * 2048;

  return (x ^ hamm24cor[e]) | hamm24err[e];
}


CDVDTeletextData::CDVDTeletextData()
: CThread()
, m_messageQueue("teletext")
{
  m_speed = DVD_PLAYSPEED_NORMAL;

  m_messageQueue.SetMaxDataSize(40 * 256 * 1024);

  /* Initialize Data structures */
  memset(&m_TXTCache.astCachetable, 0,    sizeof(m_TXTCache.astCachetable));
  memset(&m_TXTCache.astP29,        0,    sizeof(m_TXTCache.astP29));
  ResetTeletextCache();
}

CDVDTeletextData::~CDVDTeletextData()
{
  StopThread();
  ResetTeletextCache();
}

bool CDVDTeletextData::CheckStream(CDVDStreamInfo &hints)
{
  return false;
}

bool CDVDTeletextData::OpenStream(CDVDStreamInfo &hints)
{
  return false;
}

void CDVDTeletextData::CloseStream(bool bWaitForBuffers)
{
  // wait until buffers are empty
  if (bWaitForBuffers && m_speed > 0) m_messageQueue.WaitUntilEmpty();

  m_messageQueue.Abort();

  // wait for decode_video thread to end
  CLog::Log(LOGNOTICE, "waiting for teletext data thread to exit");

  StopThread(); // will set this->m_bStop to true

  m_messageQueue.End();
  ResetTeletextCache();
}


void CDVDTeletextData::ResetTeletextCache()
{
  CSingleLock lock(m_critSection);

  /* Reset Data structures */
  for (int i = 0; i < 0x900; i++)
  {
    for (int j = 0; j < 0x80; j++)
    {
      if (m_TXTCache.astCachetable[i][j])
      {
        TextPageinfo_t *p = &(m_TXTCache.astCachetable[i][j]->pageinfo);
        if (p->p24)
          free(p->p24);

        if (p->ext)
        {
          if (p->ext->p27)
            free(p->ext->p27);

          for (int d26 = 0; d26 < 16; d26++)
          {
            if (p->ext->p26[d26])
              free(p->ext->p26[d26]);
          }
          free(p->ext);
        }
        delete m_TXTCache.astCachetable[i][j];
        m_TXTCache.astCachetable[i][j] = 0;
      }
    }
  }

  for (int i = 0; i < 9; i++)
  {
    if (m_TXTCache.astP29[i])
    {
      if (m_TXTCache.astP29[i]->p27)
        free(m_TXTCache.astP29[i]->p27);

      for (int d26 = 0; d26 < 16; d26++)
      {
        if (m_TXTCache.astP29[i]->p26[d26])
          free(m_TXTCache.astP29[i]->p26[d26]);
      }
      free(m_TXTCache.astP29[i]);
      m_TXTCache.astP29[i] = 0;
    }
    m_TXTCache.CurrentPage[i]    = -1;
    m_TXTCache.CurrentSubPage[i] = -1;
  }

  memset(&m_TXTCache.SubPageTable,  0xFF, sizeof(m_TXTCache.SubPageTable));
  memset(&m_TXTCache.astP29,        0,    sizeof(m_TXTCache.astP29));
  memset(&m_TXTCache.BasicTop,      0,    sizeof(m_TXTCache.BasicTop));
  memset(&m_TXTCache.ADIPTable,     0,    sizeof(m_TXTCache.ADIPTable));
  memset(&m_TXTCache.FlofPages,     0,    sizeof(m_TXTCache.FlofPages));
  memset(&m_TXTCache.SubtitlePages, 0,    sizeof(m_TXTCache.SubtitlePages));
  memset(&m_TXTCache.astCachetable, 0,    sizeof(m_TXTCache.astCachetable));
  memset(&m_TXTCache.TimeString,    0x20, 8);

  m_TXTCache.NationalSubset           = NAT_DEFAULT;/* default */
  m_TXTCache.NationalSubsetSecondary  = NAT_DEFAULT;
  m_TXTCache.ZapSubpageManual         = false;
  m_TXTCache.PageUpdate               = false;
  m_TXTCache.ADIP_PgMax               = -1;
  m_TXTCache.BTTok                    = false;
  m_TXTCache.CachedPages              = 0;
  m_TXTCache.PageReceiving            = -1;
  m_TXTCache.Page                     = 0x100;
  m_TXTCache.SubPage                  = m_TXTCache.SubPageTable[m_TXTCache.Page];
  m_TXTCache.line30                   = "";
  if (m_TXTCache.SubPage == 0xff)
    m_TXTCache.SubPage = 0;
}

void CDVDTeletextData::OnStartup()
{
  CThread::SetName("CDVDTeletextData");
}

void CDVDTeletextData::Process()
{
}

void CDVDTeletextData::OnExit()
{
  CLog::Log(LOGNOTICE, "thread end: data_thread");
}

void CDVDTeletextData::Flush()
{
  if(!m_messageQueue.IsInited())
    return;
  /* flush using message as this get's called from dvdplayer thread */
  /* and any demux packet that has been taken out of queue need to */
  /* be disposed of before we flush */
  m_messageQueue.Flush();
  m_messageQueue.Put(new CDVDMsg(CDVDMsg::GENERAL_FLUSH));
}

void CDVDTeletextData::Decode_p2829(unsigned char *vtxt_row, TextExtData_t **ptExtData)
{
  int bitsleft, colorindex;
  unsigned char *p;
  int t1 = CDVDTeletextTools::deh24(&vtxt_row[7-4]);
  int t2 = CDVDTeletextTools::deh24(&vtxt_row[10-4]);

  if (t1 < 0 || t2 < 0)
    return;

  if (!(*ptExtData))
    (*ptExtData) = (TextExtData_t*) calloc(1, sizeof(TextExtData_t));
  if (!(*ptExtData))
    return;

  (*ptExtData)->p28Received = 1;
  (*ptExtData)->DefaultCharset = (t1>>7) & 0x7f;
  (*ptExtData)->SecondCharset = ((t1>>14) & 0x0f) | ((t2<<4) & 0x70);
  (*ptExtData)->LSP = !!(t2 & 0x08);
  (*ptExtData)->RSP = !!(t2 & 0x10);
  (*ptExtData)->SPL25 = !!(t2 & 0x20);
  (*ptExtData)->LSPColumns = (t2>>6) & 0x0f;

  bitsleft = 8; /* # of bits not evaluated in val */
  t2 >>= 10; /* current data */
  p = &vtxt_row[13-4];  /* pointer to next data triplet */
  for (colorindex = 0; colorindex < 16; colorindex++)
  {
    if (bitsleft < 12)
    {
      t2 |= CDVDTeletextTools::deh24(p) << bitsleft;
      if (t2 < 0)  /* hamming error */
        break;
      p += 3;
      bitsleft += 18;
    }
    (*ptExtData)->bgr[colorindex] = t2 & 0x0fff;
    bitsleft -= 12;
    t2 >>= 12;
  }
  if (t2 < 0 || bitsleft != 14)
  {
    (*ptExtData)->p28Received = 0;
    return;
  }
  (*ptExtData)->DefScreenColor = t2 & 0x1f;
  t2 >>= 5;
  (*ptExtData)->DefRowColor = t2 & 0x1f;
  (*ptExtData)->BlackBgSubst = !!(t2 & 0x20);
  t2 >>= 6;
  (*ptExtData)->ColorTableRemapping = t2 & 0x07;
}

void CDVDTeletextData::SavePage(int p, int sp, unsigned char* buffer)
{
  CSingleLock lock(m_critSection);
  TextCachedPage_t* pg = m_TXTCache.astCachetable[p][sp];
  if (!pg)
  {
    CLog::Log(LOGERROR, "CDVDTeletextData: trying to save a not allocated page!!");
    return;
  }

  memcpy(pg->data, buffer, 23*40);
}

void CDVDTeletextData::LoadPage(int p, int sp, unsigned char* buffer)
{
  CSingleLock lock(m_critSection);
  TextCachedPage_t* pg = m_TXTCache.astCachetable[p][sp];
  if (!pg)
  {
    CLog::Log(LOGERROR, "CDVDTeletextData: trying to load a not allocated page!!");
    return;
  }

  memcpy(buffer, pg->data, 23*40);
}

void CDVDTeletextData::ErasePage(int magazine)
{
  CSingleLock lock(m_critSection);
  TextCachedPage_t* pg = m_TXTCache.astCachetable[m_TXTCache.CurrentPage[magazine]][m_TXTCache.CurrentSubPage[magazine]];
  if (pg)
  {
    memset(&(pg->pageinfo), 0, sizeof(TextPageinfo_t));  /* struct pageinfo */
    memset(pg->p0, ' ', 24);
    memset(pg->data, ' ', 23*40);
  }
}

void CDVDTeletextData::AllocateCache(int magazine)
{
  /* check cachetable and allocate memory if needed */
  if (m_TXTCache.astCachetable[m_TXTCache.CurrentPage[magazine]][m_TXTCache.CurrentSubPage[magazine]] == 0)
  {
    m_TXTCache.astCachetable[m_TXTCache.CurrentPage[magazine]][m_TXTCache.CurrentSubPage[magazine]] = new TextCachedPage_t;
    if (m_TXTCache.astCachetable[m_TXTCache.CurrentPage[magazine]][m_TXTCache.CurrentSubPage[magazine]] )
    {
      ErasePage(magazine);
      m_TXTCache.CachedPages++;
    }
  }
}
