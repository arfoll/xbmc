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
#include "OverlayRendererUtil.h"
#include "OverlayRendererGL.h"
#ifdef HAS_GL
#include "LinuxRendererGL.h"
#elif HAS_GLES == 2
#include "LinuxRendererGLES.h"
#include "guilib/MatrixGLES.h"
#endif
#include "RenderManager.h"
#include "windowing/WindowingFactory.h"
#include "settings/Settings.h"
#include "utils/MathUtils.h"
#include "utils/log.h"
#include "utils/GLUtils.h"
#include "RenderManager.h"

#if defined(HAS_GL) || HAS_GLES == 2

#if HAS_GLES == 2
// GLES2.0 cant do CLAMP, but can do CLAMP_TO_EDGE.
#define GL_CLAMP	GL_CLAMP_TO_EDGE
#endif

#define USE_PREMULTIPLIED_ALPHA 1

using namespace OVERLAY;

static void LoadTexture(GLenum target
                      , GLsizei width, GLsizei height, GLsizei stride
                      , GLfloat* u, GLfloat* v
                      , GLenum internalFormat, GLenum externalFormat, const GLvoid* pixels)
{
  int width2  = NP2(width);
  int height2 = NP2(height);
  char *pixelVector = NULL;
  const GLvoid *pixelData = pixels;

  int bytesPerPixel;
  switch (externalFormat)
  {
#ifndef HAS_GLES
  case GL_BGRA:
#endif
  case GL_RGBA:
    bytesPerPixel = 4;
    break;
#ifndef HAS_GLES
  case GL_BGR:
#endif
  case GL_RGB:
    bytesPerPixel = 3;
    break;
  default:
    bytesPerPixel = 1;
  }

#ifdef HAS_GLES
  /** OpenGL ES does not support strided texture input. Make a copy without stride **/
  if (stride != width)
  {
    int bytesPerLine = bytesPerPixel * width;

    pixelVector = (char *)malloc(bytesPerLine * height);

    const char *src = (const char*)pixels;
    char *dst = pixelVector;
    for (int y = 0;y < height;++y)
    {
      memcpy(dst, src, bytesPerLine);
      src += stride;
      dst += bytesPerLine;
    }

    pixelData = pixelVector;
    stride = width;
  }
#else
  glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / bytesPerPixel);
#endif

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glTexImage2D   (target, 0, internalFormat
                , width2, height2, 0
                , externalFormat, GL_UNSIGNED_BYTE, NULL);

  glTexSubImage2D(target, 0
                , 0, 0, width, height
                , externalFormat, GL_UNSIGNED_BYTE
                , pixelData);

  if(height < height2)
    glTexSubImage2D( target, 0
                   , 0, height, width, 1
                   , externalFormat, GL_UNSIGNED_BYTE
                   , (unsigned char*)pixelData + stride * (height-1));

  if(width  < width2)
    glTexSubImage2D( target, 0
                   , width, 0, 1, height
                   , externalFormat, GL_UNSIGNED_BYTE
                   , (unsigned char*)pixelData + bytesPerPixel * (width-1));

#ifndef HAS_GLES
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif

  free(pixelVector);
  
  *u = (GLfloat)width  / width2;
  *v = (GLfloat)height / height2;
}

COverlayGlyphGL::~COverlayGlyphGL()
{
  glDeleteTextures(1, &m_texture);
  free(m_vertex);
}

void COverlayGlyphGL::Render(SRenderState& state)
{
  if (m_texture == 0)
    return;

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);

  glBindTexture(GL_TEXTURE_2D, m_texture);
  glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
#ifdef HAS_GL
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
  glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
  glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
  glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);

  glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
  glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE0);
  glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
  glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
  glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glTranslatef(state.x    , state.y     , 0.0f);
  glScalef    (state.width, state.height, 1.0f);

  VerifyGLState();

  glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);

  glColorPointer   (4, GL_UNSIGNED_BYTE, sizeof(VERTEX), (char*)m_vertex + offsetof(VERTEX, r));
  glVertexPointer  (3, GL_FLOAT        , sizeof(VERTEX), (char*)m_vertex + offsetof(VERTEX, x));
  glTexCoordPointer(2, GL_FLOAT        , sizeof(VERTEX), (char*)m_vertex + offsetof(VERTEX, u));
  glEnableClientState(GL_COLOR_ARRAY);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glDrawArrays(GL_QUADS, 0, m_count * 4);
  glPopClientAttrib();

  glPopMatrix();
#else
  g_matrices.MatrixMode(MM_MODELVIEW);
  g_matrices.PushMatrix();
  g_matrices.Translatef(state.x, state.y, 0.0f);
  g_matrices.Scalef(state.width, state.height, 1.0f);
  VerifyGLState();

  g_Windowing.EnableGUIShader(SM_FONTS);

  GLint posLoc  = g_Windowing.GUIShaderGetPos();
  GLint colLoc  = g_Windowing.GUIShaderGetCol();
  GLint tex0Loc = g_Windowing.GUIShaderGetCoord0();

  glVertexAttribPointer(posLoc,  3, GL_FLOAT,         GL_FALSE, sizeof(VERTEX), (char*)m_vertex + offsetof(VERTEX, x));
  glVertexAttribPointer(colLoc,  4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(VERTEX), (char*)m_vertex + offsetof(VERTEX, r));
  glVertexAttribPointer(tex0Loc, 2, GL_FLOAT,         GL_FALSE, sizeof(VERTEX), (char*)m_vertex + offsetof(VERTEX, u));

  glEnableVertexAttribArray(posLoc);
  glEnableVertexAttribArray(colLoc);
  glEnableVertexAttribArray(tex0Loc);

  // GLES2 version
  // As using triangle strips, have to do in sets of 4.
  // This is due to limitations of ES, in that tex/col has to be same size as ver!
  for (int i=0; i<(m_count*4); i+=4)
  {
    glDrawArrays(GL_TRIANGLE_STRIP, i, 4);
  }

  glDisableVertexAttribArray(posLoc);
  glDisableVertexAttribArray(colLoc);
  glDisableVertexAttribArray(tex0Loc);

  g_Windowing.DisableGUIShader();

  g_matrices.PopMatrix();
#endif

  glDisable(GL_BLEND);
  glDisable(GL_TEXTURE_2D);

  glBindTexture(GL_TEXTURE_2D, 0);
}


COverlayTextureGL::~COverlayTextureGL()
{
  glDeleteTextures(1, &m_texture);
}

void COverlayTextureGL::Render(SRenderState& state)
{
  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);

  glBindTexture(GL_TEXTURE_2D, m_texture);
#if USE_PREMULTIPLIED_ALPHA
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
#else
  glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
#endif

#if defined(HAS_GL)
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  VerifyGLState();
#endif

  DRAWRECT rd;
  if(m_pos == POSITION_RELATIVE)
  {
    rd.top     = state.y - state.height * 0.5;
    rd.bottom  = state.y + state.height * 0.5;
    rd.left    = state.x - state.width  * 0.5;
    rd.right   = state.x + state.width  * 0.5;
  }
  else
  {
    rd.top     = state.y;
    rd.bottom  = state.y + state.height;
    rd.left    = state.x;
    rd.right   = state.x + state.width;
  }

#if defined(HAS_GL)
  glBegin(GL_QUADS);
  glTexCoord2f(0.0f, 0.0f);
  glVertex2f(rd.left , rd.top);

  glTexCoord2f(m_u , 0.0f);
  glVertex2f(rd.right, rd.top);

  glTexCoord2f(m_u , m_v);
  glVertex2f(rd.right, rd.bottom);

  glTexCoord2f(0.0f, m_v);
  glVertex2f(rd.left , rd.bottom);
  glEnd();
#else
  g_Windowing.EnableGUIShader(SM_TEXTURE);

  GLfloat col[4][4];
  GLfloat ver[4][2];
  GLfloat tex[4][2];
  GLubyte idx[4] = {0, 1, 3, 2};        //determines order of triangle strip

  GLint posLoc  = g_Windowing.GUIShaderGetPos();
  GLint colLoc  = g_Windowing.GUIShaderGetCol();
  GLint tex0Loc = g_Windowing.GUIShaderGetCoord0();

  glVertexAttribPointer(posLoc,  2, GL_FLOAT, 0, 0, ver);
  glVertexAttribPointer(colLoc,  4, GL_FLOAT, 0, 0, col);
  glVertexAttribPointer(tex0Loc, 2, GL_FLOAT, 0, 0, tex);

  glEnableVertexAttribArray(posLoc);
  glEnableVertexAttribArray(colLoc);
  glEnableVertexAttribArray(tex0Loc);

  for (int i=0; i<4; i++)
  {
    // Setup Colours
    col[i][0] = col[i][1] = col[i][2] = col[i][3] = 1.0f;
  }

  // Setup vertex position values
  ver[0][0] = ver[3][0] = rd.left;
  ver[0][1] = ver[1][1] = rd.top;
  ver[1][0] = ver[2][0] = rd.right;
  ver[2][1] = ver[3][1] = rd.bottom;

  // Setup texture coordinates
  tex[0][0] = tex[0][1] = tex[1][1] = tex[3][0] = 0.0f;
  tex[1][0] = tex[2][0] = m_u;
  tex[2][1] = tex[3][1] = m_v;

  glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, idx);

  glDisableVertexAttribArray(posLoc);
  glDisableVertexAttribArray(colLoc);
  glDisableVertexAttribArray(tex0Loc);

  g_Windowing.DisableGUIShader();
#endif

  glDisable(GL_BLEND);
  glDisable(GL_TEXTURE_2D);

  glBindTexture(GL_TEXTURE_2D, 0);
}

#endif // HAS_GL
