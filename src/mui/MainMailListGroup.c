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
 YAM OpenSource project     : http://sourceforge.net/projects/yamos/

 $Id$

 Superclass:  MUIC_Group
 Description: Groups multiple 'MainMailList' objects into one PageGroup

***************************************************************************/

#include "MainMailListGroup_cl.h"

#include <proto/muimaster.h>
#include <libraries/iffparse.h>
#include <mui/NList_mcc.h>
#include <mui/NListview_mcc.h>

#include "YAM.h"

#include "mui/MainMailList.h"
#include "mui/QuickSearchBar.h"

#include "Config.h"
#include "MUIObjects.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  Object *mainListviewObjects[LT_COUNT];
  Object *mainListObjects[LT_COUNT];

  struct Mail *lastActiveMail;
  ULONG activeList;
  BOOL setupDone;
  BOOL listIsFreezed; // if set to true the list is hard freezed (cannot be unquite via NList_Quiet)
};
*/

/* EXPORT
enum MainListType
{
  LT_MAIN=0,
  LT_QUICKVIEW,
  LT_COUNT
};
*/

/* Private Functions */

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  Object *mainListviewObjects[LT_COUNT];
  Object *mainListObjects[LT_COUNT];

  if((obj = DoSuperNew(cl, obj,

    MUIA_Group_PageMode, TRUE,

    Child, mainListviewObjects[LT_MAIN] = NListviewObject,
      MUIA_CycleChain, TRUE,
      MUIA_NListview_NList, mainListObjects[LT_MAIN] = MainMailListObject,
        MUIA_ObjectID,       MAKE_ID('N','L','0','2'),
        MUIA_ContextMenu,    C->MessageCntMenu ? MUIV_NList_ContextMenu_Always : MUIV_NList_ContextMenu_Never,
        MUIA_NList_DragType, MUIV_NList_DragType_Default,
        MUIA_NList_Exports,  MUIV_NList_Exports_ColWidth|MUIV_NList_Exports_ColOrder,
        MUIA_NList_Imports,  MUIV_NList_Imports_ColWidth|MUIV_NList_Imports_ColOrder,
      End,
    End,
    Child, mainListviewObjects[LT_QUICKVIEW] = NListviewObject,
      MUIA_CycleChain, TRUE,
      MUIA_NListview_NList, mainListObjects[LT_QUICKVIEW] = MainMailListObject,
        MUIA_ContextMenu,    C->MessageCntMenu ? MUIV_NList_ContextMenu_Always : MUIV_NList_ContextMenu_Never,
        MUIA_NList_DragType, MUIV_NList_DragType_Default,
      End,
    End,

    TAG_MORE, inittags(msg))) != NULL)
  {
    GETDATA;

    data->mainListviewObjects[LT_MAIN] =      mainListviewObjects[LT_MAIN];
    data->mainListviewObjects[LT_QUICKVIEW] = mainListviewObjects[LT_QUICKVIEW];
    data->mainListObjects[LT_MAIN] =          mainListObjects[LT_MAIN];
    data->mainListObjects[LT_QUICKVIEW] =     mainListObjects[LT_QUICKVIEW];
  }

  return (IPTR)obj;
}

///
/// OVERLOAD(OM_GET)
// this is to delegate the OM_GET to the correct NLists
OVERLOAD(OM_GET)
{
  GETDATA;
  IPTR *store = ((struct opGet *)msg)->opg_Storage;

  switch(((struct opGet *)msg)->opg_AttrID)
  {
    case ATTR(ActiveList):           *store = data->activeList; return TRUE;
    case ATTR(ActiveListviewObject): *store = (ULONG)data->mainListviewObjects[data->activeList]; return TRUE;
    case ATTR(ActiveListObject):     *store = (ULONG)data->mainListObjects[data->activeList]; return TRUE;
    case ATTR(MainList):             *store = (ULONG)data->mainListObjects[LT_MAIN]; return TRUE;
    case ATTR(LastActiveMail):       *store = (ULONG)data->lastActiveMail; return TRUE;
    case ATTR(Freeze):               *store = (BOOL)data->listIsFreezed; return TRUE;

    // we also return foreign attributes
    case MUIA_NList_Active:
    case MUIA_NList_Entries:
    case MUIA_MainMailList_SortOrderReversed:
    {
      *store = xget(data->mainListObjects[data->activeList], ((struct opGet *)msg)->opg_AttrID);
      return TRUE;
    }
    break;
  }

  return DoSuperMethodA(cl, obj, msg);
}

///
/// OVERLOAD(OM_SET)
OVERLOAD(OM_SET)
{
  GETDATA;
  struct TagItem *tags = inittags(msg), *tag;

  while((tag = NextTagItem((APTR)&tags)) != NULL)
  {
    switch(tag->ti_Tag)
    {
      case ATTR(Freeze):
      {
        // mark the list as freezed or unfreezed for the LISTFREEZE/LISTUNFREEZE Arexx Command
        data->listIsFreezed = tag->ti_Data;

        // make sure to freeze/unfreeze
        set(obj, MUIA_NList_Quiet, data->listIsFreezed);

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;

      case MUIA_Group_ActivePage:
      {
        if(data->activeList != tag->ti_Data)
        {
          // set the new mainlist as the default object of the window it belongs to
          // but only if not another one is yet active
          if(data->setupDone == TRUE && (Object*)xget(_win(obj), MUIA_Window_DefaultObject) == data->mainListviewObjects[data->activeList])
            set(_win(obj), MUIA_Window_DefaultObject, data->mainListviewObjects[tag->ti_Data]);

          data->activeList = tag->ti_Data;
        }
      }
      break;

      case MUIA_NList_Active:
      {
        // make the entry the active one and center the list on it
        DoMethod(data->mainListObjects[data->activeList], MUIM_NList_SetActive, tag->ti_Data, MUIV_NList_SetActive_Jump_Center);

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;

      case MUIA_NList_Quiet:
      {
        if(tag->ti_Data == TRUE || data->listIsFreezed == FALSE)
        {
          set(data->mainListObjects[LT_MAIN], MUIA_NList_Quiet, tag->ti_Data);
          set(data->mainListObjects[LT_QUICKVIEW], MUIA_NList_Quiet, tag->ti_Data);
        }

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;

      case MUIA_ContextMenu:
      case MUIA_NList_DisplayRecall:
      case MUIA_NList_SortType:
      case MUIA_NList_SortType2:
      case MUIA_NList_KeyLeftFocus:
      {
        set(data->mainListObjects[LT_MAIN], tag->ti_Tag, tag->ti_Data);
        set(data->mainListObjects[LT_QUICKVIEW], tag->ti_Tag, tag->ti_Data);

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;
    }
  }

  return DoSuperMethodA(cl, obj, msg);
}

///
/// OVERLOAD(MUIM_Setup)
OVERLOAD(MUIM_Setup)
{
  GETDATA;
  IPTR result;

  ENTER();

  if((result = DoSuperMethodA(cl, obj, msg)) != 0)
  {
    data->setupDone = TRUE;
  }

  RETURN(result);
  return result;
}

///
/// OVERLOAD(MUIM_Cleanup)
OVERLOAD(MUIM_Cleanup)
{
  GETDATA;
  IPTR result;

  ENTER();

  data->setupDone = FALSE;
  result = DoSuperMethodA(cl, obj, msg);

  RETURN(result);
  return result;
}

///
/// OVERLOAD(MUIM_GoActive)
OVERLOAD(MUIM_GoActive)
{
  GETDATA;

  ENTER();

  // we don't forward the GoActive() call to our own
  // superclass but forward it to the activeList object itself
  set(_win(obj), MUIA_Window_ActiveObject, data->mainListviewObjects[data->activeList]);

  RETURN(0);
  return 0;
}

///
/// OVERLOAD(MUIM_NList_Clear)
OVERLOAD(MUIM_NList_Clear)
{
  GETDATA;
  IPTR result;

  // delegate this method to the currently active NList only
  result = DoMethodA(data->mainListObjects[data->activeList], msg);

  return result;
}

///
/// OVERLOAD(MUIM_NList_GetEntry)
OVERLOAD(MUIM_NList_GetEntry)
{
  GETDATA;
  IPTR result;

  // delegate this method to the currently active NList only
  result = DoMethodA(data->mainListObjects[data->activeList], msg);

  return result;
}

///
/// OVERLOAD(MUIM_NList_GetPos)
OVERLOAD(MUIM_NList_GetPos)
{
  GETDATA;
  IPTR result;

  // delegate this method to the currently active NList only
  result = DoMethodA(data->mainListObjects[data->activeList], msg);

  return result;
}

///
/// OVERLOAD(MUIM_NList_Insert)
OVERLOAD(MUIM_NList_Insert)
{
  GETDATA;
  IPTR result;

  // delegate this method to the currently active NList only
  result = DoMethodA(data->mainListObjects[data->activeList], msg);

  return result;
}

///
/// OVERLOAD(MUIM_NList_InsertSingle)
OVERLOAD(MUIM_NList_InsertSingle)
{
  GETDATA;
  IPTR result;

  // we always add the mail to the main list
  result = DoMethodA(data->mainListObjects[LT_MAIN], msg);

  // the InsertSignal method is used by all internal parties
  // to put a mail into a main listview. But in case we have a
  // quicksearchbar we have to check whether this also matches
  // any criteria of it and also put it in there
  if(data->activeList == LT_QUICKVIEW)
  {
    struct MUIP_NList_InsertSingle* m = (struct MUIP_NList_InsertSingle*)msg;

    // check if mail matches the search/view criteria of the quicksearchbar
    if(DoMethod(G->MA->GUI.GR_QUICKSEARCHBAR, MUIM_QuickSearchBar_MatchMail, m->entry) == TRUE)
    {
      result = DoMethodA(data->mainListObjects[LT_QUICKVIEW], msg);

      // make sure the statistics of the quicksearchbar are updated.
      DoMethod(G->MA->GUI.GR_QUICKSEARCHBAR, MUIM_QuickSearchBar_UpdateStats, FALSE);
    }
  }

  return result;
}

///
/// OVERLOAD(MUIM_NList_NextSelected)
OVERLOAD(MUIM_NList_NextSelected)
{
  GETDATA;
  IPTR result;

  // delegate this method to the currently active NList only
  result = DoMethodA(data->mainListObjects[data->activeList], msg);

  return result;
}

///
/// OVERLOAD(MUIM_NList_Redraw)
OVERLOAD(MUIM_NList_Redraw)
{
  GETDATA;
  IPTR result;

  // delegate this method to the currently active NList only
  result = DoMethodA(data->mainListObjects[data->activeList], msg);

  return result;
}

///
/// OVERLOAD(MUIM_NList_Select)
OVERLOAD(MUIM_NList_Select)
{
  GETDATA;
  IPTR result;

  // delegate this method to the currently active NList only
  result = DoMethodA(data->mainListObjects[data->activeList], msg);

  return result;
}

///
/// OVERLOAD(MUIM_NList_Sort)
OVERLOAD(MUIM_NList_Sort)
{
  GETDATA;

  // delegate this method call to all subNLists
  DoMethodA(data->mainListObjects[LT_MAIN], msg);
  DoMethodA(data->mainListObjects[LT_QUICKVIEW], msg);

  return 0;
}

///

/* Public Methods */
/// DECLARE(MakeFormat)
DECLARE(MakeFormat)
{
  GETDATA;

  // forward the MakeFormat message call to all our sublists as well
  DoMethod(data->mainListObjects[LT_MAIN],      MUIM_MainMailList_MakeFormat);
  DoMethod(data->mainListObjects[LT_QUICKVIEW], MUIM_MainMailList_MakeFormat);

  return 0;
}

///
/// DECLARE(SwitchToList)
DECLARE(SwitchToList) // enum MainListType type
{
  GETDATA;

  ENTER();

  // refresh the last active information
  DoMethod(data->mainListObjects[data->activeList], MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &data->lastActiveMail);

  if(data->activeList != msg->type)
  {
    int i;
    BOOL listWasActive = FALSE;

    // before we switch the activePage of the group object we
    // have to set the individual column width of the two NLists as we only save
    // the width of one.
    for(i=0; i < NUMBER_MAILLIST_COLUMNS; i++)
    {
      LONG colWidth = DoMethod(data->mainListObjects[data->activeList], MUIM_NList_ColWidth, i, MUIV_NList_ColWidth_Get);

      // set the column width of the LT_QUICKVIEW maillist also the same
      DoMethod(data->mainListObjects[msg->type], MUIM_NList_ColWidth, i, colWidth != -1 ? colWidth : MUIV_NList_ColWidth_Default);
    }

    // see if we have to make the switched object as the new active one
    if(data->setupDone == TRUE && ((Object *)xget(_win(obj), MUIA_Window_ActiveObject)) == data->mainListviewObjects[data->activeList])
      listWasActive = TRUE;

    // switch the page of the group now
    set(obj, MUIA_Group_ActivePage, msg->type);

    if(msg->type == LT_MAIN)
    {
      LONG pos = MUIV_NList_GetPos_Start;

      // in case we are switching from LT_QUICKVIEW->LT_MAIN we go and set the
      // last active mail as well.
      if(data->lastActiveMail != NULL)
      {
        // retrieve the number of the lastActive entry within the main mail list
        DoMethod(data->mainListObjects[LT_MAIN], MUIM_NList_GetPos, data->lastActiveMail, &pos);
      }

      if(pos == MUIV_NList_GetPos_End || pos == MUIV_NList_GetPos_Start)
        pos = MUIV_NList_Active_Top;

      // make sure to set a new message so that the mail view is updated
      DoMethod(data->mainListObjects[LT_MAIN], MUIM_NList_SetActive, pos, MUIV_NList_SetActive_Jump_Center);
      set(data->mainListObjects[LT_MAIN], MUIA_NList_SelectChange, TRUE);
    }

    if(listWasActive == TRUE)
      set(_win(obj), MUIA_Window_ActiveObject, data->mainListviewObjects[data->activeList]);
  }

  // no matter what, we always clear the quickview list on a switch.
  DoMethod(data->mainListObjects[LT_QUICKVIEW], MUIM_NList_Clear);

  RETURN(0);
  return 0;
}

///
/// DECLARE(AddMailToList)
// add a mail to a specific list
DECLARE(AddMailToList) // enum MainListType type, struct Mail* mail
{
  GETDATA;

  ENTER();

  // we add the mail to a specific list of our group
  DoMethod(data->mainListObjects[msg->type], MUIM_NList_InsertSingle, msg->mail, MUIV_NList_Insert_Sorted);

  // update the searchbar statistics if necessary
  if(data->activeList == LT_QUICKVIEW)
    DoMethod(G->MA->GUI.GR_QUICKSEARCHBAR, MUIM_QuickSearchBar_UpdateStats, FALSE);

  RETURN(0);
  return 0;
}

///
/// DECLARE(RemoveMail)
// properly removes a mail from both lists
DECLARE(RemoveMail) // struct Mail* mail
{
  GETDATA;
  IPTR result;

  ENTER();

  // first we check whether the active one was the quickview and if so we also remove
  // the mail from the main list
  if(data->activeList == LT_QUICKVIEW)
  {
    // forward the command to the list itself
    DoMethod(data->mainListObjects[LT_MAIN], MUIM_MainMailList_RemoveMail, msg->mail);
  }

  // now also remove the mail from the currently active list
  result = DoMethod(data->mainListObjects[data->activeList], MUIM_MainMailList_RemoveMail, msg->mail);

  RETURN(result);
  return result;
}

///
/// DECLARE(RedrawMail)
// redraws the mail on our currently active list
DECLARE(RedrawMail) // struct Mail* mail
{
  GETDATA;
  LONG pos = MUIV_NList_GetPos_Start;
  BOOL result = FALSE;

  ENTER();

  DoMethod(data->mainListObjects[data->activeList], MUIM_NList_GetPos, msg->mail, &pos);
  if(pos != MUIV_NList_GetPos_End)
  {
    DoMethod(data->mainListObjects[data->activeList], MUIM_NList_Redraw, pos);
    result = TRUE;
  }

  RETURN(result);
  return result;
}

///
/// DECLARE(IsMailList)
// checks if a passed object pointer is one of our maillists (for checking dragdrop requests)
DECLARE(IsMailList) // Object *list
{
  GETDATA;

  ASSERT(msg->list != NULL);

  return (ULONG)(msg->list == data->mainListObjects[LT_MAIN] ||
                 msg->list == data->mainListObjects[LT_QUICKVIEW]);
}

///
/// DECLARE(JumpToFirstNewMailOfFolder)
// jump to the first "new" mail of a folder
DECLARE(JumpToFirstNewMailOfFolder) // struct Folder *folder
{
  GETDATA;
  BOOL jumped;

  ENTER();

  jumped = DoMethod(data->mainListObjects[data->activeList], MUIM_MainMailList_JumpToFirstNewMailOfFolder, msg->folder);

  RETURN(jumped);
  return jumped;
}

///
/// DECLARE(JumpToRecentMailOfFolder)
// jump to the most recent mail of a folder
DECLARE(JumpToRecentMailOfFolder) // struct Folder *folder
{
  GETDATA;
  BOOL jumped;

  ENTER();

  jumped = DoMethod(data->mainListObjects[data->activeList], MUIM_MainMailList_JumpToRecentMailOfFolder, msg->folder);

  RETURN(jumped);
  return jumped;
}

///
/// DECLARE(DisplayMailsOfFolder)
// display the mails of the given folder
DECLARE(DisplayMailsOfFolder) // struct Folder *folder
{
  GETDATA;

  ENTER();

  DoMethod(data->mainListObjects[data->activeList], MUIM_MainMailList_DisplayMailsOfFolder, msg->folder);
  // make sure the maillist is enabled
  set(obj, MUIA_Disabled, FALSE);

  RETURN(0);
  return 0;
}

///
