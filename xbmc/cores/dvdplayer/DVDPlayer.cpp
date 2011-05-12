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

#include "system.h"
#include "DVDPlayer.h"

#include "DVDFileInfo.h"

#include "Util.h"
#include "utils/GUIInfoManager.h"
#include "GUIWindowManager.h"
#include "Application.h"
#include "DVDPerformanceCounter.h"
#include "FileSystem/File.h"
#include "Picture.h"
#ifdef HAS_VIDEO_PLAYBACK
#include "cores/VideoRenderers/RenderManager.h"
#endif
#ifdef HAS_PERFORMANCE_SAMPLE
#include "xbmc/utils/PerformanceSample.h"
#else
#define MEASURE_FUNCTION
#endif
#include "AdvancedSettings.h"
#include "FileItem.h"
#include "MouseStat.h"
#include "GUISettings.h"
#include "GUIUserMessages.h"
#include "Settings.h"
#include "LocalizeStrings.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "utils/StreamDetails.h"
#include "MediaManager.h"
#include "GUIDialogBusy.h"

using namespace std;

CDVDPlayer::CDVDPlayer(IPlayerCallback& callback)
    : IPlayer(callback),
      CThread(),
      m_messenger("player"),
      m_dvdPlayerVideo(&m_clock, &m_overlayContainer, m_messenger),
      m_dvdPlayerAudio(&m_clock, m_messenger),
      m_dvdPlayerSubtitle(&m_overlayContainer),
      m_dvdPlayerTeletext(),
      m_ready(true)
{
  m_pDemuxer = NULL;
  m_pSubtitleDemuxer = NULL;
  m_pInputStream = NULL;

  InitializeCriticalSection(&m_critStreamSection);

  m_dvd.Clear();
  m_State.Clear();
  m_UpdateApplication = 0;

  m_bAbortRequest = false;
  m_errorCount = 0;
  m_playSpeed = DVD_PLAYSPEED_NORMAL;
  m_caching = CACHESTATE_DONE;
  m_subLastPts = DVD_NOPTS_VALUE;
  
#ifdef DVDDEBUG_MESSAGE_TRACKER
  g_dvdMessageTracker.Init();
#endif
}

CDVDPlayer::~CDVDPlayer()
{
  CloseFile();

  DeleteCriticalSection(&m_critStreamSection);
#ifdef DVDDEBUG_MESSAGE_TRACKER
  g_dvdMessageTracker.DeInit();
#endif
}

bool CDVDPlayer::OpenFile(const CFileItem& file, const CPlayerOptions &options)
{
  try
  {
    CLog::Log(LOGNOTICE, "DVDPlayer: Opening: %s", file.m_strPath.c_str());

    // if playing a file close it first
    // this has to be changed so we won't have to close it.
    if(ThreadHandle())
      CloseFile();

    m_bAbortRequest = false;
    SetPlaySpeed(DVD_PLAYSPEED_NORMAL);

    m_State.Clear();
    m_UpdateApplication = 0;

    m_PlayerOptions = options;
    m_item     = file;
    m_mimetype  = file.GetMimeType();
    m_filename = file.m_strPath;

    m_ready.Reset();
    Create();
    if(!m_ready.WaitMSec(100))
    {
      CGUIDialogBusy* dialog = (CGUIDialogBusy*)g_windowManager.GetWindow(WINDOW_DIALOG_BUSY);
      dialog->Show();
      while(!m_ready.WaitMSec(1))
        g_windowManager.Process(true);
      dialog->Close();
    }

    // Playback might have been stopped due to some error
    if (m_bStop || m_bAbortRequest)
      return false;

    return true;
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "%s - Exception thrown on open", __FUNCTION__);
    return false;
  }
}

bool CDVDPlayer::CloseFile()
{
  CLog::Log(LOGNOTICE, "CDVDPlayer::CloseFile()");

  // unpause the player
  SetPlaySpeed(DVD_PLAYSPEED_NORMAL);

  // set the abort request so that other threads can finish up
  m_bAbortRequest = true;

  CLog::Log(LOGNOTICE, "DVDPlayer: waiting for threads to exit");

  // wait for the main thread to finish up
  // since this main thread cleans up all other resources and threads
  // we are done after the StopThread call
  StopThread();

  m_Edl.Clear();
  m_EdlAutoSkipMarkers.Clear();

  CLog::Log(LOGNOTICE, "DVDPlayer: finished waiting");
#if defined(HAS_VIDEO_PLAYBACK)
  g_renderManager.UnInit();
#endif
  return true;
}

bool CDVDPlayer::IsPlaying() const
{
  return !m_bStop;
}

void CDVDPlayer::OnStartup()
{
  CThread::SetName("CDVDPlayer");
  m_CurrentVideo.Clear();
  m_CurrentAudio.Clear();
  m_CurrentSubtitle.Clear();
  m_CurrentTeletext.Clear();

  m_messenger.Init();

  g_dvdPerformanceCounter.EnableMainPerformance(ThreadHandle());
}

bool CDVDPlayer::OpenInputStream()
{
  CLog::Log(LOGNOTICE, "Creating InputStream");

  // correct the filename if needed
  CStdString filename(m_filename);
  if (filename.Find("dvd://") == 0
  ||  filename.CompareNoCase("d:\\video_ts\\video_ts.ifo") == 0
  ||  filename.CompareNoCase("iso9660://video_ts/video_ts.ifo") == 0)
  {
    m_filename = g_mediaManager.TranslateDevicePath("");
  }

  CLog::Log(LOGERROR, "CDVDPlayer::OpenInputStream - error opening [%s]", m_filename.c_str());
  return false;
}

bool CDVDPlayer::OpenDemuxStream()
{
  CLog::Log(LOGNOTICE, "Creating Demuxer");

  return false;
}

void CDVDPlayer::OpenDefaultStreams()
{
}

void CDVDPlayer::Process()
{
  m_bAbortRequest = true;
  return;
}

void CDVDPlayer::HandlePlaySpeed()
{
  if(IsInMenu() && m_caching != CACHESTATE_DONE)
    SetCaching(CACHESTATE_DONE);

  if(m_caching == CACHESTATE_INIT)
  {
    // if all enabled streams have been inited we are done
    if((m_CurrentVideo.id < 0 || m_CurrentVideo.started)
    && (m_CurrentAudio.id < 0 || m_CurrentAudio.started))
      SetCaching(CACHESTATE_PLAY);
  }
  if(m_caching == CACHESTATE_PLAY)
  {
    // if all enabled streams have started playing we are done
    if((m_CurrentVideo.id < 0 || !m_dvdPlayerVideo.IsStalled())
    && (m_CurrentAudio.id < 0 || !m_dvdPlayerAudio.IsStalled()))
      SetCaching(CACHESTATE_DONE);
  }

  if(GetPlaySpeed() != DVD_PLAYSPEED_NORMAL && GetPlaySpeed() != DVD_PLAYSPEED_PAUSE)
  {
    if (IsInMenu())
    {
      // this can't be done in menu
      SetPlaySpeed(DVD_PLAYSPEED_NORMAL);

    }
    else if (m_CurrentVideo.id >= 0
          &&  m_CurrentVideo.inited == true
          &&  m_SpeedState.lastpts  != m_dvdPlayerVideo.GetCurrentPts()
          &&  m_SpeedState.lasttime != GetTime())
    {
      m_SpeedState.lastpts  = m_dvdPlayerVideo.GetCurrentPts();
      m_SpeedState.lasttime = GetTime();
      // check how much off clock video is when ff/rw:ing
      // a problem here is that seeking isn't very accurate
      // and since the clock will be resynced after seek
      // we might actually not really be playing at the wanted
      // speed. we'd need to have some way to not resync the clock
      // after a seek to remember timing. still need to handle
      // discontinuities somehow

      // when seeking, give the player a headstart to make sure
      // the time it takes to seek doesn't make a difference.
      double error;
      error  = m_clock.GetClock() - m_SpeedState.lastpts;
      error *= m_playSpeed / abs(m_playSpeed);

      if(error > DVD_MSEC_TO_TIME(1000))
      {
        CLog::Log(LOGDEBUG, "CDVDPlayer::Process - Seeking to catch up");
        __int64 iTime = (__int64)DVD_TIME_TO_MSEC(m_clock.GetClock() + m_State.time_offset + 500000.0 * m_playSpeed / DVD_PLAYSPEED_NORMAL);
        m_messenger.Put(new CDVDMsgPlayerSeek(iTime, (GetPlaySpeed() < 0), true, false, false, true));
      }
    }
  }
}

bool CDVDPlayer::CheckStartCaching(CCurrentStream& current)
{
  return false;
}

bool CDVDPlayer::CheckPlayerInit(CCurrentStream& current, unsigned int source)
{
  if(current.startpts != DVD_NOPTS_VALUE
  && current.dts      != DVD_NOPTS_VALUE)
  {
    if((current.startpts - current.dts) > DVD_SEC_TO_TIME(20))
    {
      CLog::Log(LOGDEBUG, "%s - too far to decode before finishing seek", __FUNCTION__);
      if(m_CurrentAudio.startpts != DVD_NOPTS_VALUE)
        m_CurrentAudio.startpts = current.dts;
      if(m_CurrentVideo.startpts != DVD_NOPTS_VALUE)
        m_CurrentVideo.startpts = current.dts;
      if(m_CurrentSubtitle.startpts != DVD_NOPTS_VALUE)
        m_CurrentSubtitle.startpts = current.dts;
      if(m_CurrentTeletext.startpts != DVD_NOPTS_VALUE)
        m_CurrentTeletext.startpts = current.dts;
    }

    if(current.startpts <= current.dts)
      current.startpts = DVD_NOPTS_VALUE;
  }

  // await start sync to be finished
  if(current.startpts != DVD_NOPTS_VALUE)
  {
    CLog::Log(LOGDEBUG, "%s - dropping packet type:%d dts:%f to get to start point at %f", __FUNCTION__, source,  current.dts, current.startpts);
    return true;
  }

  // send of the sync message if any
  if(current.startsync)
  {
    SendPlayerMessage(current.startsync, source);
    current.startsync = NULL;
  }

  //If this is the first packet after a discontinuity, send it as a resync
  if (current.inited == false && current.dts != DVD_NOPTS_VALUE)
  {
    current.inited   = true;
    current.startpts = current.dts;

    bool setclock = false;
    if(m_playSpeed == DVD_PLAYSPEED_NORMAL)
    {
      if(     source == DVDPLAYER_AUDIO)
        setclock = !m_CurrentVideo.inited;
      else if(source == DVDPLAYER_VIDEO)
        setclock = !m_CurrentAudio.inited;
    }
    else
    {
      if(source == DVDPLAYER_VIDEO)
        setclock = true;
    }

    double starttime = current.startpts;
    if(m_CurrentAudio.inited
    && m_CurrentAudio.startpts != DVD_NOPTS_VALUE
    && m_CurrentAudio.startpts < starttime)
      starttime = m_CurrentAudio.startpts;
    if(m_CurrentVideo.inited
    && m_CurrentVideo.startpts != DVD_NOPTS_VALUE
    && m_CurrentVideo.startpts < starttime)
      starttime = m_CurrentVideo.startpts;

    starttime = current.startpts - starttime;
    if(starttime > 0)
    {
      if(starttime > DVD_SEC_TO_TIME(2))
        CLog::Log(LOGWARNING, "CDVDPlayer::CheckPlayerInit(%d) - Ignoring too large delay of %f", source, starttime);
      else
        SendPlayerMessage(new CDVDMsgDouble(CDVDMsg::GENERAL_DELAY, starttime), source);
    }

    SendPlayerMessage(new CDVDMsgGeneralResync(current.dts, setclock), source);
  }
  return false;
}

bool CDVDPlayer::CheckSceneSkip(CCurrentStream& current)
{
  if(!m_Edl.HasCut())
    return false;

  if(current.dts == DVD_NOPTS_VALUE)
    return false;

  if(current.startpts != DVD_NOPTS_VALUE)
    return false;

  CEdl::Cut cut;
  return m_Edl.InCut(DVD_TIME_TO_MSEC(current.dts), &cut) && cut.action == CEdl::CUT;
}

void CDVDPlayer::CheckAutoSceneSkip()
{
  if(!m_Edl.HasCut())
    return;

  /*
   * Check that there is an audio and video stream.
   */
  if(m_CurrentAudio.id < 0
  || m_CurrentVideo.id < 0)
    return;

  /*
   * If there is a startpts defined for either the audio or video stream then dvdplayer is still
   * still decoding frames to get to the previously requested seek point.
   */
  if(m_CurrentAudio.startpts != DVD_NOPTS_VALUE
  || m_CurrentVideo.startpts != DVD_NOPTS_VALUE)
    return;

  if(m_CurrentAudio.dts == DVD_NOPTS_VALUE
  || m_CurrentVideo.dts == DVD_NOPTS_VALUE)
    return;

  const int64_t clock = DVD_TIME_TO_MSEC(min(m_CurrentAudio.dts, m_CurrentVideo.dts));

  CEdl::Cut cut;
  if(!m_Edl.InCut(clock, &cut))
    return;

  if(cut.action == CEdl::CUT
  && !(cut.end == m_EdlAutoSkipMarkers.cut || cut.start == m_EdlAutoSkipMarkers.cut)) // To prevent looping if same cut again
  {
    CLog::Log(LOGDEBUG, "%s - Clock in EDL cut [%s - %s]: %s. Automatically skipping over.",
              __FUNCTION__, CEdl::MillisecondsToTimeString(cut.start).c_str(),
              CEdl::MillisecondsToTimeString(cut.end).c_str(), CEdl::MillisecondsToTimeString(clock).c_str());
    /*
     * Seeking either goes to the start or the end of the cut depending on the play direction.
     */
    __int64 seek = GetPlaySpeed() >= 0 ? cut.end : cut.start;
    /*
     * Seeking is NOT flushed so any content up to the demux point is retained when playing forwards.
     */
    m_messenger.Put(new CDVDMsgPlayerSeek((int)seek, true, false, true, false));
    /*
     * Seek doesn't always work reliably. Last physical seek time is recorded to prevent looping
     * if there was an error with seeking and it landed somewhere unexpected, perhaps back in the
     * cut. The cut automatic skip marker is reset every 500ms allowing another attempt at the seek.
     */
    m_EdlAutoSkipMarkers.cut = GetPlaySpeed() >= 0 ? cut.end : cut.start;
  }
  else if(cut.action == CEdl::COMM_BREAK
  &&      GetPlaySpeed() >= 0
  &&      cut.start > m_EdlAutoSkipMarkers.commbreak_end)
  {
    CLog::Log(LOGDEBUG, "%s - Clock in commercial break [%s - %s]: %s. Automatically skipping to end of commercial break (only done once per break)",
              __FUNCTION__, CEdl::MillisecondsToTimeString(cut.start).c_str(), CEdl::MillisecondsToTimeString(cut.end).c_str(),
              CEdl::MillisecondsToTimeString(clock).c_str());
    /*
     * Seeking is NOT flushed so any content up to the demux point is retained when playing forwards.
     */
    m_messenger.Put(new CDVDMsgPlayerSeek(cut.end + 1, true, false, true, false));
    /*
     * Each commercial break is only skipped once so poorly detected commercial breaks can be
     * manually re-entered. Start and end are recorded to prevent looping and to allow seeking back
     * to the start of the commercial break if incorrectly flagged.
     */
    m_EdlAutoSkipMarkers.commbreak_start = cut.start;
    m_EdlAutoSkipMarkers.commbreak_end   = cut.end;
    m_EdlAutoSkipMarkers.seek_to_start   = true; // Allow backwards Seek() to go directly to the start
  }

  /*
   * Reset the EDL automatic skip cut marker every 500 ms.
   */
  m_EdlAutoSkipMarkers.ResetCutMarker(500); // in msec
}


void CDVDPlayer::SynchronizeDemuxer(DWORD timeout)
{
  if(IsCurrentThread())
    return;
  if(!m_messenger.IsInited())
    return;

  CDVDMsgGeneralSynchronize* message = new CDVDMsgGeneralSynchronize(timeout, 0);
  m_messenger.Put(message->Acquire());
  message->Wait(&m_bStop, 0);
  message->Release();
}

void CDVDPlayer::SynchronizePlayers(DWORD sources)
{
  /* if we are awaiting a start sync, we can't sync here or we could deadlock */
  if(m_CurrentAudio.startsync
  || m_CurrentVideo.startsync
  || m_CurrentSubtitle.startsync
  || m_CurrentTeletext.startsync)
  {
    CLog::Log(LOGDEBUG, "%s - can't sync since we are already awaiting a sync", __FUNCTION__);
    return;
  }

  /* we need a big timeout as audio queue is about 8seconds for 2ch ac3 */
  const int timeout = 10*1000; // in milliseconds

  CDVDMsgGeneralSynchronize* message = new CDVDMsgGeneralSynchronize(timeout, sources);
  if (m_CurrentAudio.id >= 0)
    m_CurrentAudio.startsync = message->Acquire();

  if (m_CurrentVideo.id >= 0)
    m_CurrentVideo.startsync = message->Acquire();
/* TODO - we have to rewrite the sync class, to not require
          all other players waiting for subtitle, should only
          be the oposite way
  if (m_CurrentSubtitle.id >= 0)
    m_CurrentSubtitle.startsync = message->Acquire();
*/
  message->Release();
}

void CDVDPlayer::SendPlayerMessage(CDVDMsg* pMsg, unsigned int target)
{
  if(target == DVDPLAYER_AUDIO)
    m_dvdPlayerAudio.SendMessage(pMsg);
  if(target == DVDPLAYER_VIDEO)
    m_dvdPlayerVideo.SendMessage(pMsg);
  if(target == DVDPLAYER_SUBTITLE)
    m_dvdPlayerSubtitle.SendMessage(pMsg);
  if(target == DVDPLAYER_TELETEXT)
    m_dvdPlayerTeletext.SendMessage(pMsg);
}

void CDVDPlayer::OnExit()
{
  g_dvdPerformanceCounter.DisableMainPerformance();

  m_pInputStream = NULL;
  m_pDemuxer = NULL;

  m_bStop = true;
  // if we didn't stop playing, advance to the next item in xbmc's playlist
  if(m_PlayerOptions.identify == false)
  {
    if (m_bAbortRequest)
      m_callback.OnPlayBackStopped();
    else
      m_callback.OnPlayBackEnded();
  }

  // set event to inform openfile something went wrong in case openfile is still waiting for this event
  m_ready.Set();
}

void CDVDPlayer::HandleMessages()
{
  return;
}

void CDVDPlayer::SetCaching(ECacheState state)
{
  return;
}

void CDVDPlayer::SetPlaySpeed(int speed)
{
  m_messenger.Put(new CDVDMsgInt(CDVDMsg::PLAYER_SETSPEED, speed));
  m_dvdPlayerAudio.SetSpeed(speed);
  m_dvdPlayerVideo.SetSpeed(speed);
  SynchronizeDemuxer(100);
}

void CDVDPlayer::Pause()
{
  if(m_playSpeed != DVD_PLAYSPEED_PAUSE && m_caching == CACHESTATE_FULL)
  {
    SetCaching(CACHESTATE_DONE);
    return;
  }

  // return to normal speed if it was paused before, pause otherwise
  if (m_playSpeed == DVD_PLAYSPEED_PAUSE)
  {
    SetPlaySpeed(DVD_PLAYSPEED_NORMAL);
    m_callback.OnPlayBackResumed();
  }
  else
  {
    SetPlaySpeed(DVD_PLAYSPEED_PAUSE);
    m_callback.OnPlayBackPaused();
  }
}

bool CDVDPlayer::IsPaused() const
{
  return (m_playSpeed == DVD_PLAYSPEED_PAUSE) || m_caching == CACHESTATE_FULL;
}

bool CDVDPlayer::HasVideo() const
{
  return false;
}

bool CDVDPlayer::HasAudio() const
{
  return (m_CurrentAudio.id >= 0);
}

bool CDVDPlayer::IsPassthrough() const
{
  return m_dvdPlayerAudio.IsPassthrough();
}

bool CDVDPlayer::CanSeek()
{
  return GetTotalTime() > 0;
}

void CDVDPlayer::Seek(bool bPlus, bool bLargeStep)
{
#if 0
  // sadly this doesn't work for now, audio player must
  // drop packets at the same rate as we play frames
  if( m_playSpeed == DVD_PLAYSPEED_PAUSE && bPlus && !bLargeStep)
  {
    m_dvdPlayerVideo.StepFrame();
    return;
  }
#endif

  if(((bPlus && GetChapter() < GetChapterCount())
  || (!bPlus && GetChapter() > 1)) && bLargeStep)
  {
    if(bPlus)
      SeekChapter(GetChapter() + 1);
    else
      SeekChapter(GetChapter() - 1);
    return;
  }

  __int64 seek;
  if (g_advancedSettings.m_videoUseTimeSeeking && GetTotalTime() > 2*g_advancedSettings.m_videoTimeSeekForwardBig)
  {
    if (bLargeStep)
      seek = bPlus ? g_advancedSettings.m_videoTimeSeekForwardBig : g_advancedSettings.m_videoTimeSeekBackwardBig;
    else
      seek = bPlus ? g_advancedSettings.m_videoTimeSeekForward : g_advancedSettings.m_videoTimeSeekBackward;
    seek *= 1000;
    seek += GetTime();
  }
  else
  {
    float percent;
    if (bLargeStep)
      percent = bPlus ? g_advancedSettings.m_videoPercentSeekForwardBig : g_advancedSettings.m_videoPercentSeekBackwardBig;
    else
      percent = bPlus ? g_advancedSettings.m_videoPercentSeekForward : g_advancedSettings.m_videoPercentSeekBackward;
    seek = (__int64)(GetTotalTimeInMsec()*(GetPercentage()+percent)/100);
  }

  bool restore = true;
  if (m_Edl.HasCut())
  {
    /*
     * Alter the standard seek position based on whether any commercial breaks have been
     * automatically skipped.
     */
    const int clock = DVD_TIME_TO_MSEC(m_clock.GetClock());
    /*
     * If a large backwards seek occurs within 10 seconds of the end of the last automated
     * commercial skip, then seek back to the start of the commercial break under the assumption
     * it was flagged incorrectly. 10 seconds grace period is allowed in case the watcher has to
     * fumble around finding the remote. Only happens once per commercial break.
     *
     * Small skip does not trigger this in case the start of the commercial break was in fact fine
     * but it skipped too far into the program. In that case small skip backwards behaves as normal.
     */
    if (!bPlus && bLargeStep
    &&  m_EdlAutoSkipMarkers.seek_to_start
    &&  clock >= m_EdlAutoSkipMarkers.commbreak_end
    &&  clock <= m_EdlAutoSkipMarkers.commbreak_end + 10*1000) // Only if within 10 seconds of the end (in msec)
    {
      CLog::Log(LOGDEBUG, "%s - Seeking back to start of commercial break [%s - %s] as large backwards skip activated within 10 seconds of the automatic commercial skip (only done once per break).",
                __FUNCTION__, CEdl::MillisecondsToTimeString(m_EdlAutoSkipMarkers.commbreak_start).c_str(),
                CEdl::MillisecondsToTimeString(m_EdlAutoSkipMarkers.commbreak_end).c_str());
      seek = m_EdlAutoSkipMarkers.commbreak_start;
      restore = false;
      m_EdlAutoSkipMarkers.seek_to_start = false; // So this will only happen within the 10 second grace period once.
    }
    /*
     * If big skip forward within the last "reverted" commercial break, seek to the end of the
     * commercial break under the assumption that the break was incorrectly flagged and playback has
     * now reached the actual start of the commercial break. Assume that the end is flagged more
     * correctly than the landing point for a standard big skip (ends seem to be flagged more
     * accurately than the start).
     */
    else if (bPlus && bLargeStep
    &&       clock >= m_EdlAutoSkipMarkers.commbreak_start
    &&       clock <= m_EdlAutoSkipMarkers.commbreak_end)
    {
      CLog::Log(LOGDEBUG, "%s - Seeking to end of previously skipped commercial break [%s - %s] as big forwards skip activated within the break.",
                __FUNCTION__, CEdl::MillisecondsToTimeString(m_EdlAutoSkipMarkers.commbreak_start).c_str(),
                CEdl::MillisecondsToTimeString(m_EdlAutoSkipMarkers.commbreak_end).c_str());
      seek = m_EdlAutoSkipMarkers.commbreak_end;
      restore = false;
    }
  }

  __int64 time = GetTime();
  m_messenger.Put(new CDVDMsgPlayerSeek((int)seek, !bPlus, true, false, restore));
  SynchronizeDemuxer(100);
  if (seek < 0) seek = 0;
  m_callback.OnPlayBackSeek((int)seek, (int)(seek - time));
}

bool CDVDPlayer::SeekScene(bool bPlus)
{
  if (!m_Edl.HasSceneMarker())
    return false;

  /*
   * There is a 5 second grace period applied when seeking for scenes backwards. If there is no
   * grace period applied it is impossible to go backwards past a scene marker.
   */
  __int64 clock = GetTime();
  if (!bPlus && clock > 5 * 1000) // 5 seconds
    clock -= 5 * 1000;

  int64_t iScenemarker;
  if (m_Edl.GetNextSceneMarker(bPlus, clock, &iScenemarker))
  {
    /*
     * Seeking is flushed and inaccurate, just like Seek()
     */
    m_messenger.Put(new CDVDMsgPlayerSeek((int)iScenemarker, !bPlus, true, false, false));
    SynchronizeDemuxer(100);
    return true;
  }
  return false;
}

void CDVDPlayer::GetAudioInfo(CStdString& strAudioInfo)
{
}

void CDVDPlayer::GetVideoInfo(CStdString& strVideoInfo)
{
}

void CDVDPlayer::GetGeneralInfo(CStdString& strGeneralInfo)
{
}

void CDVDPlayer::SeekPercentage(float iPercent)
{
  __int64 iTotalTime = GetTotalTimeInMsec();

  if (!iTotalTime)
    return;

  SeekTime((__int64)(iTotalTime * iPercent / 100));
}

float CDVDPlayer::GetPercentage()
{
  __int64 iTotalTime = GetTotalTimeInMsec();

  if (!iTotalTime)
    return 0.0f;

  return GetTime() * 100 / (float)iTotalTime;
}

void CDVDPlayer::SetAVDelay(float fValue)
{
  m_dvdPlayerVideo.SetDelay( (fValue * DVD_TIME_BASE) ) ;
}

float CDVDPlayer::GetAVDelay()
{
  return m_dvdPlayerVideo.GetDelay() / (float)DVD_TIME_BASE;
}

void CDVDPlayer::SetSubTitleDelay(float fValue)
{
  m_dvdPlayerVideo.SetSubtitleDelay(-fValue * DVD_TIME_BASE);
}

float CDVDPlayer::GetSubTitleDelay()
{
  return -m_dvdPlayerVideo.GetSubtitleDelay() / DVD_TIME_BASE;
}

// priority: 1: libdvdnav, 2: external subtitles, 3: muxed subtitles
int CDVDPlayer::GetSubtitleCount()
{
  return 0; 
}

int CDVDPlayer::GetSubtitle()
{
  return 0;
}

void CDVDPlayer::GetSubtitleName(int iStream, CStdString &strStreamName)
{
  strStreamName += "(Invalid)";
}

void CDVDPlayer::SetSubtitle(int iStream)
{
}

bool CDVDPlayer::GetSubtitleVisible()
{
  return false;
}

void CDVDPlayer::SetSubtitleVisible(bool bVisible)
{
}

int CDVDPlayer::GetAudioStreamCount()
{
  return 0;
}

int CDVDPlayer::GetAudioStream()
{
  return 0; 
}

void CDVDPlayer::GetAudioStreamName(int iStream, CStdString& strStreamName)
{
  strStreamName = " (Invalid)";
}

void CDVDPlayer::SetAudioStream(int iStream)
{
}

TextCacheStruct_t* CDVDPlayer::GetTeletextCache()
{
  return 0;
}

void CDVDPlayer::LoadPage(int p, int sp, unsigned char* buffer)
{
  return;
}

void CDVDPlayer::SeekTime(__int64 iTime)
{
  int seekOffset = (int)(iTime - GetTime());
  m_messenger.Put(new CDVDMsgPlayerSeek((int)iTime, true, true, true));
  SynchronizeDemuxer(100);
  m_callback.OnPlayBackSeek((int)iTime, seekOffset);
}

// return the time in milliseconds
__int64 CDVDPlayer::GetTime()
{
  double offset = 0;
  return llrint(m_State.time + DVD_TIME_TO_MSEC(offset));
}

// return length in msec
__int64 CDVDPlayer::GetTotalTimeInMsec()
{
  return llrint(m_State.time_total);
}

// return length in seconds.. this should be changed to return in milleseconds throughout xbmc
int CDVDPlayer::GetTotalTime()
{
  return (int)(GetTotalTimeInMsec() / 1000);
}

void CDVDPlayer::ToFFRW(int iSpeed)
{
  // can't rewind in menu as seeking isn't possible
  // forward is fine
  if (iSpeed < 0 && IsInMenu()) return;
  SetPlaySpeed(iSpeed * DVD_PLAYSPEED_NORMAL);
}

bool CDVDPlayer::OpenAudioStream(int iStream, int source)
{
  return false;
}

bool CDVDPlayer::OpenVideoStream(int iStream, int source)
{
  return false;
}

bool CDVDPlayer::OpenSubtitleStream(int iStream, int source)
{
  return false;
}

bool CDVDPlayer::OpenTeletextStream(int iStream, int source)
{
  return false;
}

bool CDVDPlayer::CloseAudioStream(bool bWaitForBuffers)
{
  return true;
}

bool CDVDPlayer::CloseVideoStream(bool bWaitForBuffers)
{
  return true;
}

bool CDVDPlayer::CloseSubtitleStream(bool bKeepOverlays)
{
  return true;
}

bool CDVDPlayer::CloseTeletextStream(bool bWaitForBuffers)
{
  return true;
}

void CDVDPlayer::FlushBuffers(bool queued, double pts, bool accurate)
{
  double startpts;
  if(accurate)
    startpts = pts;
  else
    startpts = DVD_NOPTS_VALUE;

  m_CurrentAudio.inited      = false;
  m_CurrentAudio.dts         = DVD_NOPTS_VALUE;
  m_CurrentAudio.startpts    = startpts;

  m_CurrentVideo.inited      = false;
  m_CurrentVideo.dts         = DVD_NOPTS_VALUE;
  m_CurrentVideo.startpts    = startpts;

  m_CurrentSubtitle.inited   = false;
  m_CurrentSubtitle.dts      = DVD_NOPTS_VALUE;
  m_CurrentSubtitle.startpts = startpts;

  m_CurrentTeletext.inited   = false;
  m_CurrentTeletext.dts      = DVD_NOPTS_VALUE;
  m_CurrentTeletext.startpts = startpts;

  if(queued)
  {
    m_dvdPlayerAudio.SendMessage(new CDVDMsg(CDVDMsg::GENERAL_RESET));
    m_dvdPlayerVideo.SendMessage(new CDVDMsg(CDVDMsg::GENERAL_RESET));
    m_dvdPlayerVideo.SendMessage(new CDVDMsg(CDVDMsg::VIDEO_NOSKIP));
    m_dvdPlayerSubtitle.SendMessage(new CDVDMsg(CDVDMsg::GENERAL_RESET));
    m_dvdPlayerTeletext.SendMessage(new CDVDMsg(CDVDMsg::GENERAL_RESET));
    SynchronizePlayers(SYNCSOURCE_ALL);
  }
  else
  {
    m_dvdPlayerAudio.Flush();
    m_dvdPlayerVideo.Flush();
    m_dvdPlayerSubtitle.Flush();
    m_dvdPlayerTeletext.Flush();

    // clear subtitle and menu overlays
    m_overlayContainer.Clear();

    if(m_playSpeed == DVD_PLAYSPEED_NORMAL
    || m_playSpeed == DVD_PLAYSPEED_PAUSE)
    {
      // make sure players are properly flushed, should put them in stalled state
      CDVDMsgGeneralSynchronize* msg = new CDVDMsgGeneralSynchronize(1000, 0);
      m_dvdPlayerAudio.m_messageQueue.Put(msg->Acquire(), 1);
      m_dvdPlayerVideo.m_messageQueue.Put(msg->Acquire(), 1);
      msg->Wait(&m_bStop, 0);
      msg->Release();

      // purge any pending PLAYER_STARTED messages
      m_messenger.Flush(CDVDMsg::PLAYER_STARTED);

      // we should now wait for init cache
      SetCaching(CACHESTATE_INIT);
      m_CurrentAudio.started    = false;
      m_CurrentVideo.started    = false;
      m_CurrentSubtitle.started = false;
      m_CurrentTeletext.started = false;
    }

    if(pts != DVD_NOPTS_VALUE)
      m_clock.Discontinuity(CLOCK_DISC_NORMAL, pts, 0);
    UpdatePlayState(0);
  }
}

// since we call ffmpeg functions to decode, this is being called in the same thread as ::Process() is
int CDVDPlayer::OnDVDNavResult(void* pData, int iMessage)
{
  return 0;
}

bool CDVDPlayer::OnAction(const CAction &action)
{
  // return false to inform the caller we didn't handle the message
  return false;
}

bool CDVDPlayer::IsInMenu() const
{
  return false;
}

bool CDVDPlayer::HasMenu()
{
  return false;
}

bool CDVDPlayer::GetCurrentSubtitle(CStdString& strSubtitle)
{
  return false;
}

CStdString CDVDPlayer::GetPlayerState()
{
  return m_State.player_state;
}

bool CDVDPlayer::SetPlayerState(CStdString state)
{
  m_messenger.Put(new CDVDMsgPlayerSetState(state));
  return true;
}

int CDVDPlayer::GetChapterCount()
{
  return m_State.chapter_count;
}

int CDVDPlayer::GetChapter()
{
  return m_State.chapter;
}

void CDVDPlayer::GetChapterName(CStdString& strChapterName)
{
  strChapterName = m_State.chapter_name;
}

int CDVDPlayer::SeekChapter(int iChapter)
{
  return 0;
}

int CDVDPlayer::AddSubtitle(const CStdString& strSubPath)
{
  return 0; 
}

int CDVDPlayer::GetCacheLevel() const
{
  int a = m_dvdPlayerAudio.m_messageQueue.GetLevel();
  int v = m_dvdPlayerVideo.m_messageQueue.GetLevel();
  return max(a, v);
}

int CDVDPlayer::GetAudioBitrate()
{
  return m_dvdPlayerAudio.GetAudioBitrate();
}

int CDVDPlayer::GetVideoBitrate()
{
  return m_dvdPlayerVideo.GetVideoBitrate();
}

int CDVDPlayer::GetSourceBitrate()
{
  return 0;
}

void CDVDPlayer::UpdatePlayState(double timeout)
{
  m_State.timestamp = CDVDClock::GetAbsoluteClock();
}

void CDVDPlayer::UpdateApplication(double timeout)
{
  m_UpdateApplication = CDVDClock::GetAbsoluteClock();
}

bool CDVDPlayer::CanRecord()
{
  return false;
}

bool CDVDPlayer::IsRecording()
{
  return false; 
}

bool CDVDPlayer::Record(bool bOnOff)
{
  return false;
}

int CDVDPlayer::GetChannels()
{
  return -1;
}

CStdString CDVDPlayer::GetAudioCodecName()
{
  CStdString retVal;
  return retVal;
}

CStdString CDVDPlayer::GetVideoCodecName()
{
  CStdString retVal;
  return retVal;
}

int CDVDPlayer::GetPictureWidth()
{
  return 0;
}

int CDVDPlayer::GetPictureHeight()
{
  return 0;
}

bool CDVDPlayer::GetStreamDetails(CStreamDetails &details)
{
  return false;
}

CStdString CDVDPlayer::GetPlayingTitle()
{
  return "";
}
