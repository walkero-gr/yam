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

 Superclass:  MUIC_Cycle
 Description: Cycle object to choose a user identity

***************************************************************************/

#include "IdentityChooser_cl.h"

#include <string.h>

#include <proto/muimaster.h>

#include "YAM_utilities.h"

#include "Config.h"
#include "MUIObjects.h"
#include "UserIdentity.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  struct UserIdentityNode *identity;  // ptr to currently selected user identity
  char **identityArray;               // titles for the different identities that can be selected
  int numIdentities;                  // number of identities
};
*/

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  #if defined(__AROS__)
  // Zune must be provided a valid MUIA_Cycle_Entries pointer
  static const char *dummy[] = { "", NULL };
  #endif

  ENTER();

  if((obj = DoSuperNew(cl, obj,

    MUIA_CycleChain,    TRUE,
    MUIA_Font,          MUIV_Font_Button,
    #if defined(__AROS__)
    MUIA_Cycle_Entries, dummy,
    #endif

    TAG_MORE, inittags(msg))) != NULL)
  {
    GETDATA;

    data->identity = (struct UserIdentityNode *)GetTagData(ATTR(Identity), (ULONG)NULL, inittags(msg));

    // set up the full description of all active identites
    DoMethod(obj, METHOD(UpdateIdentities));

    // notify ourselves about changed active items
    DoMethod(obj, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, obj, 3, MUIM_Set, ATTR(IdentityIndex), MUIV_TriggerValue);
  }

  RETURN((IPTR)obj);
  return (IPTR)obj;
}

///
/// OVERLOAD(OM_DISPOSE)
OVERLOAD(OM_DISPOSE)
{
  GETDATA;
  ULONG result;

  ENTER();

  // free the string array
  FreeStrArray(data->identityArray);

  // signal the super class to dispose as well
  result = DoSuperMethodA(cl, obj, msg);

  RETURN(result);
  return result;
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
      case ATTR(Identity):
      {
        struct UserIdentityNode *newIdentity = (struct UserIdentityNode *)tag->ti_Data;

        if(newIdentity != data->identity)
        {
          int i = 0;

          // find the new identity and set it as active entry
          if(newIdentity != NULL)
          {
            struct UserIdentityNode *uin;

            IterateList(&C->userIdentityList, struct UserIdentityNode *, uin)
            {
              if(uin->id == newIdentity->id)
                break;
              else if(uin->active == TRUE)
                i++;
            }
          }

          data->identity = newIdentity;

          // set the new active item without triggering notifications
          nnset(obj, MUIA_Cycle_Active, i);
        }
      }
      break;

      case ATTR(IdentityIndex):
      {
        struct UserIdentityNode *newIdentity = GetUserIdentity(&C->userIdentityList, tag->ti_Data, TRUE);

        // set the new identity and trigger possible notifications
        set(obj, ATTR(Identity), newIdentity);
      }
      break;
    }
  }

  return DoSuperMethodA(cl, obj, msg);
}

///
/// OVERLOAD(OM_GET)
OVERLOAD(OM_GET)
{
  GETDATA;
  IPTR *store = ((struct opGet *)msg)->opg_Storage;

  switch(((struct opGet *)msg)->opg_AttrID)
  {
    case ATTR(Identity): *store = (IPTR)data->identity; return TRUE;
    case ATTR(NumIdentities): *store = (ULONG)data->numIdentities; return TRUE;
  }

  return DoSuperMethodA(cl, obj, msg);
}

///

/* Private Functions */

/* Public Methods */
/// DECLARE(UpdateIdentities)
// updates the str array containing all identities
DECLARE(UpdateIdentities)
{
  GETDATA;
  struct UserIdentityNode *uin;
  int numIdentities = 0;

  ENTER();

  // we have to sync the content of the user identities
  // with the GUI elements of the write window
  FreeStrArray(data->identityArray);

  // first we find out how many entries the user identity list
  // has
  IterateList(&C->userIdentityList, struct UserIdentityNode *, uin)
  {
    if(uin->active == TRUE)
      numIdentities++;
  }

  // allocate enough space + 1 for NUL termination
  if((data->identityArray = calloc(MAX(2, numIdentities+1), sizeof(char *))) != NULL)
  {
    int active = 0;

    // set a single empty entry if no active identities were found
    // this works around a bug in MUI4 of MorphOS which uses a non-static
    // replacement entry on the stack instead otherwise
    if(numIdentities == 0)
    {
      data->identityArray[0] = strdup("");
    }
    else
    {
      // MUI 3.8 doesn't like left aligned text in a cycle object
      const char *preparse = LIB_VERSION_IS_AT_LEAST(MUIMasterBase, 20, 0) ? MUIX_L : "";
      int i = 0;

      // now we walk through the userIdentityList again
      // and clone the address string
      IterateList(&C->userIdentityList, struct UserIdentityNode *, uin)
      {
        if(uin->active == TRUE)
        {
          char address[SIZE_LARGE];

          // construct the new string via asprintf() so that the necessary
          // memory is automatically allocated.
          if(asprintf(&data->identityArray[i], "%s%s " MUIX_I "(%s)" MUIX_N, preparse, BuildAddress(address, sizeof(address), uin->address, uin->realname), uin->description) != -1)
          {
            // remember the active entry
            if(data->identity != NULL && uin->id == data->identity->id)
            {
              active = i;
            }

            i++;
          }
        }
      }
    }

    // update the entry strings and set the active entry
    xset(obj,
      MUIA_NoNotify, TRUE,
      MUIA_Cycle_Entries, data->identityArray,
      MUIA_Cycle_Active, active);
  }
  else
    numIdentities = 0;

  // save number of identities
  data->numIdentities = numIdentities;

  RETURN(0);
  return 0;
}

///
