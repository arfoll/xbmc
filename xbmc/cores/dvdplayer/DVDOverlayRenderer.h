#pragma once

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

#ifdef HAS_VIDEO_PLAYBACK
#include "cores/VideoRenderers/RenderManager.h"
#endif

class CDVDOverlayImage;
class CDVDOverlaySSA;

typedef struct stDVDPictureRenderer
{
  BYTE* data[4];
  int stride[4];

  int width;
  int height;
}
DVDPictureRenderer;

class CDVDOverlayRenderer
{
public:
  static void Render(DVDPictureRenderer* pPicture, char* pOverlay, double pts);
  static void Render(DVDPictureRenderer* pPicture, CDVDOverlayImage* pOverlay);
  static void Render(DVDPictureRenderer* pPicture, CDVDOverlaySSA *pOverlay, double pts);

private:

  static void Render_SPU_YUV(DVDPictureRenderer* pPicture, char* pOverlaySpu, bool bCrop);
};
