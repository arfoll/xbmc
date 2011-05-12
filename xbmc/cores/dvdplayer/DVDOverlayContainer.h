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

class CDVDInputStreamNavigator;
class CDVDDemuxSPU;

class CDVDOverlayContainer
{
public:
  CDVDOverlayContainer();
  virtual ~CDVDOverlayContainer();

  void Add(char* pPicture); // add a overlay to the fifo

  char* GetOverlays(); // get the first overlay in this fifo
  bool ContainsOverlayType(char type);

  void Remove(); // remove the first overlay in this fifo

  void Clear(); // clear the fifo and delete all overlays
  void CleanUp(double pts); // validates all overlays against current pts
  int GetSize();

  void UpdateOverlayInfo(CDVDInputStreamNavigator* pStream, CDVDDemuxSPU *pSpu, int iAction);
private:
  char Remove(char itOverlay); // removes a specific overlay

  char m_overlays;
};
