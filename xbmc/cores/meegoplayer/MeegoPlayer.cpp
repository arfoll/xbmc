/*
 *      Copyright (C) 2011 Brendan Le Foll - brendan@fridu.net
 *      Based on ExternalPlayer by Team XBMC
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

#include "signal.h"
#include "limits.h"
#include "SingleLock.h"
#include "AudioContext.h"
#include "MeegoPlayer.h"
#include "WindowingFactory.h"
#include "GUIDialogOK.h"
#include "GUIFontManager.h"
#include "GUITextLayout.h"
#include "GUIWindowManager.h"
#include "Application.h"
#include "FileSystem/FileMusicDatabase.h"
#include "Settings.h"
#include "FileItem.h"
#include "RegExp.h"
#include "StringUtils.h"
#include "URL.h"
#include "XMLUtils.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "GUIDialogBusy.h"
#include "GUIUserMessages.h"
#include <SDL/SDL_syswm.h>

// Default time after which the item's playcount is incremented
#define DEFAULT_PLAYCOUNT_MIN_TIME 10
// The player bus name
#define UPLAYER_BUS_NAME "uk.co.madeo.uplayer"
// The player bus path
#define UPLAYER_BUS_PATH "/uk/co/madeo/uplayer"
// DBUS reply timeout
#define DBUS_REPLY_TIMEOUT -1

using namespace XFILE;

CMeegoPlayer::CMeegoPlayer(IPlayerCallback& callback)
    : IPlayer(callback),
      CThread()
{
  m_bAbortRequest = false;
  m_bIsPlaying = false;
  m_paused = false;
  m_playbackStartTime = 0;
  m_speed = 1;
  m_old_speed = 1;
  // just for test
  m_totalTime = 10000;
  m_time = 0;
  m_waitTime = 0;
  m_pauseTime = 0;
  m_volume = 1.0f;
  m_volumeu = false;

  // by default this is a video player
  m_pinkvideo = true;
  m_pinkmusic = false;
  m_playCountMinTime = DEFAULT_PLAYCOUNT_MIN_TIME;
  m_playOneStackItem = false;

  m_dialog = NULL;
  m_isFullscreen = true;

  if (!InitializeDbus()) {
      CLog::Log(LOGNOTICE, "Meego dbus player: DBUS initalisation failed");
  }
}

CMeegoPlayer::~CMeegoPlayer()
{
  CloseFile();
  StopThread();
}

bool CMeegoPlayer::OpenFile(const CFileItem& file, const CPlayerOptions &options)
{
  try
  {
    m_bIsPlaying = true;
    m_launchFilename = file.m_strPath;
    CLog::Log(LOGNOTICE, "%s: %s", __FUNCTION__, m_launchFilename.c_str());
    Create();

    m_isFullscreen = options.fullscreen;

    return true;
  }
  catch(...)
  {
    m_bIsPlaying = false;
    CLog::Log(LOGERROR,"%s - Exception thrown", __FUNCTION__);
    return false;
  }
}

bool CMeegoPlayer::CloseFile()
{
  m_bAbortRequest = true;

  if (m_dialog && m_dialog->IsActive()) m_dialog->Close();

  // dbus stop playback
  callDbusMethod("stop", "", 0);
  // make sure we know we are not playing
  m_bIsPlaying = false;
  m_paused = false;
  CLog::Log(LOGNOTICE, "Meego dbus player: Playback stopped");

  return true;
}

bool CMeegoPlayer::IsPlaying() const
{
  return m_bIsPlaying;
}

void CMeegoPlayer::Process()
{
  SetName("CMeegoPlayer");

  CStdString mainFile = m_launchFilename;
  CStdString archiveContent = "";
  // unwind names
  CURL url(m_launchFilename);
  CStdString protocol = url.GetProtocol();

  // this stuff is overkill now but since it's still all parsed, might come in handy
  CLog::Log(LOGNOTICE, "%s: File   : %s", __FUNCTION__, mainFile.c_str());
  CLog::Log(LOGNOTICE, "%s: Start", __FUNCTION__);

  int iActiveDevice = g_audioContext.GetActiveDevice();
  if (iActiveDevice != CAudioContext::NONE)
  {
    CLog::Log(LOGNOTICE, "%s: Releasing audio device %d", __FUNCTION__, iActiveDevice);
    g_audioContext.SetActiveDevice(CAudioContext::NONE);
  }

  m_playbackStartTime = CTimeUtils::GetTimeMS();
  m_waitTime = m_playbackStartTime;

  if (protocol == "udp" || protocol == "rtsp") {
    /* Understood protocol going to meegoplayer as is */
    dbusURI = mainFile;
  }
  else if (protocol == "musicdb") {
    mainFile = CFileMusicDatabase::TranslateUrl(url);
    dbusURI = "file://";
    dbusURI.append(mainFile.c_str());
  }
  else if (protocol == "http") {
    /* http useragent detected */
    dbusURI = mainFile;
    size_t found;
    found = dbusURI.find("|");
    if ((int) found != (-1)) {
      /* remove the user agent */
      dbusURI.resize ((int) found);
    }
  }
  else {
    dbusURI = "file://";
    dbusURI.append(mainFile.c_str());
  }

  CLog::Log(LOGNOTICE, "Meego dbus player: URI sent to dbus player is : %s", dbusURI.c_str());
  callDbusMethod("set_uri", dbusURI, 0);
  callDbusMethod("play", "", 0);

  // wait until we receive a stop message from the player
  bool eos = waitOnDbus();

  // we close playback
  CLog::Log(LOGNOTICE, "Meego dbus player: Stopped playback");

  m_bIsPlaying = false;
  m_paused = false;

  // We don't want to come back to an active screensaver
  g_application.ResetScreenSaver();
  g_application.WakeUpScreenSaverAndDPMS();

  if (iActiveDevice != CAudioContext::NONE) {
    CLog::Log(LOGNOTICE, "%s: Reclaiming audio device %d", __FUNCTION__, iActiveDevice);
    g_audioContext.SetActiveDevice(iActiveDevice);
  }

  if (!eos) {
    m_callback.OnPlayBackStopped();
  }
  else {
    m_callback.OnPlayBackEnded();
  }

}

void CMeegoPlayer::Pause()
{
  if (m_paused == true) {
    m_paused = false;
    callDbusMethod("play", "", 0);
    m_callback.OnPlayBackResumed();
  } else {
    m_paused = true;
    callDbusMethod("pause", "", 0);
    m_callback.OnPlayBackPaused();
  }
}

bool CMeegoPlayer::IsPaused() const
{
  return m_paused;
}

bool CMeegoPlayer::HasVideo() const
{
  if (m_pinkvideo) {
    return true;
  }
  return false;
}

bool CMeegoPlayer::HasAudio() const
{
  if (m_pinkmusic) {
    return false;
  }
  return true;
}

void CMeegoPlayer::SwitchToNextLanguage()
{
}

void CMeegoPlayer::ToggleSubtitles()
{
}

bool CMeegoPlayer::CanSeek()
{
  return true;
}

void CMeegoPlayer::Seek(bool bPlus, bool bLargeStep)
{
}

void CMeegoPlayer::GetAudioInfo(CStdString& strAudioInfo)
{
  strAudioInfo = "CMeegoPlayer:GetAudioInfo";
}

void CMeegoPlayer::GetVideoInfo(CStdString& strVideoInfo)
{
  strVideoInfo = "CMeegoPlayer:GetVideoInfo";
}

void CMeegoPlayer::GetGeneralInfo(CStdString& strGeneralInfo)
{
  strGeneralInfo = "CMeegoPlayer:GetGeneralInfo";
}

void CMeegoPlayer::SwitchToNextAudioLanguage()
{
}

void CMeegoPlayer::SeekPercentage(float iPercent)
{
}

float CMeegoPlayer::GetPercentage()
{
  __int64 iTime = GetTime();
  __int64 iTotalTime = GetTotalTime() * 1000;

  if (iTotalTime != 0)
  {
//    CLog::Log(LOGDEBUG, "Percentage is %f", (iTime * 100 / (float)iTotalTime));
    return iTime * 100 / (float)iTotalTime;
  }

  return 0.0f;
}

void CMeegoPlayer::SetAVDelay(float fValue)
{
}

float CMeegoPlayer::GetAVDelay()
{
  return 0.0f;
}

void CMeegoPlayer::SetSubTitleDelay(float fValue)
{
}

float CMeegoPlayer::GetSubTitleDelay()
{
  return 0.0;
}

void CMeegoPlayer::SeekTime(__int64 iTime)
{
}

__int64 CMeegoPlayer::GetTime() // in millis
{
  if (!m_paused) {
    m_time = (CTimeUtils::GetTimeMS() - m_waitTime) - m_pauseTime;
  } else {
    m_pauseTime = (CTimeUtils::GetTimeMS() - m_waitTime) - m_time;
  }
  return m_time;
}

int CMeegoPlayer::GetTotalTime() // in seconds
{
  return m_totalTime;
}

void CMeegoPlayer::ToFFRW(int iSpeed)
{
  m_speed = iSpeed;
}

void CMeegoPlayer::ShowOSD(bool bOnoff)
{
}

CStdString CMeegoPlayer::GetPlayerState()
{
  return "";
}

bool CMeegoPlayer::SetPlayerState(CStdString state)
{
  return true;
}

void CMeegoPlayer::SetVolume(long volume)
{
  if (volume == -6000) {
    /* We are muted */
    m_volume = 0;
  } else {
    m_volume = (double) volume / -10000;
    /* Convert what XBMC gives into 0.0 -> 1.0 scale playbin2 uses */
    m_volume = ((1 - m_volume) - 0.4) * 1.6666;
  }

  /* update the volume later */
  m_volumeu = true;
}

/**
  This entire function is a little redundant.
  We should move to a Paplayer structure instead of a ExternalPlayer structure
*/
bool CMeegoPlayer::Initialize(TiXmlElement* pConfig)
{
  /* Legacy configuration */
  XMLUtils::GetBoolean(pConfig, "playonestackitem", m_playOneStackItem);
  XMLUtils::GetInt(pConfig, "playcountminimumtime", m_playCountMinTime, 1, INT_MAX);
  /* New config specific to meegoplayer - pinkvideo is the default */
  XMLUtils::GetBoolean(pConfig, "pinkmusic", m_pinkmusic);
  XMLUtils::GetBoolean(pConfig, "pinkvideo", m_pinkvideo);

  CLog::Log(LOGNOTICE, "Meego dbus player : pinkmusic (%s), pinkvideo (%s)",
            m_pinkmusic ? "true" : "false",
            m_pinkvideo ? "true" : "false");

  return true;
}

bool CMeegoPlayer::InitializeDbus()
{
  /* Initialise the error bus */
  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);

  if (dbus_error_is_set(&error)) {
     CLog::Log(LOGERROR,"Meego dbus player: Connection Error (%s)", error.message);
     dbus_error_free(&error);
     return false;
  }

  return true;
}

bool CMeegoPlayer::waitOnDbus()
{
  DBusMessage* message;

  if (NULL == connection) {
     CLog::Log(LOGERROR,"Meego dbus player: Connection is NULL - %s", error.message);
     return true;
  }
  /* add a rule for which messages we want to see */
  dbus_bus_add_match(connection, "type='signal',interface='"UPLAYER_BUS_NAME"'", &error);
  dbus_connection_flush(connection);
  if (dbus_error_is_set(&error)) {
    CLog::Log(LOGERROR,"Meego dbus player: General error on dbus");
    return true;
  }

  bool eos = true;
  
  /* stay stuck here until dbus says it's ok or m_bIsPlaying is false */
  while (m_bIsPlaying) {
    /* non blocking read of the next available message */
    dbus_connection_read_write(connection, 0);
    message = dbus_connection_pop_message(connection);

    /* loop again if we haven't read a message */
    if (message == NULL) {
      if (m_speed != m_old_speed) {
          CLog::Log(LOGDEBUG,"Meego dbus player: m_speed is : %d", m_speed);
          callDbusMethod ("set_rate", "", m_speed);
          m_old_speed = m_speed;
          m_time = callDbusMethod ("get_position", "", 0);
      } else if (m_volumeu) {
          Sleep(100);
          m_volumeu = false;
          callDbusMethod ("set_volume", "", m_volume);
      }
      Sleep(100);
    }
    else {
      CLog::Log(LOGDEBUG,"Meego dbus player: Received signal from dbus");
      if (dbus_message_is_signal (message, UPLAYER_BUS_NAME, "emitEOSSignal")) {
        /* we are EOF */
        m_bIsPlaying = false;
        CLog::Log(LOGNOTICE,"Meego dbus player: EOF received");
      } else if (dbus_message_is_signal (message, UPLAYER_BUS_NAME, "emitNewURI")) {
        CLog::Log(LOGDEBUG,"Meego dbus player: New URI signal received");
        Sleep(100);
        /* get the duration of the file */
        m_totalTime = callDbusMethod ("get_duration", "", 0);
      } else {
        CLog::Log(LOGNOTICE,"Meego dbus player: Signal received but not recognised");
      }
      dbus_message_unref(message);
    }
  }
  return eos;
}

int CMeegoPlayer::callDbusMethod(CStdString method, CStdString value, dbus_int32_t speed)
{
  DBusMessage *message;
  DBusMessage *reply;
  dbus_int32_t code = 0;

  if (connection == NULL) {
    CLog::Log(LOGDEBUG,"Failed to open connection to dbus. Check permissions: %s", error.message);
    goto end;
  }

  if (dbus_error_is_set(&error)) {
    CLog::Log(LOGERROR,"Meego dbus player: General error on dbus");
    if (!InitializeDbus()) {
      CLog::Log(LOGNOTICE, "Meego dbus player: DBUS initalisation failed");
    }
  }

  message = dbus_message_new_method_call (UPLAYER_BUS_NAME,
                                          UPLAYER_BUS_PATH,
                                          UPLAYER_BUS_NAME,
                                          method.c_str());
  if (method.compare("set_rate") == 0) {
    dbus_message_append_args (message, DBUS_TYPE_INT32, &speed, DBUS_TYPE_INVALID);
    CLog::Log(LOGDEBUG, "Meego dbus player: set_rate %d message will be sent", speed);
  } if (method.compare("set_volume") == 0) {
    dbus_message_append_args (message, DBUS_TYPE_DOUBLE, &m_volume, DBUS_TYPE_INVALID);
    CLog::Log(LOGDEBUG, "Meego dbus player: set_volume %f message will be sent", m_volume);
  } if (method.compare("set_uri") == 0) {
    const char *valueC = value.c_str();
    dbus_message_append_args (message, DBUS_TYPE_STRING, &valueC,
        DBUS_TYPE_INVALID);
    CLog::Log(LOGDEBUG, "Meego dbus player: set_uri %s message will be sent", valueC);
  } else {
    dbus_message_append_args (message, DBUS_TYPE_INVALID);
    CLog::Log(LOGDEBUG, "Meego dbus player: %s message will be sent", method.c_str());
  }

  /* Call method */
  reply = dbus_connection_send_with_reply_and_block (connection, message, DBUS_REPLY_TIMEOUT, &error);

  if (reply != NULL) {
    DBusMessageIter args;
    int type;
    
    if (dbus_message_iter_init(message, &args)) {
      /* message is not empty */
      while (dbus_message_iter_has_next(&args)) {
        type = dbus_message_iter_get_arg_type (&args);
        if (type == DBUS_TYPE_INT32) {
          dbus_message_iter_get_basic(&args, &code);
          CLog::Log(LOGDEBUG, "Meego dbus player: code is %d", code);
        }
      }
    }

    dbus_message_unref (reply);
    dbus_message_unref (message);
  } else {
    /* This is pretty fatal */
    printf("Fatal : Meego dbus service looks like it's not running \n");
    CLog::Log(LOGDEBUG, "Meego dbus player: Check the meego dbus player is running");
  }

  if (dbus_error_is_set (&error)) {
    CLog::Log(LOGDEBUG,"Meego dbus player: General Dbus error: %s\n", error.message);
  }

  end:
    return code;
}
