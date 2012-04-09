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
#include "OverlayRenderer.h"
#include "OverlayRendererUtil.h"
#include "Application.h"
#include "windowing/WindowingFactory.h"

namespace OVERLAY {

static uint32_t build_rgba(int a, int r, int g, int b, bool mergealpha)
{
  if(mergealpha)
    return      a        << PIXEL_ASHIFT
         | (r * a / 255) << PIXEL_RSHIFT
         | (g * a / 255) << PIXEL_GSHIFT
         | (b * a / 255) << PIXEL_BSHIFT;
  else
    return a << PIXEL_ASHIFT
         | r << PIXEL_RSHIFT
         | g << PIXEL_GSHIFT
         | b << PIXEL_BSHIFT;
}

#define clamp(x) (x) > 255.0 ? 255 : ((x) < 0.0 ? 0 : (int)(x+0.5f))
static uint32_t build_rgba(int yuv[3], int alpha, bool mergealpha)
{
  int    a = alpha + ( (alpha << 4) & 0xff );
  double r = 1.164 * (yuv[0] - 16)                          + 1.596 * (yuv[2] - 128);
  double g = 1.164 * (yuv[0] - 16) - 0.391 * (yuv[1] - 128) - 0.813 * (yuv[2] - 128);
  double b = 1.164 * (yuv[0] - 16) + 2.018 * (yuv[1] - 128);
  return build_rgba(a, clamp(r), clamp(g), clamp(b), mergealpha);
}
#undef clamp

}
