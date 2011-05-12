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

#include "FileItem.h"
#include "AdvancedSettings.h"
#include "Picture.h"
#include "VideoInfoTag.h"
#include "Util.h"
#include "FileSystem/StackDirectory.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"

#include "DVDClock.h"
#include "DVDFileInfo.h"
#include "FileSystem/File.h"


bool CDVDFileInfo::GetFileDuration(const CStdString &path, int& duration)
{
  return false;
}

bool CDVDFileInfo::ExtractThumb(const CStdString &strPath, const CStdString &strTarget, CStreamDetails *pStreamDetails)
{
  return false;
}


void CDVDFileInfo::GetFileMetaData(const CStdString &strPath, CFileItem *pItem)
{
  return;
}

/**
 * \brief Open the item pointed to by pItem and extact streamdetails
 * \return true if the stream details have changed
 */
bool CDVDFileInfo::GetFileStreamDetails(CFileItem *pItem)
{
  return false;
}

