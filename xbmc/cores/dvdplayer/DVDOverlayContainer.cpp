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

#include "DVDOverlayContainer.h"


CDVDOverlayContainer::CDVDOverlayContainer()
{
}

CDVDOverlayContainer::~CDVDOverlayContainer()
{
  Clear();
}

void CDVDOverlayContainer::Add(char* pOverlay)
{
}

char* CDVDOverlayContainer::GetOverlays()
{
  return &m_overlays;
}

char CDVDOverlayContainer::Remove(char itOverlay)
{
  return 'a';
}

void CDVDOverlayContainer::CleanUp(double pts)
{
}

void CDVDOverlayContainer::Remove()
{
}

void CDVDOverlayContainer::Clear()
{
}

int CDVDOverlayContainer::GetSize()
{
  return 0;
}

bool CDVDOverlayContainer::ContainsOverlayType(char type)
{
  bool result = false;

  return result;
}

/*
 * iAction should be LIBDVDNAV_BUTTON_NORMAL or LIBDVDNAV_BUTTON_CLICKED
 */
void CDVDOverlayContainer::UpdateOverlayInfo(CDVDInputStreamNavigator* pStream, CDVDDemuxSPU *pSpu, int iAction)
{
}
