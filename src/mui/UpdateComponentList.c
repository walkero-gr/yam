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
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site : http://www.yam.ch
 YAM OpenSource project    : http://sourceforge.net/projects/yamos/

 $Id$

 Superclass:  MUIC_NList
 Description: a list showing all available update components

***************************************************************************/

#include "UpdateComponentList_cl.h"

#include <stdlib.h>
#include <string.h>
#include <proto/muimaster.h>
#include <mui/NList_mcc.h>

#include "YAM_utilities.h"

#include "mui/UpdateNotifyWindow.h"

#include "Locale.h"
#include "MUIObjects.h"
#include "UpdateCheck.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  Object *contextMenu;
  char menuTitle[SIZE_DEFAULT];
};
*/

/// Menu enumerations
enum
{
  UMEN_DOWNLOAD=100,
  UMEN_DOWNLOAD_WITH_BROWSER,
};

///
/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  ENTER();

  obj = DoSuperNew(cl, obj,

    InputListFrame,
    MUIA_NList_Format,               "BAR,BAR,BAR,",
    MUIA_NList_MinColSortable,       0,
    MUIA_NList_TitleClick,           FALSE,
    MUIA_NList_TitleClick2,          FALSE,
    MUIA_NList_MultiSelect,          MUIV_NList_MultiSelect_None,
    MUIA_NList_Title,                TRUE,
    MUIA_NList_TitleSeparator,       TRUE,
    MUIA_NList_DragType,             MUIV_NList_DragType_None,
    MUIA_NList_DefaultObjectOnClick, TRUE,
    MUIA_ContextMenu,                TRUE,
    MUIA_Dropable,                   FALSE,

    TAG_MORE, inittags(msg));

  RETURN((IPTR)obj);
  return (IPTR)obj;
}

///
/// OVERLOAD(OM_DISPOSE)
OVERLOAD(OM_DISPOSE)
{
  GETDATA;

  // make sure that our context menus are also disposed
  if(data->contextMenu != NULL)
    MUI_DisposeObject(data->contextMenu);

  return DoSuperMethodA(cl, obj, msg);
}

///
/// OVERLOAD(MUIM_NList_Destruct)
OVERLOAD(MUIM_NList_Destruct)
{
  struct MUIP_NList_Destruct *ndm = (struct MUIP_NList_Destruct *)msg;
  struct UpdateComponent *entry = (struct UpdateComponent *)ndm->entry;

  ENTER();

  if(entry->changeLogFile != NULL)
    CloseTempFile(entry->changeLogFile);

  free(entry);

  RETURN(0);
  return 0;
}

///
/// OVERLOAD(MUIM_NList_Compare)
OVERLOAD(MUIM_NList_Compare)
{
  struct MUIP_NList_Compare *ncm = (struct MUIP_NList_Compare *)msg;
  struct UpdateComponent *entry1 = (struct UpdateComponent *)ncm->entry1;
  struct UpdateComponent *entry2 = (struct UpdateComponent *)ncm->entry2;
  LONG result;

  ENTER();

  // sort the entries by name, but keep YAM's own updates at the top
  if(Strnicmp(entry1->name, "YAM", 3) == 0)
    result = -1;
  else if(Strnicmp(entry2->name, "YAM", 3) == 0)
    result = +1;
  else
    result = Stricmp(entry1->name, entry2->name);

  RETURN(result);
  return result;
}

///
/// OVERLOAD(MUIM_NList_Display)
OVERLOAD(MUIM_NList_Display)
{
  struct MUIP_NList_Display *ndm = (struct MUIP_NList_Display *)msg;
  struct UpdateComponent *entry = (struct UpdateComponent *)ndm->entry;

  ENTER();

  if(entry != NULL)
  {
    ndm->strings[0] = entry->name;
    ndm->strings[1] = entry->recent;
    ndm->strings[2] = entry->installed;
    ndm->strings[3] = entry->url;
  }
  else
  {
    // setup the listview titles
    ndm->strings[0] = (char *)tr(MSG_UPD_NOTIFICATION_COMP);
    ndm->strings[1] = (char *)tr(MSG_UPD_NOTIFICATION_RECENT);
    ndm->strings[2] = (char *)tr(MSG_UPD_NOTIFICATION_INSTALLED);
    ndm->strings[3] = (char *)tr(MSG_UPD_NOTIFICATION_URL);
  }

  RETURN(0);
  return 0;
}

///
/// OVERLOAD(MUIM_ContextMenuBuild)
OVERLOAD(MUIM_ContextMenuBuild)
{
  GETDATA;
  struct UpdateComponent *comp;

  ENTER();

  // dispose the old context_menu if it still exists
  if(data->contextMenu != NULL)
  {
    MUI_DisposeObject(data->contextMenu);
    data->contextMenu = NULL;
  }

  if((comp = (struct UpdateComponent *)DoMethod(obj, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, NULL)) != NULL)
  {
    strlcpy(data->menuTitle, comp->name, sizeof(data->menuTitle));

    data->contextMenu = MenustripObject,
      Child, MenuObjectT(data->menuTitle),
        Child, MenuitemObject, MUIA_Menuitem_Title, tr(MSG_UPD_NOTIFICATION_DOWNLOAD),              MUIA_Menuitem_CopyStrings, FALSE, MUIA_UserData, UMEN_DOWNLOAD, End,
        Child, MenuitemObject, MUIA_Menuitem_Title, tr(MSG_UPD_NOTIFICATION_DOWNLOAD_WITH_BROWSER), MUIA_Menuitem_CopyStrings, FALSE, MUIA_UserData, UMEN_DOWNLOAD_WITH_BROWSER, MUIA_Menuitem_Enabled, GotoURLPossible(), End,
      End,
    End;
  }

  RETURN(data->contextMenu);
  return (IPTR)data->contextMenu;
}

///
/// OVERLOAD(MUIM_ContextMenuChoice)
OVERLOAD(MUIM_ContextMenuChoice)
{
  struct MUIP_ContextMenuChoice *m = (struct MUIP_ContextMenuChoice *)msg;
  ULONG rc;

  ENTER();

  switch(xget(m->item, MUIA_UserData))
  {
    case UMEN_DOWNLOAD:
      rc = DoMethod(_win(obj), MUIM_UpdateNotifyWindow_Download);
    break;

    case UMEN_DOWNLOAD_WITH_BROWSER:
      rc = DoMethod(_win(obj), MUIM_UpdateNotifyWindow_DownloadWithBrowser);
    break;

    default:
      rc = DoSuperMethodA(cl, obj, msg);
    break;
  }

  RETURN(rc);
  return rc;
}

///
