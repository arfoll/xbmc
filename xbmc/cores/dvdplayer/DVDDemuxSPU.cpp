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

#include "DVDDemuxSPU.h"
#include "Util.h"
#include "DVDClock.h"
#include "utils/log.h"

#undef ALIGN
#define ALIGN(value, alignment) (((value)+((alignment)-1))&~((alignment)-1))

// #define SPU_DEBUG

void DebugLog(const char *format, ...)
{
#ifdef SPU_DEBUG
  static char temp_spubuffer[1024];
  va_list va;

  va_start(va, format);
  _vsnprintf(temp_spubuffer, 1024, format, va);
  va_end(va);

  CLog::Log(LOGDEBUG,temp_spubuffer);
#endif
}

CDVDDemuxSPU::CDVDDemuxSPU()
{
  memset(&m_spuData, 0, sizeof(m_spuData));
  memset(m_clut, 0, sizeof(m_clut));
  m_bHasClut = false;
}

CDVDDemuxSPU::~CDVDDemuxSPU()
{
  if (m_spuData.data) free(m_spuData.data);
}

void CDVDDemuxSPU::Reset()
{
  FlushCurrentPacket();

  // We can't reset this during playback, cause we don't always
  // get a new clut from libdvdnav leading to invalid colors
  // so let's just never reset it. It will only be reset
  // when dvdplayer is destructed and constructed
  // m_bHasClut = false;
  // memset(m_clut, 0, sizeof(m_clut));
}

void CDVDDemuxSPU::FlushCurrentPacket()
{
  if (m_spuData.data) free(m_spuData.data);
  memset(&m_spuData, 0, sizeof(m_spuData));
}

char* CDVDDemuxSPU::AddData(BYTE* data, int iSize, double pts)
{
  return NULL;
}

#define CMD_END     0xFF
#define FSTA_DSP    0x00
#define STA_DSP     0x01
#define STP_DSP     0x02
#define SET_COLOR   0x03
#define SET_CONTR   0x04
#define SET_DAREA   0x05
#define SET_DSPXA   0x06
#define CHG_COLCON  0x07

char* CDVDDemuxSPU::ParsePacket(SPUData* pSPUData)
{
  return NULL;
}

/*****************************************************************************
 * AddNibble: read a nibble from a source packet and add it to our integer.
 *****************************************************************************/
inline unsigned int AddNibble( unsigned int i_code, BYTE* p_src, unsigned int* pi_index )
{
  if ( *pi_index & 0x1 )
  {
    return ( i_code << 4 | ( p_src[(*pi_index)++ >> 1] & 0xf ) );
  }
  else
  {
    return ( i_code << 4 | p_src[(*pi_index)++ >> 1] >> 4 );
  }
}

/*****************************************************************************
 * ParseRLE: parse the RLE part of the subtitle
 *****************************************************************************
 * This part parses the subtitle graphical data and stores it in a more
 * convenient structure for later decoding. For more information on the
 * subtitles format, see http://sam.zoy.org/doc/dvd/subtitles/index.html
 *****************************************************************************/
char* CDVDDemuxSPU::ParseRLE(char* pSPU, BYTE* pUnparsedData)
{
  return false;
}

void CDVDDemuxSPU::FindSubtitleColor(int last_color, int stats[4], char* pSPU)
{
  return;
}

bool CDVDDemuxSPU::CanDisplayWithAlphas(int a[4], int stats[4])
{
  return(
    a[0] * stats[0] > 0 ||
    a[1] * stats[1] > 0 ||
    a[2] * stats[2] > 0 ||
    a[3] * stats[3] > 0);
}
