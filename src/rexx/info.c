/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 Marcel Beck
 Copyright (C) 2000-2022 YAM Open Source Team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site :  http://www.yam.ch
 YAM OpenSource project    :  http://sourceforge.net/projects/yamos/

 $Id$

***************************************************************************/

#include <string.h>

#include <libraries/mui.h>
#include <proto/exec.h>

#include "extrasrc.h"

#include "YAM.h"
#include "YAM_main.h"

#include "MUIObjects.h"
#include "Rexx.h"

#include "Debug.h"

struct args
{
  struct RexxResult varStem;
  char *item;
};

struct results
{
  char *value;
};

struct optional
{
  char pubScreenName[SIZE_LARGE];
};

void rx_info(UNUSED struct RexxHost *host, struct RexxParams *params, enum RexxAction action, UNUSED struct RexxMsg *rexxmsg)
{
  struct args *args = params->args;
  struct results *results = params->results;
  struct optional *optional = params->optional;

  ENTER();

  switch(action)
  {
    case RXIF_INIT:
    {
      params->args = AllocVecPooled(G->SharedMemPool, sizeof(*args));
      params->results = AllocVecPooled(G->SharedMemPool, sizeof(*results));
      params->optional = AllocVecPooled(G->SharedMemPool, sizeof(*optional));
      if(params->optional == NULL)
        params->rc = RETURN_ERROR;
    }
    break;

    case RXIF_ACTION:
    {
      char *key = args->item;

      if(!key)  params->rc = RETURN_ERROR;
      else if(!stricmp(key, "title"))       results->value = (char *)xget(G->App, MUIA_Application_Title);
      else if(!stricmp(key, "author"))      results->value = (char *)xget(G->App, MUIA_Application_Author);
      else if(!stricmp(key, "copyright"))   results->value = (char *)xget(G->App, MUIA_Application_Copyright);
      else if(!stricmp(key, "description")) results->value = (char *)xget(G->App, MUIA_Application_Description);
      else if(!stricmp(key, "version"))     results->value = (char *)xget(G->App, MUIA_Application_Version);
      else if(!stricmp(key, "base"))        results->value = (char *)xget(G->App, MUIA_Application_Base);
      else if(!stricmp(key, "screen"))
      {
        GetPubScreenName((struct Screen *)xget(G->MA->GUI.WI, MUIA_Window_Screen), optional->pubScreenName, sizeof(optional->pubScreenName));
        results->value = optional->pubScreenName;
      }
      else
        params->rc = RETURN_ERROR;
    }
    break;

    case RXIF_FREE:
    {
      if(args != NULL)
        FreeVecPooled(G->SharedMemPool, args);
      if(results != NULL)
        FreeVecPooled(G->SharedMemPool, results);
      if(optional != NULL)
        FreeVecPooled(G->SharedMemPool, optional);
    }
    break;
  }

  LEAVE();
}
