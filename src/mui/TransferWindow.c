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

 Superclass:  MUIC_Window
 Description: Container window for all kinds of mail transfers (POP3, SMTP, etc)

***************************************************************************/

#include "TransferWindow_cl.h"

#include <proto/muimaster.h>
#include <libraries/iffparse.h>

#include "YAM.h"

#include "Locale.h"
#include "MUIObjects.h"

#include "mui/ObjectList.h"
#include "mui/TransferControlGroup.h"
#include "mui/TransferControlList.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  Object *transferList;

  char screenTitle[SIZE_DEFAULT];
};
*/

/* EXPORT
enum TransferWindowMode
{
  TWM_HIDE = 0,
  TWM_AUTO,
  TWM_SHOW
};
*/

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  Object *transferList;

  ENTER();

  if((obj = DoSuperNew(cl, obj,

    MUIA_Window_ID, MAKE_ID('T','R','A','N'),
    MUIA_Window_CloseGadget, TRUE,
    MUIA_Window_UseRightBorderScroller, TRUE,
    MUIA_Window_SizeRight, TRUE,
    MUIA_HelpNode, "Windows/Mailtransfers",
    WindowContents, VGroup,
      MUIA_Background, MUII_GroupBack,
      Child, transferList = TransferControlListObject,
      End,
    End,

    TAG_MORE, inittags(msg))) != NULL)
  {
    GETDATA;

    xset(obj, MUIA_Window_Title, tr(MSG_TRANSFERS_IN_PROGRESS),
              MUIA_Window_ScreenTitle, CreateScreenTitle(data->screenTitle, sizeof(data->screenTitle), tr(MSG_TRANSFERS_IN_PROGRESS)));

    DoMethod(G->App, OM_ADDMEMBER, obj);

    DoMethod(obj, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, MUIV_Notify_Self, 3, MUIM_Set, MUIA_Window_Open, FALSE);

    data->transferList = transferList;
  }

  RETURN((IPTR)obj);
  return (IPTR)obj;
}

///
/// OVERLOAD(OM_GET)
OVERLOAD(OM_GET)
{
  GETDATA;
  IPTR *store = ((struct opGet *)msg)->opg_Storage;

  switch(((struct opGet *)msg)->opg_AttrID)
  {
    case ATTR(NumberOfControlGroups):
    {
      *store = xget(data->transferList, MUIA_ObjectList_ItemCount);
      return TRUE;
    }
  }

  return DoSuperMethodA(cl, obj, msg);
}

///

/* Private Functions */

/* Public Methods */
/// DECLARE(CreateTransferControlGroup)
DECLARE(CreateTransferControlGroup) // const char *title
{
  GETDATA;
  Object *group;

  ENTER();

  // create a new transfer control group and add it to the list
  if((group = (Object *)DoMethod(data->transferList, MUIM_ObjectList_CreateItem)) != NULL)
  {
    set(group, MUIA_TransferControlGroup_Title, msg->title);
    DoMethod(data->transferList, MUIM_ObjectList_AddItem, group);
  }

  RETURN((IPTR)group);
  return (IPTR)group;
}

///
/// DECLARE(DeleteTransferControlGroup)
DECLARE(DeleteTransferControlGroup) // Object *group
{
  GETDATA;
  BOOL lastItem = FALSE;

  ENTER();

  // check if we are about to remove the last item in the list
  if(xget(data->transferList, MUIA_ObjectList_ItemCount) == 1)
  {
    lastItem = TRUE;
    // close the window before the item is removed
    set(obj, MUIA_Window_Open, FALSE);
  }

  DoMethod(data->transferList, MUIM_ObjectList_RemoveItem, msg->group);

  RETURN(lastItem);
  return lastItem;
}

///
