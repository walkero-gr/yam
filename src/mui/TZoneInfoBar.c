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

 Superclass:  MUIC_Text
 Description: Text object to show informations about a time zone

***************************************************************************/

#include "TZoneInfoBar_cl.h"

#include <stdlib.h>
#include <limits.h>

#include <proto/muimaster.h>

#include "extrasrc.h"

#include "YAM_utilities.h"

#include "Config.h"
#include "Locale.h"
#include "TZone.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  char infoText[SIZE_LARGE];
  char currentTZone[SIZE_DEFAULT];
};
*/

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  ENTER();

  obj = DoSuperNew(cl, obj,
    MUIA_Weight, 0,
    MUIA_InnerLeft, 0,
    MUIA_InnerRight, 0,
    MUIA_FramePhantomHoriz, TRUE,
    MUIA_Text_Copy, FALSE,
    TAG_MORE, inittags(msg));

  RETURN((IPTR)obj);
  return (IPTR)obj;
}

///
/// OVERLOAD(OM_SET)
OVERLOAD(OM_SET)
{
  GETDATA;
  struct TagItem *tags = inittags(msg), *tag;
  ULONG result;

  ENTER();

  while((tag = NextTagItem((APTR)&tags)) != NULL)
  {
    switch(tag->ti_Tag)
    {
      case ATTR(TZone):
      {
        char *tzone = (char *)tag->ti_Data;

        if(tzone != NULL && strcmp(tzone, data->currentTZone) != 0)
        {
          struct TM tm;
          BOOL resetTZ = FALSE;
          int gmtOffset;
          int convertedGmtOffset;
          char tzabbr[SIZE_SMALL];
          char nextDSTstr[SIZE_DEFAULT];
          struct TimeVal tv;
          time_t dstSwitchTime;
          ULONG continent = 0;
          ULONG location = 0;
          char *tzComment = NULL;

          // get the current date/time in struct tm format
          TimeVal2tm(NULL, &tm);

          // check if we have to handle a different location than the one in the current configuration
          if(strcasecmp(tzone, C->Location) != 0)
          {
            TZSet(tzone);
            resetTZ = TRUE;
          }

          // call mktime() so that struct tm will be set correctly.
          mktime(&tm);

          // copy all info or otherwise it gets lost
          gmtOffset = tm.tm_gmtoff / 60;
          strlcpy(tzabbr, tm.tm_zone, sizeof(tzabbr));

          // get the date/time of the next scheduled DST switch
          dstSwitchTime = FindNextDSTSwitch(NULL, &tv);

          // reset the location to the former value
          if(resetTZ == TRUE)
            TZSet(C->Location);

          // convert the GMT offset to a human readable value
          convertedGmtOffset = (gmtOffset/60)*100 + (gmtOffset%60);

          // create the rfc time string for the next DST switch
          if(dstSwitchTime > 0)
          {
            struct DateStamp ds;

            TimeVal2DateStamp(&tv, &ds, TZC_NONE);
            DateStamp2RFCString(nextDSTstr, sizeof(nextDSTstr), &ds, INT_MIN, NULL, FALSE);
          }
          else
            strlcpy(nextDSTstr, tr(MSG_CO_TZONE_NODSTSWITCH), sizeof(nextDSTstr));

          // get the location comment
          ParseTZoneName(tzone, &continent, &location, &tzComment);

          // prepare the info text we want to show to the user
          snprintf(data->infoText, sizeof(data->infoText),
                                   "%s %s\n%s %+05d (%s)\n%s %s", tr(MSG_CO_TZONE_DESCRIPTION),
                                                                  tzComment != NULL ? tzComment : tr(MSG_CO_TZONE_DESCRIPTION_NA),
                                                                  tr(MSG_CO_TZONE_GMTOFFSET),
                                                                  convertedGmtOffset,
                                                                  tzabbr,
                                                                  tr(MSG_CO_TZONE_NEXTDSTSWITCH),
                                                                  nextDSTstr);

          // set the info text
          set(obj, MUIA_Text_Contents, data->infoText);

          // remember the timezone
          strlcpy(data->currentTZone, tzone, sizeof(data->currentTZone));
        }

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;
    }
  }

  result = DoSuperMethodA(cl, obj, msg);

  RETURN(result);
  return result;
}

///
