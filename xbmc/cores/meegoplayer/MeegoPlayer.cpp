/*
 *      Copyright (C) 2011, 2012 Brendan Le Foll - brendan@fridu.net
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

#include "threads/SystemClock.h"
#include "system.h"
#include "signal.h"
#include "limits.h"
#include "threads/SingleLock.h"
#include "guilib/AudioContext.h"
#include "MeegoPlayer.h"
#include "windowing/WindowingFactory.h"
#include "dialogs/GUIDialogOK.h"
#include "guilib/GUIFontManager.h"
#include "guilib/GUITextLayout.h"
#include "guilib/GUIWindowManager.h"
#include "Application.h"
#include "filesystem/FileMusicDatabase.h"
#include "settings/Settings.h"
#include "FileItem.h"
#include "RegExp.h"
#include "StringUtils.h"
#include "URL.h"
#include "utils/XMLUtils.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "dialogs/GUIDialogBusy.h"
#include "GUIUserMessages.h"
#include <SDL/SDL_syswm.h>

// Default time after which the item's playcount is incremented
#define DEFAULT_PLAYCOUNT_MIN_TIME 10
// DBUS reply timeout
#define DBUS_REPLY_TIMEOUT -1
// UMMS Dbus Stuff
#define UMMS_SERVICE_NAME "com.UMMS"
#define UMMS_OBJECT_MANAGER_OBJECT_PATH "/com/UMMS/ObjectManager"
#define UMMS_OBJECT_MANAGER_INTERFACE_NAME "com.UMMS.ObjectManager.iface"
#define UMMS_MEDIA_PLAYER_INTERFACE_NAME "com.UMMS.MediaPlayer"

using namespace XFILE;

CMeegoPlayer::CMeegoPlayer(IPlayerCallback& callback)
    : IPlayer(callback),
      CThread("CMeegoPlayer")
{
  m_bAbortRequest = false;
  m_bIsPlaying = false;
  m_paused = false;
  m_playbackStartTime = 0;
  m_speed = 1;
  m_old_speed = 1;
  m_totalTime = 0;
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
  m_playerName = NULL;

  if (!InitializeDbus()) {
      CLog::Log(LOGNOTICE, "MeeGo dbus player: DBUS initalisation failed");
  }

  if (!RequestPlayer(TRUE)) {
      CLog::Log(LOGNOTICE, "MeeGo dbus player: Request player failed");
  }
}

CMeegoPlayer::~CMeegoPlayer()
{
  CloseFile();
  RequestPlayer(FALSE);
  StopThread();
}

bool CMeegoPlayer::OpenFile(const CFileItem& file, const CPlayerOptions &options)
{
  try
  {
    m_bIsPlaying = true;
    m_launchFilename = file.GetPath();
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
  callDbusMethod("Stop", "", 0);
  // make sure we know we are not playing
  m_bIsPlaying = false;
  m_paused = false;
  CLog::Log(LOGNOTICE, "MeeGo dbus player: Playback stopped");

  return true;
}

bool CMeegoPlayer::IsPlaying() const
{
  return m_bIsPlaying;
}

void CMeegoPlayer::Process()
{
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

  m_playbackStartTime = XbmcThreads::SystemClockMillis();
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

  CLog::Log(LOGNOTICE, "MeeGo dbus player: URI sent to dbus player is : %s", dbusURI.c_str());
  int returnCode = callDbusMethod("SetUri", dbusURI, 0);
  if (returnCode == -1) {
    CGUIDialogOK::ShowAndGetInput(257, 853, 0, 0);
    m_callback.OnPlayBackStopped();
    return;
  }
  callDbusMethod("Play", "", 0);

  // wait until we receive a stop message from the player
  bool eos = waitOnDbus();

  // we close playback
  CLog::Log(LOGNOTICE, "MeeGo dbus player: Stopped playback");

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
    callDbusMethod("Play", "", 0);
    m_callback.OnPlayBackResumed();
  } else {
    m_paused = true;
    callDbusMethod("Pause", "", 0);
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
    return true;
  }
  return false;
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

  if (iTotalTime != 0) {
    return iTime * 100 / (float) iTotalTime;
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
    m_time = (XbmcThreads::SystemClockMillis() - m_waitTime) - m_pauseTime;
  } else {
    m_pauseTime = (XbmcThreads::SystemClockMillis() - m_waitTime) - m_time;
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
    //m_volume = (double) volume / -10000;
    /* Convert what XBMC gives into 0.0 -> 1.0 scale playbin2 uses */
    //m_volume = ((1 - m_volume) - 0.4) * 1.6666;

    m_volume = volume;
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
  //  XMLUtils::GetBoolean(pConfig, "playonestackitem", m_playOneStackItem);
  //  XMLUtils::GetInt(pConfig, "playcountminimumtime", m_playCountMinTime, 1, INT_MAX);
  /* New config specific to meegoplayer - pinkvideo is the default */
  //  XMLUtils::GetBoolean(pConfig, "pinkmusic", m_pinkmusic);
  //  XMLUtils::GetBoolean(pConfig, "pinkvideo", m_pinkvideo);

  CLog::Log(LOGNOTICE, "MeeGo dbus player : pinkmusic (%s), pinkvideo (%s)",
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
     CLog::Log(LOGERROR,"MeeGo dbus player: Connection Error (%s)", error.message);
     dbus_error_free(&error);
     return false;
  }

  return true;
}

bool CMeegoPlayer::RequestPlayer(bool attend)
{
    DBusMessage *message;
    DBusMessage *reply;
    char * playerName = NULL;

    if (connection == NULL) {
        CLog::Log(LOGDEBUG,"MeeGo dbus player: Failed to open connection to dbus. Check permissions: %s", error.message);
        return false;
    }

    if (dbus_error_is_set(&error)) {
        CLog::Log(LOGERROR,"MeeGo dbus player: General error on dbus");
        if (!InitializeDbus()) {
            CLog::Log(LOGNOTICE, "MeeGo dbus player: DBUS initalisation failed");
        }
    }

    if(attend) {
        /* Now init the Object and get the player. */
        message = dbus_message_new_method_call (UMMS_SERVICE_NAME,
                UMMS_OBJECT_MANAGER_OBJECT_PATH,
                UMMS_OBJECT_MANAGER_INTERFACE_NAME,
                "RequestMediaPlayer");

        CLog::Log(LOGERROR,"MeeGo dbus player: Before UMMS init");

        dbus_error_free(&error);
        reply = dbus_connection_send_with_reply_and_block (connection, message, DBUS_REPLY_TIMEOUT, &error);

        if (reply != NULL) {
            DBusMessageIter args;
            int type;

            if (dbus_message_iter_init(reply, &args)) {
                /* message is not empty */
                do {
                    type = dbus_message_iter_get_arg_type (&args);
                    if (type == DBUS_TYPE_STRING) {
                        dbus_message_iter_get_basic(&args, &playerName);
                        CLog::Log(LOGDEBUG, "MeeGo dbus player: Get the UMMS player name:%s", playerName);

                        if(m_playerName)
                            free(m_playerName);

                        m_playerName = (char *)malloc((strlen(playerName)+1)*sizeof(char));
                        memcpy(m_playerName, playerName, strlen(playerName));
                        m_playerName[strlen(playerName)] = 0;
                        break;
                    }
                } while (dbus_message_iter_has_next(&args));

            }

            dbus_message_unref (reply);
            dbus_message_unref (message);
        } else {
            /* This is pretty fatal */
            CLog::Log(LOGDEBUG, "MeeGo dbus player: Check the meego dbus player is running");
        }

        if(m_playerName == NULL) {
            CLog::Log(LOGDEBUG, "MeeGo dbus player: No UMMS player, Check the meego dbus player is running");
            return false;
        }

        return true;
    } else {
        if(m_playerName == NULL) {
            CLog::Log(LOGDEBUG, "MeeGo dbus player: No UMMS player, Check the meego dbus player is running");
            return false;
        }

        /* Now init the Object and get the player. */
        message = dbus_message_new_method_call (UMMS_SERVICE_NAME,
                UMMS_OBJECT_MANAGER_OBJECT_PATH,
                UMMS_OBJECT_MANAGER_INTERFACE_NAME,
                "RemoveMediaPlayer");

        dbus_message_append_args (message, DBUS_TYPE_STRING, &m_playerName, DBUS_TYPE_INVALID);
        reply = dbus_connection_send_with_reply_and_block (connection, message, DBUS_REPLY_TIMEOUT, &error);
        if (dbus_error_is_set(&error) || reply == NULL) {
            /* This is pretty fatal */
            fprintf(stderr, "Fatal : Meego dbus service looks like it's not running \n");
            CLog::Log(LOGDEBUG, "MeeGo dbus player: Check the meego dbus player is running");
        }
        dbus_error_free(&error);

        m_playerName = NULL;

        free(m_playerName);
        if(reply)
            dbus_message_unref (reply);
        dbus_message_unref (message);
        return reply != NULL;
    }
}

bool CMeegoPlayer::waitOnDbus()
{
    DBusMessage* message;
    int last_duration = 0;

    if (NULL == connection) {
        CLog::Log(LOGERROR,"MeeGo dbus player: Connection is NULL - %s", error.message);
        return true;
    }

    if (dbus_error_is_set(&error)) {
        CLog::Log(LOGERROR,"MeeGo dbus player: General error on dbus");
        return true;
    }

    /* add a rule for which messages we want to see */
    dbus_bus_add_match(connection, "type='signal',interface='"UMMS_MEDIA_PLAYER_INTERFACE_NAME"'", &error);
    dbus_connection_flush(connection);

    bool eos = false;
    callDbusMethod("GetMediaSizeTime", "", 0); 

    /* stay stuck here until dbus says it's ok or m_bIsPlaying is false */
    while (m_bIsPlaying) {
        /* non blocking read of the next available message */
        dbus_connection_read_write(connection, 0);
        message = dbus_connection_pop_message(connection);

        /* loop again if we haven't read a message */
        if (message == NULL) {
            if (m_speed != m_old_speed) {
                CLog::Log(LOGDEBUG,"MeeGo dbus player: m_speed is : %d", m_speed);
                callDbusMethod ("SetPlaybackRate", "", m_speed);
                m_old_speed = m_speed;
                m_time = callDbusMethod ("GetPosition", "", 0);
            } else if (m_volumeu) {
                Sleep(100);
                m_volumeu = false;
                callDbusMethod ("SetVolume", "", m_volume);
            }
            Sleep(100);

            /* Do some duration query logic here. */
            last_duration++;
            if(!(last_duration % 50)) {
                callDbusMethod("GetMediaSizeTime", "", 0);
            }
        }
        else {
            CLog::Log(LOGDEBUG,"MeeGo dbus player: Received signal from dbus");      
            if (dbus_message_is_signal (message, UMMS_MEDIA_PLAYER_INTERFACE_NAME, "Eof")) {
                /* we are EOF */
                m_bIsPlaying = false;
                eos = true;
                CLog::Log(LOGNOTICE,"MeeGo dbus player: EOF received");
            } else if (dbus_message_is_signal (message, UMMS_MEDIA_PLAYER_INTERFACE_NAME, "TargetReady")) {
                CLog::Log(LOGDEBUG,"MeeGo dbus player: New URI signal received");
            } else if (dbus_message_is_signal (message, UMMS_MEDIA_PLAYER_INTERFACE_NAME, "PlayerStateChanged")) {
                CLog::Log(LOGDEBUG,"MeeGo dbus player: Playing signal received");
            } else if (dbus_message_is_signal (message, UMMS_MEDIA_PLAYER_INTERFACE_NAME, "Error")) {        
                CLog::Log(LOGDEBUG,"MeeGo dbus player: Error signal received");
                m_bIsPlaying = false;
                CGUIDialogOK::ShowAndGetInput(257, 854, 0, 0);
            } else if (dbus_message_is_signal (message, UMMS_MEDIA_PLAYER_INTERFACE_NAME, "NeedReply")) {
                CLog::Log(LOGDEBUG,"MeeGo dbus player: NeedReply received");
                callDbusMethod("Reply", "", 0);
            } else {
                CLog::Log(LOGNOTICE,"MeeGo dbus player: Signal received but not recognised");
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
    int result = 0;

    CLog::Log(LOGDEBUG,"MeeGo dbus player: Enter the dbus call: %s", method.c_str());
    //printf("------------ Enter the dbus call: %s\n", method.c_str());

    if (connection == NULL) {
        CLog::Log(LOGDEBUG,"MeeGo dbus player: Failed to open connection to dbus. Check permissions: %s", error.message);
        goto end;
    }

    if (m_playerName == NULL) {
        CLog::Log(LOGDEBUG,"MeeGo dbus player: Not request a player");
        goto end;
    }

    if (dbus_error_is_set(&error)) {
        CLog::Log(LOGERROR,"MeeGo dbus player: General error on dbus");
        if (!InitializeDbus()) {
            CLog::Log(LOGNOTICE, "MeeGo dbus player: DBUS initalisation failed");
        }
    }

    CLog::Log(LOGNOTICE, "MeeGo dbus player: dbus path is %s", m_playerName);
    message = dbus_message_new_method_call (UMMS_SERVICE_NAME,
            m_playerName,
            UMMS_MEDIA_PLAYER_INTERFACE_NAME,
            method.c_str());
    if (method.compare("SetPlaybackRate") == 0) {
        double d_speed = (double)speed;
        dbus_message_append_args (message, DBUS_TYPE_DOUBLE, &d_speed, DBUS_TYPE_INVALID);
        CLog::Log(LOGDEBUG, "MeeGo dbus player: set_rate %d message will be sent", speed);
    } if (method.compare("SetVolume") == 0) {
        dbus_message_append_args (message, DBUS_TYPE_INT32, &m_volume, DBUS_TYPE_INVALID);
        CLog::Log(LOGDEBUG, "MeeGo dbus player: set_volume %d message will be sent", m_volume);
    } if (method.compare("SetUri") == 0) {
        const char *valueC = value.c_str();
        dbus_message_append_args (message, DBUS_TYPE_STRING, &valueC,
                DBUS_TYPE_INVALID);
        CLog::Log(LOGDEBUG, "MeeGo dbus player: SetUri:%s message will be sent", valueC);
    } else {
        dbus_message_append_args (message, DBUS_TYPE_INVALID);
        CLog::Log(LOGDEBUG, "MeeGo dbus player: %s message will be sent", method.c_str());
    }

    /* Call method */
    dbus_error_free(&error);
    reply = dbus_connection_send_with_reply_and_block (connection, message, DBUS_REPLY_TIMEOUT, &error);

    if (reply != NULL) {
        DBusMessageIter args;
        int type;

        if(method.compare("GetMediaSizeTime") == 0) {

            if (dbus_message_iter_init(reply, &args)) {
                /* message is not empty */
                int64_t l_time = -1;

                type = dbus_message_iter_get_arg_type (&args);
                if (type == DBUS_TYPE_INT64) {
                    dbus_message_iter_get_basic(&args, &l_time);
                    CLog::Log(LOGDEBUG, "Total time is %li", l_time);
                    m_totalTime = (int)(l_time/1000);
                }
            }
        }

        dbus_message_unref (reply);
        dbus_message_unref (message);
        result = 0;
    } else {
        /* This is pretty fatal */
        printf("@@@line:%d   Fatal : Meego dbus service looks like it's not running \n", __LINE__);
        CLog::Log(LOGDEBUG, "MeeGo dbus player: Check the meego dbus player is running");
        result = -1;
    }

    if (dbus_error_is_set (&error)) {
        printf("MeeGo dbus player: General Dbus error: %s\n", error.message);
        CLog::Log(LOGDEBUG,"MeeGo dbus player: General Dbus error: %s\n", error.message);
    }

end:
    return result;
}
