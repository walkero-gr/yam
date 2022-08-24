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

 Superclass:  MUIC_NList
 Description: List that manages the different pages of the configuration

***************************************************************************/

#include "ConfigPageList_cl.h"

#include <proto/muimaster.h>
#include <mui/NList_mcc.h>

#include "YAM.h"

#include "mui/ImageArea.h"

#include "Config.h"
#include "Locale.h"
#include "Themes.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  Object *configIcon[cp_Max];
  char pageName[SIZE_DEFAULT];
};
*/

/* EXPORT
struct PageList
{
  int Offset;
  const struct fcstr *PageLabel;
};

enum ConfigPage
{
  cp_AllPages = -1,
  cp_FirstSteps = 0,
  cp_TCPIP,
  cp_Identities,
  cp_Filters,
  cp_Spam,
  cp_Read,
  cp_Write,
  cp_ReplyForward,
  cp_Signature,
  cp_Security,
  cp_StartupQuit,
  cp_MIME,
  cp_AddressBook,
  cp_Scripts,
  cp_Mixed,
  cp_LookFeel,
  cp_Update,
  cp_Max,
};
*/

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  ENTER();

  if((obj = DoSuperNew(cl, obj,
    TAG_MORE, inittags(msg))) != NULL)
  {
    GETDATA;
    enum ConfigPage i;

    // create/load all bodychunkimages of our config icons
    data->configIcon[cp_FirstSteps  ] = MakeImageObject("config_firststep", G->theme.configImages[CI_FIRSTSTEP]);
    data->configIcon[cp_TCPIP       ] = MakeImageObject("config_network",   G->theme.configImages[CI_NETWORK]);
    data->configIcon[cp_Identities  ] = MakeImageObject("config_identities",G->theme.configImages[CI_IDENTITIES]);
    data->configIcon[cp_Filters     ] = MakeImageObject("config_filters",   G->theme.configImages[CI_FILTERS]);
    data->configIcon[cp_Spam        ] = MakeImageObject("config_spam",      G->theme.configImages[CI_SPAM]);
    data->configIcon[cp_Read        ] = MakeImageObject("config_read",      G->theme.configImages[CI_READ]);
    data->configIcon[cp_Write       ] = MakeImageObject("config_write",     G->theme.configImages[CI_WRITE]);
    data->configIcon[cp_ReplyForward] = MakeImageObject("config_answer",    G->theme.configImages[CI_ANSWER]);
    data->configIcon[cp_Signature   ] = MakeImageObject("config_signature", G->theme.configImages[CI_SIGNATURE]);
    data->configIcon[cp_Security    ] = MakeImageObject("config_security",  G->theme.configImages[CI_SECURITY]);
    data->configIcon[cp_StartupQuit ] = MakeImageObject("config_start",     G->theme.configImages[CI_START]);
    data->configIcon[cp_MIME        ] = MakeImageObject("config_mime",      G->theme.configImages[CI_MIME]);
    data->configIcon[cp_AddressBook ] = MakeImageObject("config_abook",     G->theme.configImages[CI_ABOOK]);
    data->configIcon[cp_Scripts     ] = MakeImageObject("config_scripts",   G->theme.configImages[CI_SCRIPTS]);
    data->configIcon[cp_Mixed       ] = MakeImageObject("config_misc",      G->theme.configImages[CI_MISC]);
    data->configIcon[cp_LookFeel    ] = MakeImageObject("config_lookfeel",  G->theme.configImages[CI_LOOKFEEL]);
    data->configIcon[cp_Update      ] = MakeImageObject("config_update",    G->theme.configImages[CI_UPDATE]);

    // now we can add the config icon objects and use UseImage
    // to prepare it for displaying it in the NList
    for(i = cp_FirstSteps; i < cp_Max; i++)
      DoMethod(obj, MUIM_NList_UseImage, data->configIcon[i], i, MUIF_NONE);
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
  enum ConfigPage i;

  ENTER();

  for(i = cp_FirstSteps; i < cp_Max; i++)
  {
    if(data->configIcon[i])
      MUI_DisposeObject(data->configIcon[i]);

    data->configIcon[i] = NULL;
  }

  result = DoSuperMethodA(cl, obj, msg);

  RETURN(result);
  return result;
}

///
/// OVERLOAD(MUIM_NList_Display)
OVERLOAD(MUIM_NList_Display)
{
  struct MUIP_NList_Display *ndm = (struct MUIP_NList_Display *)msg;
  struct PageList *entry = ndm->entry;
  GETDATA;

  ndm->strings[0] = data->pageName;
  snprintf(data->pageName, sizeof(data->pageName), "\033o[%d] %s", entry->Offset, tr(entry->PageLabel));

  return 0;
}

///

/* Public Methods */
