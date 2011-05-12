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

#include "HTSPSession.h"
#include "URL.h"
#include "VideoInfoTag.h"
#include "FileItem.h"
#include "utils/log.h"
#ifdef _MSC_VER
#include <winsock2.h>
#define SHUT_RDWR SD_BOTH
#define ETIMEDOUT WSAETIMEDOUT
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

struct SContentType
{
  unsigned    id;
  const char* genre; 
};

static const SContentType g_dvb_content_group[] =
{ { 0x1, "Movie/Drama" }
, { 0x2, "News/Current Affairs" }
, { 0x3, "Show/Game show" }
, { 0x4, "Sports" }
, { 0x5, "Children's/Youth" }
, { 0x6, "Music/Ballet/Dance" }
, { 0x7, "Arts/Culture (without music)" }
, { 0x8, "Social/Political issues/Economics" }
, { 0x9, "Childrens/Youth Education/Science/Factual" }
, { 0xa, "Leisure hobbies" }
, { 0xb, "Misc" }
, { 0xf, "Unknown" }
};

static const SContentType g_dvb_content_type[] =
{
// movie/drama
  { 0x11, "Detective/Thriller" }
, { 0x12, "Adventure/Western/War" }
, { 0x13, "Science Fiction/Fantasy/Horror" }
, { 0x14, "Comedy" }
, { 0x15, "Soap/Melodrama/Folkloric" }
, { 0x16, "Romance" }
, { 0x17, "Serious/ClassicalReligion/Historical" }
, { 0x18, "Adult Movie/Drama" }

// news/current affairs
, { 0x21, "News/Weather Report" }
, { 0x22, "Magazine" }
, { 0x23, "Documentary" }
, { 0x24, "Discussion/Interview/Debate" }

// show/game show
, { 0x31, "Game show/Quiz/Contest" }
, { 0x32, "Variety" }
, { 0x33, "Talk" }

// sports
, { 0x41, "Special Event (Olympics/World cup/...)" }
, { 0x42, "Magazine" }
, { 0x43, "Football/Soccer" }
, { 0x44, "Tennis/Squash" }
, { 0x45, "Team sports (excluding football)" }
, { 0x46, "Athletics" }
, { 0x47, "Motor Sport" }
, { 0x48, "Water Sport" }
, { 0x49, "Winter Sports" }
, { 0x4a, "Equestrian" }
, { 0x4b, "Martial sports" }

// childrens/youth
, { 0x51, "Pre-school" }
, { 0x52, "Entertainment (6 to 14 year-olds)" }
, { 0x53, "Entertainment (10 to 16 year-olds)" }
, { 0x54, "Informational/Educational/Schools" }
, { 0x55, "Cartoons/Puppets" }

// music/ballet/dance
, { 0x61, "Rock/Pop" }
, { 0x62, "Serious music/Classical Music" }
, { 0x63, "Folk/Traditional music" }
, { 0x64, "Jazz" }
, { 0x65, "Musical/Opera" }
, { 0x66, "Ballet" }

// arts/culture
, { 0x71, "Performing Arts" }
, { 0x72, "Fine Arts" }
, { 0x73, "Religion" }
, { 0x74, "Popular Culture/Tradital Arts" }
, { 0x75, "Literature" }
, { 0x76, "Film/Cinema" }
, { 0x77, "Experimental Film/Video" }
, { 0x78, "Broadcasting/Press" }
, { 0x79, "New Media" }
, { 0x7a, "Magazine" }
, { 0x7b, "Fashion" }

// social/political/economic
, { 0x81, "Magazine/Report/Domentary" }
, { 0x82, "Economics/Social Advisory" }
, { 0x83, "Remarkable People" }

// children's youth: educational/science/factual
, { 0x91, "Nature/Animals/Environment" }
, { 0x92, "Technology/Natural sciences" }
, { 0x93, "Medicine/Physiology/Psychology" }
, { 0x94, "Foreign Countries/Expeditions" }
, { 0x95, "Social/Spiritual Sciences" }
, { 0x96, "Further Education" }
, { 0x97, "Languages" }

// leisure hobbies
, { 0xa1, "Tourism/Travel" }
, { 0xa2, "Handicraft" }
, { 0xa3, "Motoring" }
, { 0xa4, "Fitness & Health" }
, { 0xa5, "Cooking" }
, { 0xa6, "Advertisement/Shopping" }
, { 0xa7, "Gardening" }

// misc
, { 0xb0, "Original Language" }
, { 0xb1, "Black and White" }
, { 0xb2, "Unpublished" }
, { 0xb3, "Live Broadcast" }
};

using namespace std;
using namespace HTSP;

string CHTSPSession::GetGenre(unsigned type)
{
  // look for full content
  for(unsigned int i = 0; i < sizeof(g_dvb_content_type) / sizeof(g_dvb_content_type[0]); i++)
  {
    if(g_dvb_content_type[i].id == type)
      return g_dvb_content_type[i].genre;
  }

  // look for group
  type = (type >> 4) & 0xf;
  for(unsigned int i = 0; i < sizeof(g_dvb_content_group) / sizeof(g_dvb_content_group[0]); i++)
  {
    if(g_dvb_content_group[i].id == type)
      return g_dvb_content_group[i].genre;
  }

  return "";
}

CHTSPSession::CHTSPSession()
  : m_fd(INVALID_SOCKET)
  , m_seq(0)
  , m_challenge(NULL)
  , m_challenge_len(0)
  , m_protocol(0)
  , m_queue_size(1000)
{
}

CHTSPSession::~CHTSPSession()
{
  Close();
}

void CHTSPSession::Abort()
{
  shutdown(m_fd, SHUT_RDWR);
}

void CHTSPSession::Close()
{
  if(m_fd != INVALID_SOCKET)
  {
    closesocket(m_fd);
    m_fd = INVALID_SOCKET;
  }

  if(m_challenge)
  {
    free(m_challenge);
    m_challenge     = NULL;
    m_challenge_len = 0;
  }
}

bool CHTSPSession::Connect(const std::string& hostname, int port)
{
  return false;
}

bool CHTSPSession::Auth(const std::string& username, const std::string& password)
{
  return NULL;
}

htsmsg_t* CHTSPSession::ReadMessage(int timeout)
{
  return NULL;
}

bool CHTSPSession::SendMessage(htsmsg_t* m)
{
  return false;
}

htsmsg_t* CHTSPSession::ReadResult(htsmsg_t* m, bool sequence)
{
  return NULL;
}

bool CHTSPSession::ReadSuccess(htsmsg_t* m, bool sequence, std::string action)
{
  return false;
}

bool CHTSPSession::SendSubscribe(int subscription, int channel)
{
  return false;
}

bool CHTSPSession::SendUnsubscribe(int subscription)
{
  return false;
}

bool CHTSPSession::SendEnableAsync()
{
  return false;
}

bool CHTSPSession::GetEvent(SEvent& event, uint32_t id)
{
  event.Clear();
  return false;
}

bool CHTSPSession::ParseEvent(htsmsg_t* msg, uint32_t id, SEvent &event)
{
  return false;
}

void CHTSPSession::ParseChannelRemove(htsmsg_t* msg, SChannels &channels)
{
  return;
}

void CHTSPSession::ParseTagUpdate(htsmsg_t* msg, STags &tags)
{
  return;
}

void CHTSPSession::ParseTagRemove(htsmsg_t* msg, STags &tags)
{
  return;
}

bool CHTSPSession::ParseItem(const SChannel& channel, int tagid, const SEvent& event, CFileItem& item)
{
  CVideoInfoTag* tag = item.GetVideoInfoTag();

  CStdString temp;

  CURL url(item.m_strPath);
  temp.Format("tags/%d/%d.ts", tagid, channel.id);
  url.SetFileName(temp);

  tag->m_iSeason  = 0;
  tag->m_iEpisode = 0;
  tag->m_iTrack       = channel.num;
  tag->m_strAlbum     = channel.name;
  tag->m_strShowTitle = event.title;
  tag->m_strPlot      = event.descs;
  tag->m_strStatus    = "livetv";
  tag->m_strGenre     = GetGenre(event.content);

  tag->m_strTitle = tag->m_strAlbum;
  if(tag->m_strShowTitle.length() > 0)
    tag->m_strTitle += " : " + tag->m_strShowTitle;

  item.m_strPath  = url.Get();
  item.m_strTitle = tag->m_strTitle;
  item.SetThumbnailImage(channel.icon);
  item.SetMimeType("video/X-htsp");
  item.SetCachedVideoThumb();
  return true;
}

bool CHTSPSession::ParseQueueStatus (htsmsg_t* msg, SQueueStatus &queue)
{
  return false;
}
