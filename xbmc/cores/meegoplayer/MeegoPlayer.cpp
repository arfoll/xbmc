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

#include "system.h"
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
#include <dbus/dbus.h>
#include "GUIDialogBusy.h"
#include "GUIUserMessages.h"
#include <SDL/SDL_syswm.h>

// If the process ends in less than this time (ms), we assume it's a launcher
// and wait for manual intervention before continuing
#define LAUNCHER_PROCESS_TIME 2000
// Time (ms) we give a process we sent a WM_QUIT to close before terminating
#define PROCESS_GRACE_TIME 3000
// Default time after which the item's playcount is incremented
#define DEFAULT_PLAYCOUNT_MIN_TIME 10
// The player bus name
#define UPLAYER_BUS_NAME = "uk.co.madeo.uplayer"
// The player bus path
#define UPLAYER_BUS_PATH = "/uk/co/madeo/uplayer"

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
  m_trickPlay = false;
  // just for test
  m_totalTime = 10000;
  m_time = 0;
  m_waitTime = 0;
  m_pauseTime = 0;

  // by default this is a video player
  m_pinkvideo = true;
  m_pinkmusic = false;
  m_playCountMinTime = DEFAULT_PLAYCOUNT_MIN_TIME;
  m_playOneStackItem = false;

  m_dialog = NULL;
  m_isFullscreen = true;

}

CMeegoPlayer::~CMeegoPlayer()
{
  CloseFile();
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
  callDbusMethod("Stop", "");
  // make sure we know we are not playing
  m_bIsPlaying = false;
  m_paused = false;
  CLog::Log(LOGNOTICE, "Playback stopped");

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
  //CLog::Log(LOGNOTICE, "MeegoPlayer : Playback Start time = %d", m_playbackStartTime);
  CStdString dbusURI;

  if (protocol == "udp" || protocol == "rtsp") {
    /* Understood protocol going to meegoplayer as is */
    dbusURI = mainFile;
  }
  else if (protocol != "http") {
    /* local media */
    dbusURI = "file://";
    dbusURI.append(mainFile.c_str());
  }
  else {
    /* http useragent detected */
    dbusURI = mainFile;
    size_t found;
    found = dbusURI.find("?|");
    if ((int) found != (-1)) {
      /* remove the user agent */
      dbusURI.resize ((int) found);
    }
  }

  // fix for flash player to clean the screen properly - resets screen A and B
  system("echo '' | /opt/gdl_samples/cfgplane UPP_B -r");
  system("echo 'GDL_PLANE_VID_MISMATCH_POLICY GDL_VID_POLICY_RESIZE' | /opt/gdl_samples/cfgplane UPP_A");

  CLog::Log(LOGNOTICE, "MeegoPlayer : URI sent to dbus player is : %s",dbusURI.c_str());
  callDbusMethod("set_uri", dbusURI);
  callDbusMethod("play", "");

  // wait until we receive a stop message from the player
  waitOnDbus();

  // we close playback
  CLog::Log(LOGNOTICE, "Stopped playback");

  m_bIsPlaying = false;
  m_paused = false;

  // We don't want to come back to an active screensaver
  g_application.ResetScreenSaver();
  g_application.WakeUpScreenSaverAndDPMS();

  if (iActiveDevice != CAudioContext::NONE)
  {
    CLog::Log(LOGNOTICE, "%s: Reclaiming audio device %d", __FUNCTION__, iActiveDevice);
    g_audioContext.SetActiveDevice(iActiveDevice);
  }

#if 0
  if (!ret || (m_playOneStackItem && g_application.CurrentFileItem().IsStack()))
    m_callback.OnPlayBackStopped();
  else
#endif
    m_callback.OnPlayBackEnded();

  /* Make sure UPP_C is fully visible */
  system("echo 'GDL_PLANE_ALPHA_GLOBAL 255' | /opt/gdl_samples/cfgplane  UPP_C");
}

void CMeegoPlayer::Pause()
{
  if (m_paused == true) {
    m_paused = false;
    callDbusMethod("Pause", "");
    m_callback.OnPlayBackResumed();
  } else {
    m_paused = true;
    callDbusMethod("Pause", "");
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

  CLog::Log(LOGNOTICE, "MeegoPlayer : pinkmusic (%s), pinkvideo (%s)",
            m_pinkmusic ? "true" : "false",
            m_pinkvideo ? "true" : "false");

  return true;
}

void CMeegoPlayer::waitOnDbus()
{
  DBusMessage* msg;
  DBusMessageIter args;
  DBusConnection* conn;
  DBusError err;
  dbus_int32_t sigvalueInt;

  /* initialise the errors */
  dbus_error_init(&err);

  /* connect to the bus and check for errors */
  conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
  if (dbus_error_is_set(&err)) {
     CLog::Log(LOGERROR,"Meego dbus player: Connection Error (%s)", err.message);
     dbus_error_free(&err);
  }
  if (NULL == conn) {
     CLog::Log(LOGERROR,"Meego dbus player: Connection is NULL - %s", err.message);
     return;
  }
  /* add a rule for which messages we want to see */
  dbus_bus_add_match(conn, "type='signal',interface='uk.co.madeo.uplayer'", &err);
  dbus_connection_flush(conn);
  if (dbus_error_is_set(&err)) {
     CLog::Log(LOGERROR,"Meego dbus player: General error on dbus");
     return;
  }

  /* stay stuck here until dbus says it's ok or m_bIsPlaying is false */
  while (m_bIsPlaying) {
      /* non blocking read of the next available message */
      dbus_connection_read_write(conn, 0);
      msg = dbus_connection_pop_message(conn);

      /* loop again if we haven't read a message */
      if (NULL == msg) {
         /* Are we in trickplay? */
         CLog::Log(LOGDEBUG,"Meego dbus player: m_speed is : %d", m_speed);
         if (m_speed != 1) {
           /* length to char* for dbus API */
           int length = m_speed*5;
           char lengthS[5];
           snprintf(lengthS, 5, "%d", length);
           /* Emulate timing when trickplaying */
           if (length > 1) {
             m_waitTime = m_waitTime - length*1000;
           } else {
             m_waitTime = m_waitTime + length*1000;
           }
           setDbusProperty ("player-jump" , lengthS);
           sleep (0.8);
         } else {
            sleep(1);
         }

         /* we don't have a message */
         continue;
      }

      CLog::Log(LOGDEBUG,"Meego dbus player: Received signal from dbus");
      /* read args */
      if (!dbus_message_iter_init(msg, &args))
		CLog::Log(LOGDEBUG,"Meego dbus player: Message is empty");
      else if(dbus_message_has_interface(msg, "uk.co.madeo.uplayer")) {
		CLog::Log(LOGDEBUG,"Meego dbus player: Message is from correct interface");
		int type;
		/* grab value type */
		type = dbus_message_iter_get_arg_type (&args);
		/* check for string */
		if (type == 115)
		  CLog::Log(LOGDEBUG,"Meego dbus player: Received unkown signal containing a string");
		/* check for int */
		else if (type == 105) {
		  int i;
		  for (i = 0; i < 2; i++) {
			type = dbus_message_iter_get_arg_type (&args);
			/* make sure second arg it's a number! */
			if (type == 105) {
			  /* grab value */
			  dbus_message_iter_get_basic(&args, &sigvalueInt);
			  if (sigvalueInt == 906) {
				/* we are EOF */
				m_bIsPlaying = false;
				CLog::Log(LOGNOTICE,"Meego dbus player: We are EOF");
			  }
			  if (dbus_message_iter_has_next(&args)) {
				/* get the next value */
				dbus_message_iter_next(&args);
			  } else {
				/* not very important - we'd just do the check twice */
				break;
			}
		  }
		}
      }
      dbus_message_unref(msg);
    }
  }
}

CStdString CMeegoPlayer::getProperty(CStdString property)
{
  DBusConnection *connection;
  DBusError error;
  DBusMessageIter args;
  DBusMessage *message;
  DBusMessage *reply;
  CStdString output;

  /* Initialise the error bus */
  dbus_error_init (&error);

  int reply_timeout;
  connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
  if (connection == NULL)
  {
    CLog::Log(LOGDEBUG,"Failed to open connection to dbus. Check permissions: %s", error.message);
    //dbus_error_free (error);
  }

  /* We always use handle 1 */
  dbus_int32_t v_INT32 = 1;

  /* Grab system property from parameters */
  const char *propertyC = property.c_str();

  message = dbus_message_new_method_call ("uk.co.madeo.uplayer",       /*service*/
                                          "/uk/co/madeo/uplayer",     /*path*/
                                          "uk.co.madeo.uplayer",      /*interface*/
                                          "Getproperty");
  dbus_message_append_args (message,
                            DBUS_TYPE_INT32, &v_INT32,
                            DBUS_TYPE_STRING, &propertyC,
                            DBUS_TYPE_INVALID);

  /* Call method */
  reply_timeout = -1;   /*don't timeout*/
  reply = dbus_connection_send_with_reply_and_block (connection, message, reply_timeout, &error);

  CLog::Log(LOGDEBUG,"Meego dbus player: Received signal from dbus");
  if (!dbus_message_iter_init(message, &args))
    CLog::Log(LOGDEBUG,"Meego dbus player: Message is empty");
  else if(dbus_message_has_interface(message, "uk.co.madeo.uplayer")) {
    CLog::Log(LOGDEBUG,"Meego dbus player: Message is from correct interface");

    int type;
    int i;
    while (dbus_message_iter_has_next(&args)) {
      type = dbus_message_iter_get_arg_type (&args);
      // make sure second arg is a string!
      if (type == DBUS_TYPE_STRING) {
        char * str;
        dbus_message_iter_get_basic(&args, &str);
        printf("Meego dbus player: iterator returned %s", str);
        output = CStdString(str);
      } else {
         CLog::Log(LOGDEBUG,"Meego dbus player: type was %d, expected %d, i %d", type, DBUS_TYPE_STRING, i);
      }
    }
    dbus_message_unref(message);
  }
  CLog::Log(LOGDEBUG,"Meego dbus player: Value returned = %s", output.c_str());
  return output;
}

/**
 * call setDbusProperty - calls a dbus method to set a value
 *
 * @property - property to set on dbus
 * @value - char* argument to dbus method
 */
int CMeegoPlayer::setDbusProperty(char *property, char *value)
{
  DBusConnection *connection;
  DBusError error;
  DBusMessage *message;
  DBusMessage *reply;

  /* Initialise the error bus */
  dbus_error_init (&error);

  int reply_timeout;
  connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
  if (connection == NULL)
  {
    fprintf(stderr,"Failed to open connection to dbus. Check permissions\n");
    return 1;
  }
  /* We always use handle 1 */
  dbus_int32_t v_INT32 = 1;

  /* With more than 3 message types this needs to be rewritten */
  if (property == "player-jump") {
    message = dbus_message_new_method_call ("uk.co.madeo.uplayer",
                                            "/uk/co/madeo/uplayer",
                                            "uk.co.madeo.uplayer",
                                            "Setproperty");
    dbus_message_append_args (message,
                              DBUS_TYPE_INT32, &v_INT32,
                              DBUS_TYPE_STRING, &property,
                              DBUS_TYPE_STRING, &value,
                              DBUS_TYPE_INVALID);
  } else {
    fprintf(stderr, "Method unknown\n");
    return 2;
  }
  /* Call method */
  reply_timeout = -1;   /* 2s timeout */
  reply = dbus_connection_send_with_reply_and_block \
      (connection, message, reply_timeout, &error);

  if (reply != NULL) {
    dbus_message_unref (reply);
    dbus_message_unref (message);
  } else {
    fprintf(stderr, "dbus service looks like it's not running \n");
    return 3;
  }

  if (dbus_error_is_set (&error)) {
    fprintf (stderr,"General Dbus error: %s\n", error.message);
    return 1;
  }

  return 0;
}

void CMeegoPlayer::callDbusMethod(CStdString method, CStdString value)
{
  DBusConnection *connection;
  DBusError error;
  DBusMessage *message;
  DBusMessage *reply;

  /* Initialise the error bus */
  dbus_error_init (&error);

  int reply_timeout;
  connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
  if (connection == NULL)
  {
    CLog::Log(LOGDEBUG,"Failed to open connection to dbus. Check permissions: %s", error.message);
  }

  if (method.compare("set_uri") == 0) {
      const char *valueC = value.c_str();
      message = dbus_message_new_method_call ("uk.co.madeo.uplayer",
                                              "/uk/co/madeo/player",
                                              "uk.co.madeo.uplayer",
                                              "set_uri");
      dbus_message_append_args (message, DBUS_TYPE_STRING, &valueC,
                DBUS_TYPE_INVALID);
      CLog::Log(LOGDEBUG, "Meego dbus player: Play %s message will be sent", valueC);
  } else if (method.compare("play") == 0) {
      message = dbus_message_new_method_call ("uk.co.madeo.uplayer",
                                              "/uk/co/madeo/player",
                                              "uk.co.madeo.uplayer",
                                              "play");
      dbus_message_append_args (message, DBUS_TYPE_INVALID);
      CLog::Log(LOGDEBUG, "Meego dbus player: Play message will be sent");
  } else if (method.compare("stop") == 0) {
      message = dbus_message_new_method_call ("uk.co.madeo.uplayer",
                                              "/uk/co/madeo/player",
                                              "uk.co.madeo.uplayer",
                                              "stop");
      dbus_message_append_args (message, DBUS_TYPE_INVALID);
      CLog::Log(LOGDEBUG, "Meego dbus player: Stop message will be sent");
  } else if (method.compare("Pause") == 0) {
      message = dbus_message_new_method_call ("uk.co.madeo.uplayer",
                                              "/uk/co/madeo/player",
                                              "uk.co.madeo.uplayer",
                                              "pause");
      dbus_message_append_args (message, DBUS_TYPE_INVALID);
      CLog::Log(LOGDEBUG, "Meego dbus player: Pause message will be sent");
  } else {
      CLog::Log(LOGDEBUG,"Unkown DBUS message: %s\n", method.c_str());
      goto end;
  }

  /* Call method */
  reply_timeout = -1;   /* .5s timeout */
  reply = dbus_connection_send_with_reply_and_block (connection, message, reply_timeout, &error);

  if (reply != NULL) {
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
    return;
}
