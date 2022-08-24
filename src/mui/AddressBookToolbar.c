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

 Superclass:  MUIC_TheBarVirt
 Description: Toolbar management class of the addressbook

***************************************************************************/

#include "AddressBookToolbar_cl.h"

#include <mui/NListtree_mcc.h>

#include "YAM.h"

#include "ImageCache.h"
#include "Locale.h"
#include "MUIObjects.h"

#include "Debug.h"

/* EXPORT
enum { TB_ABOOK_SAVE=0,
       TB_ABOOK_FIND,
       TB_ABOOK_NEWUSER,
       TB_ABOOK_NEWLIST,
       TB_ABOOK_NEWGROUP,
       TB_ABOOK_EDIT,
       TB_ABOOK_DELETE,
       TB_ABOOK_PRINT,
       TB_ABOOK_OPENTREE,
       TB_ABOOK_CLOSETREE,
       TB_ABOOK_NUM
};
*/

/* Private Data */

/* Private Functions */

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  ENTER();

  // depending on whether the write window toolbar
  // exists cached already we go and obtain the images
  // from the cached object instead.
  if(IsToolbarInCache(TBT_AbookWindow))
  {
    // prepare the buttons array which defines how our
    // toolbar looks like.
    struct MUIS_TheBar_Button buttons[TB_ABOOK_NUM+3] =
    {
    #if !defined(__SASC)
      { TB_ABOOK_SAVE,      TB_ABOOK_SAVE,      tr(MSG_AB_TBSave),      tr(MSG_HELP_AB_BT_SAVE),      0, 0, NULL, NULL },
      { TB_ABOOK_FIND,      TB_ABOOK_FIND,      tr(MSG_AB_TBFind),      tr(MSG_HELP_AB_BT_SEARCH),    0, 0, NULL, NULL },

      { MUIV_TheBar_ButtonSpacer, -1,  NULL, NULL, 0, 0, NULL, NULL },

      { TB_ABOOK_NEWUSER,   TB_ABOOK_NEWUSER,   tr(MSG_AB_TBNewUser),   tr(MSG_HELP_AB_BT_ADDUSER),   0, 0, NULL, NULL },
      { TB_ABOOK_NEWLIST,   TB_ABOOK_NEWLIST,   tr(MSG_AB_TBNewList),   tr(MSG_HELP_AB_BT_ADDMLIST),  0, 0, NULL, NULL },
      { TB_ABOOK_NEWGROUP,  TB_ABOOK_NEWGROUP,  tr(MSG_AB_TBNewGroup),  tr(MSG_HELP_AB_BT_ADDGROUP),  0, 0, NULL, NULL },
      { TB_ABOOK_EDIT,      TB_ABOOK_EDIT,      tr(MSG_AB_TBEdit),      tr(MSG_HELP_AB_BT_EDIT),      0, 0, NULL, NULL },
      { TB_ABOOK_DELETE,    TB_ABOOK_DELETE,    tr(MSG_AB_TBDelete),    tr(MSG_HELP_AB_BT_DELETE),    0, 0, NULL, NULL },
      { TB_ABOOK_PRINT,     TB_ABOOK_PRINT,     tr(MSG_AB_TBPrint),     tr(MSG_HELP_AB_BT_PRINT),     0, 0, NULL, NULL },

      { MUIV_TheBar_ButtonSpacer, -1,  NULL, NULL, 0, 0, NULL, NULL },

      { TB_ABOOK_OPENTREE,  TB_ABOOK_OPENTREE,  tr(MSG_AB_TBOpenTree),  tr(MSG_HELP_AB_BT_OPEN),      0, 0, NULL, NULL },
      { TB_ABOOK_CLOSETREE, TB_ABOOK_CLOSETREE, tr(MSG_AB_TBCloseTree), tr(MSG_HELP_AB_BT_CLOSE),     0, 0, NULL, NULL },

      { MUIV_TheBar_End, -1,        NULL, NULL, 0, 0, NULL, NULL },
    #else
      { TB_ABOOK_SAVE,      TB_ABOOK_SAVE,      NULL,                   NULL,                         0, 0, NULL, NULL },
      { TB_ABOOK_FIND,      TB_ABOOK_FIND,      NULL,                   NULL,                         0, 0, NULL, NULL },

      { MUIV_TheBar_ButtonSpacer, -1,  NULL, NULL, 0, 0, NULL, NULL },

      { TB_ABOOK_NEWUSER,   TB_ABOOK_NEWUSER,   NULL,                   NULL,                         0, 0, NULL, NULL },
      { TB_ABOOK_NEWLIST,   TB_ABOOK_NEWLIST,   NULL,                   NULL,                         0, 0, NULL, NULL },
      { TB_ABOOK_NEWGROUP,  TB_ABOOK_NEWGROUP,  NULL,                   NULL,                         0, 0, NULL, NULL },
      { TB_ABOOK_EDIT,      TB_ABOOK_EDIT,      NULL,                   NULL,                         0, 0, NULL, NULL },
      { TB_ABOOK_DELETE,    TB_ABOOK_DELETE,    NULL,                   NULL,                         0, 0, NULL, NULL },
      { TB_ABOOK_PRINT,     TB_ABOOK_PRINT,     NULL,                   NULL,                         0, 0, NULL, NULL },

      { MUIV_TheBar_ButtonSpacer, -1,  NULL, NULL, 0, 0, NULL, NULL },

      { TB_ABOOK_OPENTREE,  TB_ABOOK_OPENTREE,  NULL,                   NULL,                         0, 0, NULL, NULL },
      { TB_ABOOK_CLOSETREE, TB_ABOOK_CLOSETREE, NULL,                   NULL,                         0, 0, NULL, NULL },

      { MUIV_TheBar_End, -1,        NULL, NULL, 0, 0, NULL, NULL },
    #endif
    };

    #if defined(__SASC)
    buttons[ 0].text = tr(MSG_AB_TBSave);        buttons[ 0].help = tr(MSG_HELP_AB_BT_SAVE);
    buttons[ 1].text = tr(MSG_AB_TBFind);        buttons[ 1].help = tr(MSG_HELP_AB_BT_SEARCH);
    buttons[ 3].text = tr(MSG_AB_TBNewUser);     buttons[ 3].help = tr(MSG_HELP_AB_BT_ADDUSER);
    buttons[ 4].text = tr(MSG_AB_TBNewList);     buttons[ 4].help = tr(MSG_HELP_AB_BT_ADDMLIST);
    buttons[ 5].text = tr(MSG_AB_TBNewGroup);    buttons[ 5].help = tr(MSG_HELP_AB_BT_ADDGROUP);
    buttons[ 6].text = tr(MSG_AB_TBEdit);        buttons[ 6].help = tr(MSG_HELP_AB_BT_EDIT);
    buttons[ 7].text = tr(MSG_AB_TBDelete);      buttons[ 7].help = tr(MSG_HELP_AB_BT_DELETE);
    buttons[ 8].text = tr(MSG_AB_TBPrint);       buttons[ 8].help = tr(MSG_HELP_AB_BT_PRINT);
    buttons[10].text = tr(MSG_AB_TBOpenTree);    buttons[10].help = tr(MSG_HELP_AB_BT_OPEN);
    buttons[11].text = tr(MSG_AB_TBCloseTree);   buttons[11].help = tr(MSG_HELP_AB_BT_CLOSE);
    #endif

    // create TheBar object with the cached
    // toolbar images
    obj = DoSuperNew(cl, obj,
                     MUIA_Group_Horiz,      TRUE,
                     MUIA_TheBar_Buttons,   buttons,
                     MUIA_TheBar_Images,    ObtainToolbarImages(TBT_AbookWindow, TBI_Normal),
                     MUIA_TheBar_DisImages, ObtainToolbarImages(TBT_AbookWindow, TBI_Ghosted),
                     MUIA_TheBar_SelImages, ObtainToolbarImages(TBT_AbookWindow, TBI_Selected),
                     TAG_MORE, inittags(msg));

  }
  else
  {
    // create the TheBar object, but via loading the images from
    // the corresponding image files.
    obj = DoSuperNew(cl, obj,
                     MUIA_TheBar_Pics,      G->theme.abookWindowToolbarImages[TBIM_NORMAL],
                     MUIA_TheBar_SelPics,   G->theme.abookWindowToolbarImages[TBIM_SELECTED],
                     MUIA_TheBar_DisPics,   G->theme.abookWindowToolbarImages[TBIM_GHOSTED],
                     TAG_MORE, inittags(msg));
  }

  RETURN((IPTR)obj);
  return (IPTR)obj;
}

///
