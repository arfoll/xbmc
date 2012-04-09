/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *      Initial code sponsored by: Voddler Inc (voddler.com)
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
#include "cores/VideoRenderers/RenderManager.h"
#include "Application.h"
#include "windowing/WindowingFactory.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#if defined(HAS_GL) || defined(HAS_GLES)
#include "OverlayRendererGL.h"
#elif defined(HAS_DX)
#include "OverlayRendererDX.h"
#endif


using namespace OVERLAY;


COverlay::COverlay()
{
  m_x      = 0.0f;
  m_y      = 0.0f;
  m_width  = 0.0;
  m_height = 0.0;
  m_type   = TYPE_NONE;
  m_align  = ALIGN_SCREEN;
  m_pos    = POSITION_RELATIVE;
  m_references = 1;
}

COverlay::~COverlay()
{
}

COverlay* COverlay::Acquire()
{
  return this;
}

long COverlay::Release()
{
  return 0;
}

long COverlayMainThread::Release()
{
  return 0;
}


CRenderer::CRenderer()
{
  m_render = 0;
  m_decode = (m_render + 1) % 2;
}

CRenderer::~CRenderer()
{
  for(int i = 0; i < 2; i++)
    Release(m_buffers[i]);
}

void CRenderer::AddOverlay(COverlay* o, double pts)
{
  CSingleLock lock(m_section);

  SElement   e;
  e.pts = pts;
  e.overlay = o->Acquire();
  m_buffers[m_decode].push_back(e);
}

void CRenderer::AddCleanup(COverlay* o)
{
  CSingleLock lock(m_section);
  m_cleanup.push_back(o->Acquire());
}

void CRenderer::Release(SElementV& list)
{
  SElementV l = list;
  list.clear();

  for(SElementV::iterator it = l.begin(); it != l.end(); it++)
  {
    if(it->overlay)
      it->overlay->Release();
  }
}

void CRenderer::Release(COverlayV& list)
{
  COverlayV l = list;
  list.clear();

  for(COverlayV::iterator it = l.begin(); it != l.end(); it++)
    (*it)->Release();
}

void CRenderer::Flush()
{
  CSingleLock lock(m_section);

  for(int i = 0; i < 2; i++)
    Release(m_buffers[i]);

  Release(m_cleanup);
}

void CRenderer::Flip()
{
  CSingleLock lock(m_section);

  m_render = m_decode;
  m_decode =(m_decode + 1) % 2;

  Release(m_buffers[m_decode]);
}

void CRenderer::Render()
{
  CSingleLock lock(m_section);

  Release(m_cleanup);

  SElementV& list = m_buffers[m_render];
  for(SElementV::iterator it = list.begin(); it != list.end(); it++)
  {
    COverlay*& o = it->overlay;

    if(!o)
      continue;

    Render(o);
  }
}

void CRenderer::Render(COverlay* o)
{
  CRect rs, rd, rv;
  RESOLUTION_INFO res;
  g_renderManager.GetVideoRect(rs, rd);
  rv  = g_graphicsContext.GetViewWindow();
  res = g_settings.m_ResInfo[g_renderManager.GetResolution()];

  SRenderState state;
  state.x       = o->m_x;
  state.y       = o->m_y;
  state.width   = o->m_width;
  state.height  = o->m_height;

  COverlay::EPosition pos   = o->m_pos;
  COverlay::EAlign    align = o->m_align;

  if(pos == COverlay::POSITION_RELATIVE)
  {
    float scale_x = 1.0;
    float scale_y = 1.0;

    if(align == COverlay::ALIGN_SCREEN
    || align == COverlay::ALIGN_SUBTITLE)
    {
      scale_x = (float)res.iWidth;
      scale_y = (float)res.iHeight;
    }

    if(align == COverlay::ALIGN_VIDEO)
    {
      scale_x = rs.Width();
      scale_y = rs.Height();
    }

    state.x      *= scale_x;
    state.y      *= scale_y;
    state.width  *= scale_x;
    state.height *= scale_y;

    pos = COverlay::POSITION_ABSOLUTE;
  }

  if(pos == COverlay::POSITION_ABSOLUTE)
  {
    if(align == COverlay::ALIGN_SCREEN
    || align == COverlay::ALIGN_SUBTITLE)
    {
      float scale_x = rv.Width() / res.iWidth;
      float scale_y = rv.Height()  / res.iHeight;

      state.x      *= scale_x;
      state.y      *= scale_y;
      state.width  *= scale_x;
      state.height *= scale_y;

      if(align == COverlay::ALIGN_SUBTITLE)
      {
        state.x += rv.x1 + rv.Width() * 0.5f;
        state.y += rv.y1  + (res.iSubtitles - res.Overscan.top) * scale_y;
      }
      else
      {
        state.x += rv.x1;
        state.y += rv.y1;
      }
    }

    if(align == COverlay::ALIGN_VIDEO)
    {
      float scale_x = rd.Width() / rs.Width();
      float scale_y = rd.Height() / rs.Height();

      state.x      -= rs.x1;
      state.y      -= rs.y1;

      state.x      *= scale_x;
      state.y      *= scale_y;
      state.width  *= scale_x;
      state.height *= scale_y;

      state.x      += rd.x1;
      state.y      += rd.y1;
    }

  }

  o->Render(state);
}

