#!/bin/bash

#
# This script is made to clean an svn checkout of XBMC, the dir is given as $1
# This has been made for dharma beta4, other versions should work just as easily
# Brendan Le Foll 2010 - brendan@fridu.net
#

if [ -d "$1" ]; then
  # move to current directory
  cd $1
  echo "removing all .svn directories"
  find . -name .svn -print0 | xargs -0 rm -rf 
  echo "removing xcodeproj for windows"
  rm -rf XBMC.xcodeproj project
  echo "removing tools"
  rm -rf tools/arm tools/Fake\ Episode\ Maker tools/HardwareConfigure
  rm -rf tools/Mach5 tools/MingwBuildEnvironment tools/osx
  rm -rf tools/PackageMaker tools/Translator tools/UpdateThumbs.py 
  rm -rf tools/win32buildtools tools/XBMCTex tools/XprPack
  echo "removing doxygen doc"
  rm -rf doxygen_ressources
  echo "cleaning unused libraries"
  rm -rf lib/bzip2 lib/enca lib/fribidi lib/libass lib/libcdio lib/libcrystalhd lib/libcurl-OSX lib/libiconv
  rm -rf lib/liblame lib/libmicrohttpd* lib/libmodplug lib/libmysql_win32 lib/libSDL-OSX lib/librtmp
  rm -rf lib/libSDL-OSX lib/libssh_win32 lib/libvpx lib/pcre 
  echo "cleaning more unused libs"
  rm -rf xbmc/lib/boost
  echo "removing legally dodgy files"
  rm -rf system/players/dvdplayer/*.dll system/players/paplayer/*.so
  rm -rf system/cdrip/lame_enc-x86-osx.so
  rm -rf lib/libmodplug xbmc/lib/UnrarXLib xbmc/lib/libXBMS xbmc/lib/libmms
  echo "removing dodgy + unused codec/player code"
  rm -rf xbmc/cores/dvdplayer/Codecs xbmc/cores/dvdplayer/DVDCodecs xbmc/cores/dvdplayer/DVDDemuxers xbmc/cores/dvdplayer/DVDInputStreams xbmc/cores/dvdplayer/DVDSubtitles
  rm -rf xbmc/cores/paplayer/AC3CDDACodec.cpp xbmc/cores/paplayer/AC3CDDACodec.h xbmc/cores/paplayer/AC3Codec.cpp xbmc/cores/paplayer/AC3Codec.h xbmc/cores/paplayer/AC3Codec/
  rm -rf xbmc/cores/paplayer/ADPCMCodec.cpp xbmc/cores/paplayer/ADPCMCodec.h xbmc/cores/paplayer/ADPCMCodec/
  rm -rf xbmc/cores/paplayer/AIFFcodec.cpp xbmc/cores/paplayer/AIFFcodec.h xbmc/cores/paplayer/ASAPCodec.cpp xbmc/cores/paplayer/ASAPCodec.h xbmc/cores/paplayer/CDDAcodec.cpp xbmc/cores/paplayer/CDDAcodec.h 
  rm -rf xbmc/cores/paplayer/DTSCDDACodec.cpp xbmc/cores/paplayer/DTSCDDACodec.h xbmc/cores/paplayer/DTSCodec.cpp xbmc/cores/paplayer/DTSCodec.hxbmc/cores/paplayer/DVDPlayerCodec.cpp xbmc/cores/paplayer/DVDPlayerCodec.h xbmc/cores/paplayer/DllASAP.h xbmc/cores/paplayer/DllAc3codec.h xbmc/cores/paplayer/DllAdpcm.h xbmc/cores/paplayer/DllDCACodec.h xbmc/cores/paplayer/DllLibFlac.h xbmc/cores/paplayer/DllModplug.h xbmc/cores/paplayer/DllNosefart.h xbmc/cores/paplayer/DllSidplay2.h xbmc/cores/paplayer/DllStSound.h xbmc/cores/paplayer/DllTimidity.h xbmc/cores/paplayer/DllVGMStream.h xbmc/cores/paplayer/DllVorbisfile.h xbmc/cores/paplayer/DllWAVPack.h xbmc/cores/paplayer/DllWMA.h xbmc/cores/paplayer/FLACCodec
  rm -rf xbmc/cores/paplayer/FLACcodec.cpp xbmc/cores/paplayer/FLACcodec.h xbmc/cores/paplayer/MP3codec.cpp xbmc/cores/paplayer/MP3codec.h xbmc/cores/paplayer/ModplugCodec.cpp xbmc/cores/paplayer/ModplugCodec.h xbmc/cores/paplayer/NSFCodec.cpp xbmc/cores/paplayer/NSFCodec.h xbmc/cores/paplayer/NSFCodec xbmc/cores/paplayer/OGGcodec.cpp xbmc/cores/paplayer/OGGcodec.h xbmc/cores/paplayer/SIDCodec.cpp xbmc/cores/paplayer/SIDCodec.h xbmc/cores/paplayer/SIDCodec xbmc/cores/paplayer/SPCCodec.cpp xbmc/cores/paplayer/SPCCodec.h xbmc/cores/paplayer/SPCCodec xbmc/cores/paplayer/TimidityCodec.cpp xbmc/cores/paplayer/TimidityCodec.h xbmc/cores/paplayer/VGMCodec.cpp xbmc/cores/paplayer/VGMCodec.h xbmc/cores/paplayer/WAVPackcodec.cpp xbmc/cores/paplayer/WAVPackcodec.h xbmc/cores/paplayer/WAVcodec.cpp xbmc/cores/paplayer/WAVcodec.h xbmc/cores/paplayer/WavPackCodec xbmc/cores/paplayer/YMCodec.cpp xbmc/cores/paplayer/YMCodec.h xbmc/cores/paplayer/YMCodec xbmc/cores/paplayer/asap xbmc/cores/paplayer/ogg xbmc/cores/paplayer/spc xbmc/cores/paplayer/timidity xbmc/cores/paplayer/vgmstream xbmc/cores/paplayer/vorbisfile
  echo "Removing GPLv3 files"
  rm -rf tools/XBMCLive/ tools/Linux/packaging/ xbmc/lib/libhts/ tools/EventClients/ xbmc/lib/libhdhomerun
  echo "finished!"
else
  echo "No paramaeter or parameter is not a valid directory"
  exit 1
fi
