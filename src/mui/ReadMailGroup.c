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
 Description: Mail read GUI group which include a texteditor and a header display

***************************************************************************/

#include "ReadMailGroup_cl.h"

#include <ctype.h>
#include <string.h>
#include <proto/dos.h>
#include <proto/muimaster.h>
#include <libraries/gadtools.h>
#include <libraries/iffparse.h>
#include <mui/NBalance_mcc.h>
#include <mui/NList_mcc.h>
#include <mui/NListview_mcc.h>
#include <mui/TextEditor_mcc.h>

#include "SDI_hook.h"

#include "YAM.h"
#include "YAM_error.h"
#include "YAM_mainFolder.h"
#include "YAM_read.h"

#include "mui/AttachmentGroup.h"
#include "mui/HeaderList.h"
#include "mui/ImageArea.h"
#include "mui/MainMailListGroup.h"
#include "mui/MailTextEdit.h"
#include "mui/SearchTextWindow.h"
#include "mui/ReadMailGroup.h"
#include "mui/ReadWindow.h"
#include "mui/YAMApplication.h"

#include "Busy.h"
#include "Config.h"
#include "DynamicString.h"
#include "FileInfo.h"
#include "HTML2Mail.h"
#include "Locale.h"
#include "Logfile.h"
#include "MailList.h"
#include "MethodStack.h"
#include "MimeTypes.h"
#include "MUIObjects.h"
#include "ParseEmail.h"
#include "Requesters.h"
#include "Timer.h"
#include "UserIdentity.h"

#include "Debug.h"

/* CLASSDATA
struct Data
{
  struct ReadMailData *readMailData;

  Object *headerGroup;
  Object *headerListview;
  Object *headerList;
  Object *senderImageGroup;
  Object *senderImage;
  Object *senderImageSpace;
  Object *balanceObjectTop;
  Object *balanceObjectBottom;
  Object *mailBodyGroup;
  Object *mailTextObject;
  Object *textEditScrollbar;
  Object *scrolledAttachmentGroup;
  Object *attachmentGroup;
  Object *contextMenu;
  Object *searchWindow;

  char menuTitle[SIZE_DEFAULT];

  struct MinList senderInfoHeaders;

  BOOL hasContent;
  BOOL activeAttachmentGroup;
};
*/

/* EXPORT
#define MUIF_ReadMailGroup_Clear_KeepText              (1<<0) // don't clear the text editor content
#define MUIF_ReadMailGroup_Clear_KeepAttachmentGroup   (1<<1) // don't clear the attachmentgroup
#define hasKeepTextFlag(v)            (isFlagSet((v), MUIF_ReadMailGroup_Clear_KeepText))
#define hasKeepAttachmentGroupFlag(v) (isFlagSet((v), MUIF_ReadMailGroup_Clear_KeepAttachmentGroup))

#define MUIF_ReadMailGroup_ReadMail_UpdateOnly         (1<<0) // the call to ReadMail is just because of an update of the same mail
#define MUIF_ReadMailGroup_ReadMail_StatusChangeDelay  (1<<1) // the mail status should not be change immediatley but with a specified time interval
#define MUIF_ReadMailGroup_ReadMail_UpdateTextOnly     (1<<2) // update the main mail text only
#define hasUpdateOnlyFlag(v)         (isFlagSet((v), MUIF_ReadMailGroup_ReadMail_UpdateOnly))
#define hasStatusChangeDelayFlag(v)  (isFlagSet((v), MUIF_ReadMailGroup_ReadMail_StatusChangeDelay))
#define hasUpdateTextOnlyFlag(v)     (isFlagSet((v), MUIF_ReadMailGroup_ReadMail_UpdateTextOnly))

#define MUIF_ReadMailGroup_Search_Again                (1<<0) // perform the same search again
#define hasSearchAgainFlag(v)        (isFlagSet((v), MUIF_ReadMailGroup_Search_Again))

#define MUIF_ReadMailGroup_DoEditAction_Fallback       (1<<0) // fallback to default
#define hasEditActionFallbackFlag(v)  (isFlagSet((v), MUIF_ReadMailGroup_DoEditAction_Fallback))
*/

/// Menu enumerations
enum { RMEN_HSHORT=100, RMEN_HFULL, RMEN_SNONE, RMEN_SDATA, RMEN_SFULL, RMEN_SIMAGE, RMEN_WRAPH,
       RMEN_TSTYLE, RMEN_FFONT, RMEN_EXTKEY, RMEN_CHKSIG, RMEN_SAVEDEC, RMEN_DISPLAY, RMEN_DETACH,
       RMEN_DELETEATT, RMEN_PRINT, RMEN_SAVE, RMEN_REPLY, RMEN_FORWARD_ATTACH, RMEN_FORWARD_INLINE,
       RMEN_MOVE, RMEN_COPY, RMEN_DELETE, RMEN_SEARCH, RMEN_SEARCHAGAIN, RMEN_TCOLOR
     };
///

/* Hooks */
/// TextEditDoubleClickHook
//  Handles double-clicks on an URL
HOOKPROTONH(TextEditDoubleClickFunc, BOOL, Object *obj, struct ClickMessage *clickmsg)
{
  // default to let TextEditor.mcc handle the double click
  BOOL result = FALSE;

  ENTER();

  D(DBF_GUI, "DoubleClick: %ld - [%s]", clickmsg->ClickPosition, clickmsg->LineContents);

  // if the user clicked on space we skip the following
  // analysis of a URL and just check if it was an attachment the user clicked at
  if(!isspace(clickmsg->LineContents[clickmsg->ClickPosition]))
  {
    char *line;

    // then we make a copy of the LineContents
    if((line = strdup(clickmsg->LineContents)) != NULL)
    {
      int pos = clickmsg->ClickPosition;
      char *surl;
      char *url = NULL;
      char *eol = &line[strlen(line)];
      char *p;
      enum tokenType type;

      // find the beginning of the word we clicked at
      surl = &line[pos];
      while(surl != &line[0] && !isspace(*(surl-1)))
        surl--;

      // now find the end of the word the user clicked at
      p = &line[pos];
      while(p+1 != eol && !isspace(*(p+1)))
        p++;

      *(++p) = '\0';

      SHOWSTRING(DBF_GUI, surl);

      // now we start our quick lexical analysis to find a clickable element within
      // the doubleclick area
      if((type = ExtractURL(surl, &url)) != 0)
      {
        SHOWSTRING(DBF_GUI, url);

        switch(type)
        {
          case tEMAIL:
          {
            RE_ClickedOnMessage(url, _win(obj));
            // no further handling by TextEditor.mcc required
            result = TRUE;
          }
          break;

          case tMAILTO:
          {
            RE_ClickedOnMessage(&url[7], _win(obj));
            // no further handling by TextEditor.mcc required
            result = TRUE;
          }
          break;

          case tHTTP:
          case tHTTPS:
          case tFTP:
          case tFILE:
          case tGOPHER:
          case tTELNET:
          case tNEWS:
          case tURL:
          {
            BOOL newWindow = FALSE;

            // TextEditor.mcc V15.26+ tells us the pressed qualifier
            // if the CTRL key is pressed we try to open a new window
            newWindow = isAnyFlagSet(clickmsg->Qualifier, IEQUALIFIER_CONTROL);
            SHOWVALUE(DBF_GUI, newWindow);

            // don't invoke the GotoURL command right here in there hook, as the
            // execution may take lots of time
            PushMethodOnStack(_app(obj), 3, MUIM_YAMApplication_GotoURL, url, newWindow);

            // don't free the URL string in this context
            url = NULL;

            // no further handling by TextEditor.mcc required
            result = TRUE;
          }
          break;

          default:
            // nothing
          break;
        }
      }

      free(url);
      free(line);
    }
  }

  RETURN(result);
  return result;
}
MakeStaticHook(TextEditDoubleClickHook, TextEditDoubleClickFunc);

///

/* Private Functions */
/// ShowAttachmentGroup
// add the attachmentrgoup to our readmailgroup object
static BOOL ShowAttachmentGroup(struct Data *data)
{
  BOOL success = FALSE;

  ENTER();

  if(data->activeAttachmentGroup == FALSE)
  {
    if(DoMethod(data->mailBodyGroup, MUIM_Group_InitChange))
    {
      // add the group to the surrounding group
      DoMethod(data->mailBodyGroup, OM_ADDMEMBER, data->balanceObjectBottom);
      DoMethod(data->mailBodyGroup, OM_ADDMEMBER, data->scrolledAttachmentGroup);
      data->activeAttachmentGroup = TRUE;

      DoMethod(data->mailBodyGroup, MUIM_Group_ExitChange);

      success = TRUE;
    }
  }
  else
  {
    // force a relayout of the scrollgroup, otherwise the scrollbars of the
    // attachment group might not be refreshed correctly
    if(DoMethod(data->scrolledAttachmentGroup, MUIM_Group_InitChange))
      DoMethod(data->scrolledAttachmentGroup, MUIM_Group_ExitChange2, TRUE);

    // an attachment group already existed so signal success
    success = TRUE;
  }

  RETURN(success);
  return success;
}
///
/// HideAttachmentGroup
// remove an attachment group from the window if it exists
static void HideAttachmentGroup(struct Data *data)
{
  ENTER();

  if(data->activeAttachmentGroup)
  {
    if(DoMethod(data->mailBodyGroup, MUIM_Group_InitChange))
    {
      // remove the attachment group and free it
      DoMethod(data->mailBodyGroup, OM_REMMEMBER, data->balanceObjectBottom);
      DoMethod(data->mailBodyGroup, OM_REMMEMBER, data->scrolledAttachmentGroup);
      data->activeAttachmentGroup = FALSE;

      DoMethod(data->mailBodyGroup, MUIM_Group_ExitChange);
    }
  }

  LEAVE();
}
///
/// ParamEnd
//  Finds next parameter in header field
static char *ParamEnd(const char *s)
{
  char *result = NULL;
  BOOL inquotes = FALSE;

  ENTER();

  while(*s != '\0')
  {
    if(inquotes == TRUE)
    {
      if(*s == '"')
        inquotes = FALSE;
      else if(*s == '\\')
        s++;
    }
    else if(*s == ';')
    {
      result = (char *)s;
      break;
    }
    else if(*s == '"')
    {
      inquotes = TRUE;
    }

    s++;
  }

  RETURN(result);
  return result;
}
///
/// Cleanse
//  Removes trailing and leading spaces and converts string to lower case
static char *Cleanse(char *s)
{
  char *tmp;

  ENTER();

  // skip all leading spaces and return pointer
  // to first real char.
  s = TrimStart(s);

  // convert all chars to lowercase no matter what
  for(tmp=s; *tmp; ++tmp)
    *tmp = tolower((int)*tmp);

  // now we walk back from the end of the string
  // and strip the trailing spaces.
  while(tmp > s && *--tmp && isspace(*tmp))
    *tmp = '\0';

  RETURN(s);
  return s;
}

///
/// ExtractSenderInfo
// Extracts all sender information of a mail and put it
// into a supplied ABEntry. (parses X-SenderInfo header field)
static void ExtractSenderInfo(const struct Mail *mail, struct ABookNode *ab)
{
  ENTER();

  InitABookNode(ab, ABNT_USER);
  strlcpy(ab->Address, mail->From.Address, sizeof(ab->Address));
  strlcpy(ab->RealName, mail->From.RealName, sizeof(ab->RealName));

  if(isSenderInfoMail(mail))
  {
    struct ExtendedMail *email;

    if((email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)) != NULL)
    {
      char *s;

      if((s = strchr(email->SenderInfo, ';')) != NULL)
      {
        char *t;

        *s++ = '\0';
        do
        {
          char *eq;

          if((t = ParamEnd(s)) != NULL)
            *t++ = '\0';

          if((eq = strchr(s, '=')) == NULL)
            Cleanse(s);
          else
          {
            *eq++ = '\0';
            s = Cleanse(s);
            eq = TrimStart(eq);
            TrimEnd(eq);
            UnquoteString(eq, FALSE);

            if(stricmp(s, "street") == 0)
              strlcpy(ab->Street, eq, sizeof(ab->Street));
            else if(stricmp(s, "city") == 0)
              strlcpy(ab->City, eq, sizeof(ab->City));
            else if(stricmp(s, "country") == 0)
              strlcpy(ab->Country, eq, sizeof(ab->Country));
            else if(stricmp(s, "phone") == 0)
              strlcpy(ab->Phone, eq, sizeof(ab->Phone));
            else if(stricmp(s, "homepage") == 0)
              strlcpy(ab->Homepage, eq, sizeof(ab->Homepage));
            else if(stricmp(s, "dob") == 0)
              ab->Birthday = atol(eq);
            else if(stricmp(s, "picture") == 0)
              strlcpy(ab->Photo, eq, sizeof(ab->Photo));
          }
          s = t;
        }
        while(t != NULL);
      }

      MA_FreeEMailStruct(email);
    }
  }

  LEAVE();
}
///

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
  Object *result = NULL;
  struct TagItem *tags = inittags(msg);
  struct TagItem *tag;
  struct ReadMailData *rmData;
  LONG hgVertWeight = 5;
  LONG tgVertWeight = 100;
  LONG agVertWeight = 1;

  ENTER();

  // get eventually set attributes first
  while((tag = NextTagItem((APTR)&tags)) != NULL)
  {
    switch(tag->ti_Tag)
    {
      case ATTR(HGVertWeight): hgVertWeight = tag->ti_Data; break;
      case ATTR(TGVertWeight): tgVertWeight = tag->ti_Data; break;
    }
  }

  // allocate the readMailData structure
  if((rmData = AllocPrivateRMData(NULL, 0)) != NULL)
  {
    Object *headerGroup;
    Object *headerListview;
    Object *headerList;
    Object *senderImageGroup;
    Object *senderImageSpace;
    Object *balanceObjectTop;
    Object *balanceObjectBottom;
    Object *mailBodyGroup;
    Object *mailTextObject;
    Object *textEditScrollbar;
    Object *scrolledAttachmentGroup;
    Object *attachmentGroup;

    // set some default values for a new readMailGroup
    rmData->headerMode = C->ShowHeader;
    rmData->senderInfoMode = C->ShowSenderInfo;
    rmData->wrapHeaders = C->WrapHeader;
    rmData->useTextcolors = C->UseTextColorsRead;
    rmData->useTextstyles = C->UseTextStylesRead;
    rmData->useFixedFont = C->FixedFontEdit;

    // create the scrollbar for the TE.mcc object
    textEditScrollbar = ScrollbarObject, End;

    // create a balance object we can use between our texteditor
    // and the attachment display
    balanceObjectBottom = NBalanceObject,
      MUIA_ObjectID, MAKE_ID('B','0','0','3'),
      MUIA_Balance_Quiet, TRUE,
    End;

    // create the scrolled group for our attachment display
    scrolledAttachmentGroup = HGroup,
      MUIA_VertWeight, agVertWeight,
      Child, VGroup,
        Child, TextObject,
          MUIA_Font,          MUIV_Font_Tiny,
          MUIA_Text_SetMax,   TRUE,
          MUIA_Text_Contents, tr(MSG_MA_ATTACHMENTS),
          MUIA_Text_PreParse, "\033b",
          MUIA_Text_Copy,     FALSE,
        End,
        Child, VSpace(0),
      End,
      Child, ScrollgroupObject,
        MUIA_Scrollgroup_FreeHoriz, FALSE,
        MUIA_Scrollgroup_AutoBars, TRUE,
        MUIA_Scrollgroup_Contents, attachmentGroup = AttachmentGroupObject,
        End,
      End,
    End;

    // create the readmailgroup obj
    obj = DoSuperNew(cl, obj,

            MUIA_Group_Horiz, FALSE,
            GroupSpacing(0),
            Child, headerGroup = HGroup,
              GroupSpacing(0),
              MUIA_VertWeight, hgVertWeight,
              MUIA_ShowMe,     rmData->headerMode != HM_NOHEADER,
              Child, headerListview = NListviewObject,
                MUIA_NListview_NList, headerList = HeaderListObject,
                  MUIA_HeaderList_ReadMailData, rmData,
                End,
              End,
              Child, senderImageGroup = VGroup,
                GroupSpacing(0),
                InnerSpacing(0,0),
                MUIA_CycleChain,    FALSE,
                MUIA_ShowMe,        FALSE,
                MUIA_Weight,        1,
                Child, senderImageSpace = RectangleObject,
                  MUIA_Weight, 1,
                End,
              End,
            End,
            Child, balanceObjectTop = NBalanceObject,
              MUIA_ObjectID, MAKE_ID('B','0','0','4'),
              MUIA_ShowMe, rmData->headerMode != HM_NOHEADER,
              MUIA_Balance_Quiet, TRUE,
            End,
            Child, mailBodyGroup = VGroup,
              MUIA_VertWeight, tgVertWeight,
              GroupSpacing(0),
              Child, HGroup,
                GroupSpacing(0),
                Child, mailTextObject = MailTextEditObject,
                  InputListFrame,
                  MUIA_TextEditor_Slider,               textEditScrollbar,
                  MUIA_TextEditor_FixedFont,            rmData->useFixedFont,
                  MUIA_TextEditor_DoubleClickHook,      &TextEditDoubleClickHook,
                  MUIA_TextEditor_ImportHook,           MUIV_TextEditor_ImportHook_Plain,
                  MUIA_TextEditor_ExportHook,           MUIV_TextEditor_ExportHook_Plain,
                  MUIA_TextEditor_ReadOnly,             TRUE,
                  MUIA_TextEditor_AutoClip,             C->AutoClip,
                  MUIA_TextEditor_ActiveObjectOnClick,  TRUE,
                  MUIA_CycleChain,                      TRUE,
                End,
                Child, textEditScrollbar,
              End,
            End,

            TAG_MORE, inittags(msg));

    // check if all necessary data was created
    if(obj != NULL &&
       textEditScrollbar != NULL &&
       attachmentGroup != NULL)
    {
      GETDATA;

      // copy back the temporary object pointers
      data->readMailData = rmData;
      data->textEditScrollbar = textEditScrollbar;
      data->scrolledAttachmentGroup = scrolledAttachmentGroup;
      data->attachmentGroup = attachmentGroup;
      data->headerGroup = headerGroup;
      data->headerListview = headerListview;
      data->headerList = headerList;
      data->senderImageGroup = senderImageGroup;
      data->senderImageSpace = senderImageSpace;
      data->balanceObjectTop = balanceObjectTop;
      data->balanceObjectBottom = balanceObjectBottom;
      data->mailBodyGroup = mailBodyGroup;
      data->mailTextObject = mailTextObject;

      // prepare the senderInfoHeader list
      NewMinList(&data->senderInfoHeaders);

      // place our data in the node and add it to the readMailDataList
      rmData->readMailGroup = obj;
      AddTail((struct List *)&G->readMailDataList, (struct Node *)data->readMailData);

      // now we connect some notifies.
      DoMethod(data->headerList, MUIM_Notify, MUIA_NList_DoubleClick, MUIV_EveryTime, obj, 1, METHOD(HeaderListDoubleClicked));

      result = obj;
    }
    else
      FreeSysObject(ASOT_NODE, rmData);
  }

  RETURN((IPTR)result);
  return (IPTR)result;
}
///
/// OVERLOAD(OM_DISPOSE)
OVERLOAD(OM_DISPOSE)
{
  GETDATA;
  ULONG result;
  ENTER();

  // clear the senderInfoHeaders
  ClearHeaderList(&data->senderInfoHeaders);

  // free the readMailData pointer
  if(data->readMailData != NULL)
  {
    // Remove our readWindowNode and free it afterwards
    Remove((struct Node *)data->readMailData);
    // don't use FreePrivateRMData()!
    FreeSysObject(ASOT_NODE, data->readMailData);
    data->readMailData = NULL;
  }

  // Dispose the attachmentgroup object in case it isn't
  // a child of this readmailgroup
  if(data->activeAttachmentGroup == FALSE)
  {
    MUI_DisposeObject(data->balanceObjectBottom);
    MUI_DisposeObject(data->scrolledAttachmentGroup);
    data->balanceObjectBottom = NULL;
    data->scrolledAttachmentGroup = NULL;
    data->attachmentGroup = NULL;
  }

  // signal the super class to dipose as well
  result = DoSuperMethodA(cl, obj, msg);

  RETURN(result);
  return result;
}

///
/// OVERLOAD(OM_GET)
OVERLOAD(OM_GET)
{
  GETDATA;
  IPTR *store = ((struct opGet *)msg)->opg_Storage;

  switch(((struct opGet *)msg)->opg_AttrID)
  {
    case ATTR(HGVertWeight) : *store = xget(data->headerGroup, MUIA_VertWeight); return TRUE;
    case ATTR(TGVertWeight) : *store = xget(data->mailBodyGroup, MUIA_VertWeight); return TRUE;
    case ATTR(ReadMailData) : *store = (ULONG)data->readMailData; return TRUE;
    case ATTR(DefaultObject): *store = (ULONG)data->mailTextObject; return TRUE;
    case ATTR(ActiveObject):
    {
      Object *actobj = (Object *)xget(_win(obj), MUIA_Window_ActiveObject);

      if(actobj == data->headerListview || actobj == data->headerList || actobj == data->mailTextObject)
        *store = (ULONG)actobj;
      else
        *store = (ULONG)NULL;

      return TRUE;
    }
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
      case ATTR(HGVertWeight):
      {
        set(data->headerGroup, MUIA_VertWeight, tag->ti_Data);

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;

      case ATTR(TGVertWeight):
      {
        set(data->mailBodyGroup, MUIA_VertWeight, tag->ti_Data);

        // make the superMethod call ignore those tags
        tag->ti_Tag = TAG_IGNORE;
      }
      break;
    }
  }

  return DoSuperMethodA(cl, obj, msg);
}

///
/// OVERLOAD(MUIM_ContextMenuBuild)
OVERLOAD(MUIM_ContextMenuBuild)
{
  GETDATA;
  struct MUIP_ContextMenuBuild *mb = (struct MUIP_ContextMenuBuild *)msg;
  struct ReadMailData *rmData = data->readMailData;
  BOOL hasContent = data->hasContent;

  ENTER();

  // dispose the old contextMenu if it still exists
  if(data->contextMenu != NULL)
  {
    MUI_DisposeObject(data->contextMenu);
    data->contextMenu = NULL;
  }

  // generate a context menu title now
  if(_isinobject(data->headerGroup, mb->mx, mb->my))
  {
    strlcpy(data->menuTitle, tr(MSG_RE_HEADERDISPLAY), sizeof(data->menuTitle));

    data->contextMenu = MenustripObject,
      Child, MenuObjectT(data->menuTitle),
        Child, MenuitemCheck(tr(MSG_RE_ShortHeaders), NULL, hasContent, rmData->headerMode==HM_SHORTHEADER, FALSE, 0x05, RMEN_HSHORT),
        Child, MenuitemCheck(tr(MSG_RE_FullHeaders),  NULL, hasContent, rmData->headerMode==HM_FULLHEADER,  FALSE, 0x03, RMEN_HFULL),
        Child, MenuBarLabel,
        Child, MenuitemCheck(tr(MSG_RE_NoSInfo),      NULL, hasContent, rmData->senderInfoMode==SIM_OFF,    FALSE, 0xE0, RMEN_SNONE),
        Child, MenuitemCheck(tr(MSG_RE_SInfo),        NULL, hasContent, rmData->senderInfoMode==SIM_DATA,   FALSE, 0xD0, RMEN_SDATA),
        Child, MenuitemCheck(tr(MSG_RE_SInfoImage),   NULL, hasContent, rmData->senderInfoMode==SIM_ALL,    FALSE, 0x90, RMEN_SFULL),
        Child, MenuitemCheck(tr(MSG_RE_SImageOnly),   NULL, hasContent, rmData->senderInfoMode==SIM_IMAGE,  FALSE, 0x70, RMEN_SIMAGE),
        Child, MenuBarLabel,
        Child, MenuitemCheck(tr(MSG_RE_WrapHeader),   NULL, hasContent, rmData->wrapHeaders, TRUE, 0, RMEN_WRAPH),
      End,
    End;
  }
  else if(rmData->mail != NULL)
  {
    struct Mail *mail = rmData->mail;
    BOOL isRealMail = !isVirtualMail(mail);
    BOOL hasAttach = data->activeAttachmentGroup;
    BOOL hasPGPKey = rmData->hasPGPKey;
    BOOL hasPGPSig = (hasPGPSOldFlag(rmData) || hasPGPSMimeFlag(rmData));
    BOOL isPGPEnc = isRealMail && (hasPGPEMimeFlag(rmData) || hasPGPEOldFlag(rmData));

    if(hasContent == TRUE)
    {
      snprintf(data->menuTitle, sizeof(data->menuTitle), "%s: ", tr(MSG_Subject));
      strlcat(data->menuTitle, rmData->mail->Subject, 30-strlen(data->menuTitle) > 0 ? 30-strlen(data->menuTitle) : 0);
      strlcat(data->menuTitle, "...", sizeof(data->menuTitle));
    }
    else
      strlcpy(data->menuTitle, tr(MSG_RE_MAILDETAILS), sizeof(data->menuTitle));

    data->contextMenu = MenustripObject,
      Child, MenuObjectT(data->menuTitle),
        Child, Menuitem(tr(MSG_MA_MREPLY),   NULL, hasContent, FALSE, RMEN_REPLY),
        Child, MenuitemObject,
          MUIA_Menuitem_Title, tr(MSG_MA_MFORWARD),
          MUIA_Menuitem_CopyStrings, FALSE,
          MUIA_Menuitem_Enabled, hasContent,
          Child, Menuitem(tr(MSG_MA_MFORWARD_ATTACH), NULL, hasContent, FALSE, RMEN_FORWARD_ATTACH),
          Child, Menuitem(tr(MSG_MA_MFORWARD_INLINE), NULL, hasContent, FALSE, RMEN_FORWARD_INLINE),
        End,
        Child, Menuitem(tr(MSG_MA_MMOVE),    NULL, hasContent && isRealMail, FALSE, RMEN_MOVE),
        Child, Menuitem(tr(MSG_MA_MCOPY),    NULL, hasContent, FALSE, RMEN_COPY),
        Child, MenuBarLabel,
        Child, Menuitem(tr(MSG_RE_MDisplay), NULL, hasContent, FALSE, RMEN_DISPLAY),
        Child, Menuitem(tr(MSG_MA_Save),     NULL, hasContent, FALSE, RMEN_SAVE),
        Child, Menuitem(tr(MSG_Print),       NULL, hasContent, FALSE, RMEN_PRINT),
        Child, Menuitem(tr(MSG_MA_MDelete),  NULL, hasContent && isRealMail, TRUE,  RMEN_DELETE),
        Child, MenuBarLabel,
        Child, Menuitem(tr(MSG_RE_SEARCH),   NULL, hasContent, FALSE,  RMEN_SEARCH),
        Child, Menuitem(tr(MSG_RE_SEARCH_AGAIN), NULL, hasContent, FALSE,  RMEN_SEARCHAGAIN),
        Child, MenuBarLabel,
        Child, MenuitemObject,
          MUIA_Menuitem_Title, tr(MSG_Attachments),
          MUIA_Menuitem_CopyStrings, FALSE,
          MUIA_Menuitem_Enabled, hasContent && hasAttach,
          Child, Menuitem(tr(MSG_RE_SaveAll), NULL, hasContent && hasAttach, FALSE, RMEN_DETACH),
          Child, Menuitem(tr(MSG_MA_DELETEATT), NULL, hasContent && isRealMail && hasAttach, FALSE, RMEN_DELETEATT),
        End,
        Child, MenuBarLabel,
        Child, MenuitemObject,
          MUIA_Menuitem_Title, "PGP",
          MUIA_Menuitem_CopyStrings, FALSE,
          MUIA_Menuitem_Enabled, hasContent && (hasPGPKey || hasPGPSig || isPGPEnc),
          Child, Menuitem(tr(MSG_RE_ExtractKey),    NULL, hasPGPKey, FALSE, RMEN_EXTKEY),
          Child, Menuitem(tr(MSG_RE_SigCheck),      NULL, hasPGPSig, FALSE, RMEN_CHKSIG),
          Child, Menuitem(tr(MSG_RE_SaveDecrypted), NULL, isPGPEnc, FALSE, RMEN_SAVEDEC),
        End,
        Child, MenuBarLabel,
        Child, MenuitemCheck(tr(MSG_RE_FixedFont),  NULL, hasContent, rmData->useFixedFont, TRUE, 0, RMEN_FFONT),
        Child, MenuitemCheck(tr(MSG_RE_TEXTCOLORS), NULL, hasContent, rmData->useTextcolors, TRUE, 0, RMEN_TCOLOR),
        Child, MenuitemCheck(tr(MSG_RE_Textstyles), NULL, hasContent, rmData->useTextstyles, TRUE, 0, RMEN_TSTYLE),
      End,
    End;
  }

  RETURN((IPTR)data->contextMenu);
  return (IPTR)data->contextMenu;
}
///
/// OVERLOAD(MUIM_ContextMenuChoice)
OVERLOAD(MUIM_ContextMenuChoice)
{
  GETDATA;
  struct MUIP_ContextMenuChoice *m = (struct MUIP_ContextMenuChoice *)msg;
  struct ReadMailData *rmData = data->readMailData;
  BOOL updateHeader = FALSE;
  BOOL updateText = FALSE;
  BOOL checked = xget(m->item, MUIA_Menuitem_Checked);
  IPTR result = 0;

  switch(xget(m->item, MUIA_UserData))
  {
    case RMEN_REPLY:          DoMethod(_app(obj), MUIM_CallHook, &MA_NewMessageHook, NMM_REPLY, 0); break;
    case RMEN_FORWARD_ATTACH: DoMethod(_app(obj), MUIM_CallHook, &MA_NewMessageHook, NMM_FORWARD_ATTACH, 0); break;
    case RMEN_FORWARD_INLINE: DoMethod(_app(obj), MUIM_CallHook, &MA_NewMessageHook, NMM_FORWARD_INLINE, 0); break;
    case RMEN_MOVE:           DoMethod(_app(obj), MUIM_CallHook, &MA_MoveMessageHook); break;
    case RMEN_COPY:           DoMethod(_app(obj), MUIM_CallHook, &MA_CopyMessageHook); break;
    case RMEN_DISPLAY:        DoMethod(obj, METHOD(DisplayMailRequest)); break;
    case RMEN_SAVE:           DoMethod(obj, METHOD(SaveMailRequest)); break;
    case RMEN_PRINT:          DoMethod(obj, METHOD(PrintMailRequest)); break;
    case RMEN_DELETE:         DoMethod(obj, METHOD(DeleteMail)); break;
    case RMEN_DETACH:         DoMethod(obj, METHOD(SaveAllAttachments)); break;
    case RMEN_DELETEATT:      DoMethod(obj, METHOD(DeleteAttachmentsRequest)); break;
    case RMEN_EXTKEY:         DoMethod(obj, METHOD(ExtractPGPKey)); break;
    case RMEN_CHKSIG:         DoMethod(obj, METHOD(CheckPGPSignature), TRUE); break;
    case RMEN_SAVEDEC:        DoMethod(obj, METHOD(SaveDecryptedMail)); break;
    case RMEN_SEARCH:         DoMethod(obj, METHOD(Search), MUIF_NONE); break;
    case RMEN_SEARCHAGAIN:    DoMethod(obj, METHOD(Search), MUIF_ReadMailGroup_Search_Again); break;

    // now we check the checkmarks of the
    // context-menu
    case RMEN_HSHORT:   rmData->headerMode = HM_SHORTHEADER; updateHeader = TRUE; break;
    case RMEN_HFULL:    rmData->headerMode = HM_FULLHEADER; updateHeader = TRUE; break;
    case RMEN_SNONE:    rmData->senderInfoMode = SIM_OFF; updateHeader = TRUE; break;
    case RMEN_SDATA:    rmData->senderInfoMode = SIM_DATA; updateHeader = TRUE; break;
    case RMEN_SFULL:    rmData->senderInfoMode = SIM_ALL; updateHeader = TRUE; break;
    case RMEN_SIMAGE:   rmData->senderInfoMode = SIM_IMAGE; updateHeader = TRUE; break;
    case RMEN_WRAPH:    rmData->wrapHeaders = checked; updateHeader = TRUE; break;
    case RMEN_FFONT:    rmData->useFixedFont = checked; updateText = TRUE; break;
    case RMEN_TCOLOR:   rmData->useTextcolors = checked; updateText = TRUE; break;
    case RMEN_TSTYLE:   rmData->useTextstyles = checked; updateText = TRUE; break;

    default:
      result = DoSuperMethodA(cl, obj, msg);
  }

  if(updateText == TRUE)
    DoMethod(obj, METHOD(ReadMail), rmData->mail, MUIF_ReadMailGroup_ReadMail_UpdateOnly|MUIF_ReadMailGroup_ReadMail_UpdateTextOnly);
  else if(updateHeader == TRUE)
    DoMethod(obj, METHOD(UpdateHeaderDisplay), MUIF_ReadMailGroup_ReadMail_UpdateOnly);

  RETURN(result);
  return result;
}
///

/* Public Methods */
/// DECLARE(Clear)
DECLARE(Clear) // ULONG flags
{
  GETDATA;

  ENTER();

  if(data->hasContent == TRUE)
  {
    // clear all relevant GUI elements
    DoMethod(data->headerList, MUIM_NList_Clear);

    if(hasKeepTextFlag(msg->flags) == FALSE)
      set(data->mailTextObject, MUIA_TextEditor_Contents, "");

    // cleanup the senderInfoHeaders list
    ClearHeaderList(&data->senderInfoHeaders);

    // cleanup the SenderImage field as well
    if(DoMethod(data->senderImageGroup, MUIM_Group_InitChange))
    {
      if(data->senderImage != NULL)
      {
        DoMethod(data->senderImageGroup, OM_REMMEMBER, data->senderImage);
        MUI_DisposeObject(data->senderImage);
        data->senderImage = NULL;
      }

      DoMethod(data->senderImageGroup, MUIM_Group_ExitChange);
    }

    set(data->senderImageGroup, MUIA_ShowMe, FALSE);

    // hide the attachmentGroup
    if(hasKeepAttachmentGroupFlag(msg->flags) == FALSE)
      HideAttachmentGroup(data);
  }

  CleanupReadMailData(data->readMailData, FALSE);

  data->hasContent = FALSE;

  RETURN(0);
  return 0;
}

///
/// DECLARE(ReadMail)
DECLARE(ReadMail) // struct Mail *mail, ULONG flags
{
  GETDATA;
  struct Mail *mail = msg->mail;
  struct Folder *folder = mail->Folder;
  struct ReadMailData *rmData = data->readMailData;
  BOOL result = FALSE; // error per default

  ENTER();

  // before we actually start loading data into our readmailGroup
  // we have to make sure we didn't actually have something displayed
  // which should get freed first
  if(!hasUpdateTextOnlyFlag(msg->flags))
  {
    DoMethod(obj, METHOD(Clear), MUIF_ReadMailGroup_Clear_KeepAttachmentGroup |
                                 (hasUpdateOnlyFlag(msg->flags) ? MUIF_ReadMailGroup_Clear_KeepText : 0));
  }

  // set the passed mail as the current mail read by our ReadMailData
  // structure
  rmData->mail = mail;
  setFlag(rmData->parseFlags, PM_ALL);

  // load the message now
  if(RE_LoadMessage(rmData) == TRUE)
  {
    struct BusyNode *busy;
    char *cmsg;

    busy = BusyBegin(BUSY_TEXT);
    BusyText(busy, tr(MSG_BusyDisplaying), "");

    // now read in the Mail in a temporary buffer
    if((cmsg = RE_ReadInMessage(rmData, RIM_READ)) != NULL)
    {
      char *body;

      // the first operation should be: check if the mail is a multipart mail and if so we tell
      // our attachment group about it and read the partlist or otherwise a previously opened
      // attachmentgroup may still hold some references to our already deleted parts
      if(isMultiPartMail(mail))
      {
        if(DoMethod(data->attachmentGroup, MUIM_AttachmentGroup_Refresh, rmData->firstPart) > 0)
        {
          // make sure we show the attachmentGroup
          ShowAttachmentGroup(data);
        }
        else
        {
          // make sure the attachmentGroup is hidden
          HideAttachmentGroup(data);

          // if this mail was/is a multipart mail but no valid part was
          // found we can remove the multipart flag again
          if(isMP_MixedMail(mail))
          {
            // clear the multipart/mixed flag
            clearFlag(mail->mflags, MFLAG_MP_MIXED);

            // update the status bar of an eventually existing read window.
            if(rmData->readWindow != NULL)
              DoMethod(rmData->readWindow, MUIM_ReadWindow_UpdateStatusBar);

            // if the mail is no virtual mail we can also
            // refresh the maillist depending information
            if(!isVirtualMail(mail))
            {
              // flag folder as modified and redraw the mail list entry
              setFlag(mail->Folder->Flags, FOFL_MODIFY);
              DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_RedrawMail, mail);
            }
          }
        }
      }
      else
        HideAttachmentGroup(data);

      // make sure the header display is also updated correctly.
      if(hasUpdateTextOnlyFlag(msg->flags) == FALSE)
        DoMethod(obj, METHOD(UpdateHeaderDisplay), msg->flags);

      // before we can put the message body into the TextEditor, we have to preparse the text and
      // try to set some styles, as we don't use the buggy import hooks of TextEditor anymore and
      // are more powerful this way.
      if(rmData->useTextstyles == TRUE || rmData->useTextcolors == TRUE)
        body = ParseEmailText(cmsg, TRUE, rmData->useTextstyles, rmData->useTextcolors);
      else
        body = cmsg;

      xset(data->mailTextObject, MUIA_TextEditor_FixedFont, rmData->useFixedFont,
                                 MUIA_TextEditor_Contents,  body);

      // free the parsed text afterwards as the texteditor has copied it anyway.
      if(rmData->useTextstyles == TRUE || rmData->useTextcolors == TRUE)
        dstrfree(body);

      dstrfree(cmsg);

      // start the macro
      if(rmData->readWindow != NULL)
      {
        char numStr[SIZE_SMALL];

        snprintf(numStr, sizeof(numStr), "%d", (int)xget(rmData->readWindow, MUIA_ReadWindow_Num));
        DoMethod(_app(obj), MUIM_YAMApplication_StartMacro, MACRO_READ, numStr);
      }
      else
        DoMethod(_app(obj), MUIM_YAMApplication_StartMacro, MACRO_READ, "-1");

      // if the displayed mail isn't a virtual one we flag it as read now
      if(!isVirtualMail(mail) &&
         (hasStatusNew(mail) || !hasStatusRead(mail)))
      {
        // first, depending on the PGP status we either check an existing
        // PGP signature or not, but only for new mails. A second check must
        // be triggered by the user via the menus.
        // This has to be done before we eventually set the mail state to "read"
        // which will in turn rename the mail file, which will make PGP fail,
        // because it cannot read the desired file anymore because of the delayed
        // status change.
        if((hasPGPSOldFlag(rmData) || hasPGPSMimeFlag(rmData))
           && hasStatusNew(mail))
        {
          DoMethod(obj, METHOD(CheckPGPSignature), FALSE);
        }

        // second, depending on the local delayedStatusChange and the global
        // configuration settings for the mail status change interval we either
        // start the timer that will change the mail status to read after a
        // specified time has passed or we change it immediatley here
        if(hasStatusChangeDelayFlag(msg->flags) &&
           C->StatusChangeDelayOn == TRUE && C->StatusChangeDelay > 0)
        {
          // start the timer event. Please note that the timer event might be
          // canceled by the MA_ChangeSelected() function when the next mail
          // will be selected.
          RestartTimer(TIMER_READSTATUSUPDATE, 0, (C->StatusChangeDelay-500)*1000, FALSE);
        }
        else
        {
          if(!isOutgoingFolder(mail->Folder) && !isDraftsFolder(mail->Folder))
          {
            // set mail status to OLD in any non-outgoing folder
            setStatusToRead(mail);
            DisplayStatistics(folder, TRUE);
          }
        }

        // check for any MDN and allow to reply to it.
        if(isSendMDNMail(mail))
          RE_ProcessMDN(MDN_MODE_DISPLAY, mail, FALSE, FALSE, _win(obj));
      }

      // everything worked out fine so lets return it
      result = TRUE;
    }
    else
    {
      char mailfile[SIZE_PATHFILE];

      GetMailFile(mailfile, sizeof(mailfile), NULL, mail);
      ER_NewError(tr(MSG_ER_ErrorReadMailfile), mailfile);
    }

    BusyEnd(busy);
  }
  else
  {
    // check first if the mail file exists and if not we have to exit with an error
    if(FileExists(mail->MailFile) == FALSE)
      ER_NewError(tr(MSG_ER_CantOpenFile), mail->MailFile);
  }

  // make sure we know that there is some content
  data->hasContent = TRUE;

  RETURN(result);
  return result;
}

///
/// DECLARE(UpdateHeaderDisplay)
DECLARE(UpdateHeaderDisplay) // ULONG flags
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  struct Person *from = &rmData->mail->From;
  struct ABookNode *ab = NULL;
  struct ABookNode abtmpl;
  BOOL foundIdentity;
  BOOL dispheader;
  int hits;

  ENTER();

  // make sure the headerList is cleared if necessary
  if(data->hasContent == TRUE)
    DoMethod(data->headerList, MUIM_NList_Clear);

  // then we check whether we should disable the headerList display
  // or not.
  dispheader = (rmData->headerMode != HM_NOHEADER);
  set(data->headerGroup, MUIA_ShowMe, dispheader);
  set(data->balanceObjectTop, MUIA_ShowMe, dispheader);

  set(data->headerList, MUIA_NList_Quiet, TRUE);

  // we first go through the headerList of our first Part, which should in fact
  // be the headerPart
  if(dispheader == TRUE && rmData->firstPart != NULL && rmData->firstPart->headerList != NULL)
  {
    struct HeaderNode *hdrNode;

    // Now we process the read headers to set all flags accordingly
    IterateList(rmData->firstPart->headerList, struct HeaderNode *, hdrNode)
    {
      // now we use MatchNoCase() to find out if we should include that headerNode
      // in our headerList or not
      if(rmData->headerMode == HM_SHORTHEADER)
        dispheader = MatchNoCase(hdrNode->name, C->ShortHeaders);
      else
        dispheader = (rmData->headerMode == HM_FULLHEADER);

      if(dispheader == TRUE)
      {
        // we simply insert the whole headerNode and split the display later in our HeaderDisplayHook
        DoMethod(data->headerList, MUIM_NList_InsertSingleWrap, hdrNode,
                                   MUIV_NList_Insert_Sorted,
                                   rmData->wrapHeaders ? WRAPCOL1 : NOWRAP, ALIGN_LEFT);
      }
    }
  }

  // we search in the address for the matching entry by just comparing
  // the email addresses or otherwise we can't be certain that the found
  // addressbook entry really belongs to the sender address of the mail.
  hits = SearchABook(&G->abook, from->Address, ASM_ADDRESS|ASM_USER, &ab);

  // extract the realname/email address and
  // X-SenderInfo information from the mail into a temporary ABEntry
  ExtractSenderInfo(rmData->mail, &abtmpl);

  // we have to search through our identities and
  // if the email address matches the from address
  // we simply reuse that information
  if(FindUserIdentityByAddress(&C->userIdentityList, from->Address) != NULL)
  {
    // if there is no addressbook entry for the user
    // we use the template one but erase the photo link
    if(ab == NULL)
    {
      ab = &abtmpl;
      *ab->Photo = '\0';
    }

    foundIdentity = TRUE;
  }
  else
    foundIdentity = FALSE;

  if(foundIdentity == FALSE)
  {
    if(ab != NULL)
      RE_UpdateSenderInfo(ab, &abtmpl);
    else
    {
      if(!hasUpdateOnlyFlag(msg->flags) &&
         C->AddToAddrbook > 0)
      {
        if((ab = RE_AddToAddrbook(_win(obj), &abtmpl)) == NULL)
        {
          ab = &abtmpl;
          *ab->Photo = '\0';
        }
      }
      else
      {
        ab = &abtmpl;
        *ab->Photo = '\0';
      }
    }
  }

  if(rmData->senderInfoMode != SIM_OFF)
  {
    if(rmData->senderInfoMode != SIM_IMAGE)
    {
      if(hits == 1 || ab->type == ABNT_LIST)
      {
        struct HeaderNode *newNode;
        char dateStr[SIZE_SMALL];

        // make sure we cleaned up the senderInfoHeader List beforehand
        ClearHeaderList(&data->senderInfoHeaders);

        if(*ab->RealName != '\0' && (newNode = AllocHeaderNode()) != NULL)
        {
          dstrcpy(&newNode->name, MUIX_I);
          dstrcat(&newNode->name, StripUnderscore(tr(MSG_EA_RealName)));
          dstrcpy(&newNode->content, ab->RealName);
          AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
          DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Sorted);
        }

        if(*ab->Street != '\0' && (newNode = AllocHeaderNode()) != NULL)
        {
          dstrcpy(&newNode->name, MUIX_I);
          dstrcat(&newNode->name, StripUnderscore(tr(MSG_EA_Street)));
          dstrcpy(&newNode->content, ab->Street);
          AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
          DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Sorted);
        }

        if(*ab->City != '\0' && (newNode = AllocHeaderNode()) != NULL)
        {
          dstrcpy(&newNode->name, MUIX_I);
          dstrcat(&newNode->name, StripUnderscore(tr(MSG_EA_City)));
          dstrcpy(&newNode->content, ab->City);
          AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
          DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Sorted);
        }

        if(*ab->Country != '\0' && (newNode = AllocHeaderNode()) != NULL)
        {
          dstrcpy(&newNode->name, MUIX_I);
          dstrcat(&newNode->name, StripUnderscore(tr(MSG_EA_Country)));
          dstrcpy(&newNode->content, ab->Country);
          AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
          DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Sorted);
        }

        if(*ab->Phone != '\0' && (newNode = AllocHeaderNode()) != NULL)
        {
          dstrcpy(&newNode->name, MUIX_I);
          dstrcat(&newNode->name, StripUnderscore(tr(MSG_EA_Phone)));
          dstrcpy(&newNode->content, ab->Phone);
          AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
          DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Sorted);
        }

        if(BirthdayToString(ab->Birthday, dateStr, sizeof(dateStr)) == TRUE && (newNode = AllocHeaderNode()) != NULL)
        {
          dstrcpy(&newNode->name, MUIX_I);
          dstrcat(&newNode->name, StripUnderscore(tr(MSG_EA_DOB)));
          dstrcpy(&newNode->content, dateStr);
          AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
          DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Sorted);
        }

        if(*ab->Comment != '\0' && (newNode = AllocHeaderNode()) != NULL)
        {
          dstrcpy(&newNode->name, MUIX_I);
          dstrcat(&newNode->name, StripUnderscore(tr(MSG_EA_Description)));
          dstrcpy(&newNode->content, ab->Comment);
          AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
          DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Sorted);
        }

        if(*ab->Homepage != '\0' && (newNode = AllocHeaderNode()) != NULL)
        {
          dstrcpy(&newNode->name, MUIX_I);
          dstrcat(&newNode->name, StripUnderscore(tr(MSG_EA_Homepage)));
          dstrcpy(&newNode->content, ab->Homepage);
          AddTail((struct List *)&data->senderInfoHeaders, (struct Node *)newNode);
          DoMethod(data->headerList, MUIM_NList_InsertSingle, newNode, MUIV_NList_Insert_Sorted);
        }
      }
    }

    // now we check if that addressbook entry has a specified
    // photo entry and if so we go and show that sender picture
    if((rmData->senderInfoMode == SIM_ALL || rmData->senderInfoMode == SIM_IMAGE) &&
       (data->senderImage != NULL || FileExists(ab->Photo)))
    {
      if(DoMethod(data->senderImageGroup, MUIM_Group_InitChange))
      {
        if(data->senderImage != NULL)
        {
          DoMethod(data->senderImageGroup, OM_REMMEMBER, data->senderImage);
          MUI_DisposeObject(data->senderImage);
          data->senderImage = NULL;
        }

        if(FileExists(ab->Photo) &&
           (data->senderImage = ImageAreaObject,
                                  MUIA_Weight,                100,
                                  MUIA_ImageArea_ID,          from->Address,
                                  MUIA_ImageArea_Filename,    ab->Photo,
                                  MUIA_ImageArea_MaxHeight,   64,
                                  MUIA_ImageArea_MaxWidth,    64,
                                  MUIA_ImageArea_NoMinHeight, TRUE,
                                  MUIA_ImageArea_ShowLabel,   FALSE,
                                End))
        {
          D(DBF_GUI, "SenderPicture found: %s %ld %ld", ab->Photo, xget(data->headerList, MUIA_Width), xget(data->headerList, MUIA_Height));

          DoMethod(data->senderImageGroup, OM_ADDMEMBER, data->senderImage);

          // resort the group so that the space object is at the bottom of it.
          DoMethod(data->senderImageGroup, MUIM_Group_Sort, data->senderImage,
                                                            data->senderImageSpace,
                                                            NULL);
        }

        DoMethod(data->senderImageGroup, MUIM_Group_ExitChange);
      }
    }
  }

  set(data->senderImageGroup, MUIA_ShowMe, (rmData->senderInfoMode == SIM_ALL ||
                                            rmData->senderInfoMode == SIM_IMAGE) &&
                                           (data->senderImage != NULL));

  // enable the headerList again
  set(data->headerList, MUIA_NList_Quiet, FALSE);

  RETURN(0);
  return 0;
}

///
/// DECLARE(CheckPGPSignature)
DECLARE(CheckPGPSignature) // ULONG forceRequester
{
  GETDATA;
  BOOL result = FALSE;
  struct ReadMailData *rmData = data->readMailData;

  ENTER();

  // Don't try to use PGP if it's not installed
  if(G->PGPVersion >= 0)
  {
    if(hasPGPSOldFlag(rmData) || hasPGPSMimeFlag(rmData))
    {
      char options[SIZE_LARGE];
      int error;
      struct Part *part;
      struct Part *letterPart = NULL;
      struct Part *pgpPart = NULL;

      // find the letter and signature parts
      part = rmData->firstPart;
      do
      {
        if(letterPart == NULL && part->Nr == rmData->letterPartNum)
        {
          letterPart = part;
        }
        else if(pgpPart == NULL && stricmp(part->ContentType, "application/pgp-signature") == 0)
        {
          pgpPart = part;
        }

        part = part->Next;
      }
      while(part != NULL);

      if(letterPart != NULL && pgpPart != NULL)
      {
        // decode the letter part first, otherwise PGP might want to check
        // a still encoded file which definitely will fail.
        RE_DecodePart(letterPart);

        snprintf(options, sizeof(options), (G->PGPVersion == 5) ? "%s -o %s +batchmode=1 +force +language=us" : "%s %s +bat +f +lang=en", pgpPart->Filename, letterPart->Filename);
        error = PGPCommand((G->PGPVersion == 5) ? "pgpv": "pgp", options, KEEPLOG);
        if(error > 0)
          setFlag(rmData->signedFlags, PGPS_BADSIG);

        if(error >= 0)
        {
          // running PGP was successful, but that still doesn't mean that the check itself succeeded!
          RE_GetSigFromLog(rmData, NULL);
          result = TRUE;
        }

        if(result == TRUE && (hasPGPSBadSigFlag(rmData) || msg->forceRequester == TRUE))
        {
          char buffer[SIZE_LARGE];

          strlcpy(buffer, hasPGPSBadSigFlag(rmData) ? tr(MSG_RE_BadSig) : tr(MSG_RE_GoodSig), sizeof(buffer));
          if(hasPGPSAddressFlag(rmData))
          {
            strlcat(buffer, tr(MSG_RE_SigFrom), sizeof(buffer));
            strlcat(buffer, rmData->sigAuthor, sizeof(buffer));
          }

          MUI_Request(_app(obj), _win(obj), MUIF_NONE, tr(MSG_RE_SigCheck), tr(MSG_OkayReq), buffer);
        }
      }
    }
  }

  RETURN(result);
  return result;
}

///
/// DECLARE(ExtractPGPKey)
//  Extracts public PGP key from the current message
DECLARE(ExtractPGPKey)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  struct Mail *mail = rmData->mail;
  char mailfile[SIZE_PATHFILE];
  char fullfile[SIZE_PATHFILE];
  char options[SIZE_PATHFILE];

  GetMailFile(mailfile, sizeof(mailfile), NULL, mail);
  if(StartUnpack(mailfile, fullfile, mail->Folder) != NULL)
  {
    snprintf(options, sizeof(options), (G->PGPVersion == 5) ? "-a %s +batchmode=1 +force" : "-ka %s +bat +f", fullfile);
    PGPCommand((G->PGPVersion == 5) ? "pgpk" : "pgp", options, 0);
    FinishUnpack(fullfile);
  }

  return 0;
}

///
/// DECLARE(SaveDisplay)
//  Saves current message as displayed
DECLARE(SaveDisplay) // FILE *fileHandle
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  FILE *fh = msg->fileHandle;
  char *ptr;

  if(rmData->headerMode != HM_NOHEADER)
  {
    int i;
    struct MUI_NList_GetEntryInfo res;

    fputs("\033[3m", fh);
    for(i=0;;i++)
    {
      struct HeaderNode *curNode;
      res.pos = MUIV_NList_GetEntryInfo_Line;
      res.line = i;

      DoMethod(data->headerList, MUIM_NList_GetEntryInfo, &res);

      if((curNode = (struct HeaderNode *)res.entry) != NULL)
      {
        char *name = curNode->name;
        char *content = curNode->content;

        // skip the italic style if present
        if(strncmp(name, MUIX_I, strlen(MUIX_I)) == 0)
          name += strlen(MUIX_I);

        fprintf(fh, "%s: %s\n", name, content);
      }
      else
        break;
    }
    fputs("\033[23m\n", fh);
  }

  ptr = (char *)DoMethod(data->mailTextObject, MUIM_TextEditor_ExportText);
  for(; *ptr; ptr++)
  {
    if(*ptr == '\033')
    {
      switch(*++ptr)
      {
        case 'u':
          fputs("\033[4m", fh);
        break;

        case 'b':
          fputs("\033[1m", fh);
        break;

        case 'i':
          fputs("\033[3m", fh);
        break;

        case 'n':
          fputs("\033[0m", fh);
        break;

        case 'h':
          break;

        case '[':
        {
          if(!strncmp(ptr, "[s:18]", 6))
          {
            fputs("===========================================================", fh);
            ptr += 5;
          }
          else if(!strncmp(ptr, "[s:2]", 5))
          {
            fputs("-----------------------------------------------------------", fh);
            ptr += 4;
          }
        }
        break;

        case 'p':
          while(*ptr != ']' && *ptr && *ptr != '\n')
            ptr++;
        break;
      }
    }
    else
      fputc(*ptr, fh);
  }

  return 0;
}

///
/// DECLARE(SaveDecryptedMail)
//  Saves decrypted version of a PGP message
DECLARE(SaveDecryptedMail)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  struct Mail *mail = rmData->mail;
  struct Folder *folder = mail->Folder;
  int choice;

  if(folder == NULL)
    return 0;

  if((choice = MUI_Request(_app(obj), rmData->readWindow, MUIF_NONE, tr(MSG_RE_SaveDecrypted),
                                                                     tr(MSG_RE_SaveDecGads),
                                                                     tr(MSG_RE_SaveDecReq))) != 0)
  {
    struct Compose comp;
    char mfilePath[SIZE_PATHFILE];

    memset(&comp, 0, sizeof(struct Compose));

    if(MA_NewMailFile(folder, mfilePath, sizeof(mfilePath)) == TRUE &&
       (comp.FH = fopen(mfilePath, "w")) != NULL)
    {
      struct ExtendedMail *email;

      setvbuf(comp.FH, NULL, _IOFBF, SIZE_FILEBUF);

      comp.Mode = NMM_SAVEDEC;
      comp.refMail = mail;
      if((comp.FirstPart = NewMIMEpart(NULL)) != NULL)
      {
        struct WritePart *p1 = comp.FirstPart;

        p1->Filename = rmData->firstPart->Next->Filename;
        WriteOutMessage(&comp);
        FreePartsList(p1, TRUE);
      }
      fclose(comp.FH);

      if((email = MA_ExamineMail(folder, FilePart(mfilePath), TRUE)) != NULL)
      {
        struct Mail *newmail;

        // lets set some values depending on the original message
        email->Mail.sflags = mail->sflags;
        memcpy(&email->Mail.transDate, &mail->transDate, sizeof(email->Mail.transDate));

        // add the mail to the folder now
        if((newmail = CloneMail(&email->Mail)) != NULL)
        {
          AddMailToFolder(newmail, folder);

          // if this was a compressed/encrypted folder we need to pack the mail now
          if(folder->Mode > FM_SIMPLE)
            RepackMailFile(newmail, -1, NULL);

          if(GetCurrentFolder() == folder)
            DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_NList_InsertSingle, newmail, MUIV_NList_Insert_Sorted);

          if(choice == 2)
          {
            MA_DeleteSingle(mail, DELF_UPDATE_APPICON|DELF_CHECK_CONNECTIONS);

            // erase the old pointer as this has been free()ed by MA_DeleteSingle()
            rmData->mail = NULL;

            DoMethod(rmData->readWindow, MUIM_ReadWindow_ReadMail, newmail);
          }
        }

        MA_FreeEMailStruct(email);
      }
    }
    else
    {
      ER_NewError(tr(MSG_ER_CANNOT_CREATE_MAIL_FILE), mfilePath);
    }
  }

  return 0;
}

///
/// DECLARE(SaveAllAttachments)
DECLARE(SaveAllAttachments)
{
  GETDATA;

  return DoMethod(data->attachmentGroup, MUIM_AttachmentGroup_SaveAll);
}
///
/// DECLARE(ActivateMailText)
//  sets the mailTextObject as the active object of the window
DECLARE(ActivateMailText)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;

  if(rmData->readWindow != NULL)
    set(rmData->readWindow, MUIA_Window_DefaultObject, data->mailTextObject);

  return 0;
}

///
/// DECLARE(SaveMailRequest)
//  Saves the current message or an attachment to disk
DECLARE(SaveMailRequest)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  struct Part *part;
  struct TempFile *tf;

  ENTER();

  if((part = AttachRequest(tr(MSG_RE_SaveMessage),
                           tr(MSG_RE_SelectSavePart),
                           tr(MSG_RE_SaveGad),
                           tr(MSG_Cancel), ATTREQ_SAVE|ATTREQ_MULTI, rmData)) != NULL)
  {
    struct BusyNode *busy;

    busy = BusyBegin(BUSY_TEXT);
    BusyText(busy, tr(MSG_BusyDecSaving), "");

    for(; part; part = part->NextSelected)
    {
      switch(part->Nr)
      {
        case PART_ORIGINAL:
        {
          RE_Export(rmData, rmData->readFile, "", "", 0, FALSE, FALSE, IntMimeTypeArray[MT_ME_EMAIL].ContentType);
        }
        break;

        case PART_ALLTEXT:
        {
          if((tf = OpenTempFile("w")) != NULL)
          {
            DoMethod(obj, METHOD(SaveDisplay), tf->FP);
            fclose(tf->FP);
            tf->FP = NULL;

            RE_Export(rmData, tf->Filename, "", "", 0, FALSE, FALSE, IntMimeTypeArray[MT_TX_PLAIN].ContentType);
            CloseTempFile(tf);
          }
        }
        break;

        default:
        {
          char *suggestedFileName;

          RE_DecodePart(part);

          suggestedFileName = SuggestPartFileName(part);

          RE_Export(rmData, part->Filename, "",
                    suggestedFileName, part->Nr,
                    FALSE, FALSE, part->ContentType);

          free(suggestedFileName);
        }
      }
    }

    BusyEnd(busy);
  }

  RETURN(0);
  return 0;
}

///
/// DECLARE(PrintMailRequest)
DECLARE(PrintMailRequest)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  struct Part *part;
  struct TempFile *prttmp;

  ENTER();

  if((part = AttachRequest(tr(MSG_RE_PrintMsg),
                           tr(MSG_RE_SelectPrintPart),
                           tr(MSG_RE_PrintGad),
                           tr(MSG_Cancel), ATTREQ_PRINT|ATTREQ_MULTI, rmData)) != NULL)
  {
    struct BusyNode *busy;

    busy = BusyBegin(BUSY_TEXT);
    BusyText(busy, tr(MSG_BusyDecPrinting), "");

    for(; part; part = part->NextSelected)
    {
      switch(part->Nr)
      {
        case PART_ORIGINAL:
          RE_PrintFile(rmData->readFile, _win(obj));
        break;

        case PART_ALLTEXT:
        {
          if((prttmp = OpenTempFile("w")) != NULL)
          {
            DoMethod(obj, METHOD(SaveDisplay), prttmp->FP);
            fclose(prttmp->FP);
            prttmp->FP = NULL;

            RE_PrintFile(prttmp->Filename, _win(obj));
            CloseTempFile(prttmp);
          }
        }
        break;

        default:
          RE_PrintFile(part->Filename, _win(obj));
      }
    }

    BusyEnd(busy);
  }

  RETURN(0);
  return 0;
}

///
/// DECLARE(DisplayMailRequest)
DECLARE(DisplayMailRequest)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  struct Part *part;

  ENTER();

  if((part = AttachRequest(tr(MSG_RE_DisplayMsg),
                           tr(MSG_RE_SelectDisplayPart),
                           tr(MSG_RE_DisplayGad),
                           tr(MSG_Cancel), ATTREQ_DISP|ATTREQ_MULTI, rmData)) != NULL)
  {
    struct BusyNode *busy;

    busy = BusyBegin(BUSY_TEXT);
    BusyText(busy, tr(MSG_BusyDecDisplaying), "");

    for(; part; part = part->NextSelected)
    {
      RE_DecodePart(part);

      switch(part->Nr)
      {
        case PART_ORIGINAL:
          RE_DisplayMIME(rmData->readFile, NULL, "text/plain", TRUE);
        break;

        default:
        {
          char *fileName;

          // get the suggested filename for the mail part
          fileName = SuggestPartFileName(part);

          RE_DisplayMIME(part->Filename, fileName,
                         part->ContentType, isPrintable(part));

          free(fileName);
        }
        break;
      }
    }

    BusyEnd(busy);
  }

  RETURN(0);
  return 0;
}

///
/// DECLARE(DeleteMail)
DECLARE(DeleteMail)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  struct Mail *mail = rmData->mail;
  struct Folder *folder = mail->Folder;

  ENTER();

  if(MailExists(mail, folder) == TRUE)
 {
    struct Folder *delfolder = FO_GetFolderByType(FT_TRASH, NULL);

    // delete the mail
    MA_DeleteSingle(mail, DELF_UPDATE_APPICON|DELF_CHECK_CONNECTIONS);
    AppendToLogfile(LF_NORMAL, 22, tr(MSG_LOG_Moving), 1, folder->Name, delfolder->Name);

    // erase the old pointer as this has been free()ed by MA_MoveCopy()
    rmData->mail = NULL;
  }

  RETURN(0);
  return 0;
}

///
/// DECLARE(DeleteAttachmentsRequest)
//  Removes attachments from the current message
DECLARE(DeleteAttachmentsRequest)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;
  struct Mail *mail = rmData->mail;

  ENTER();

  // remove the attchments now
  MA_RemoveAttach(mail, NULL, C->ConfirmRemoveAttachments);

  RETURN(0);
  return 0;
}

///
/// DECLARE(HeaderListDoubleClicked)
// Switches between the SHORT and FULL header view of
// the header list
DECLARE(HeaderListDoubleClicked)
{
  GETDATA;
  struct ReadMailData *rmData = data->readMailData;

  ENTER();

  if(rmData->headerMode == HM_SHORTHEADER)
    rmData->headerMode = HM_FULLHEADER;
  else
    rmData->headerMode = HM_SHORTHEADER;

  DoMethod(obj, METHOD(UpdateHeaderDisplay), MUIF_ReadMailGroup_ReadMail_UpdateOnly);

  RETURN(0);
  return 0;
}

///
/// DECLARE(Search)
DECLARE(Search) // int flags
{
  GETDATA;
  ENTER();

  if(data->searchWindow == NULL)
  {
    if((data->searchWindow = SearchTextWindowObject, End) != NULL)
    {
      // perform the search operation
      DoMethod(data->searchWindow, MUIM_SearchTextWindow_Open, data->mailTextObject);
    }
  }
  else
  {
    if(hasSearchAgainFlag(msg->flags))
      DoMethod(data->searchWindow, MUIM_SearchTextWindow_Next);
    else
      DoMethod(data->searchWindow, MUIM_SearchTextWindow_Open, data->mailTextObject);
  }

  RETURN(0);
  return 0;
}
///
/// DECLARE(ChangeHeaderMode)
DECLARE(ChangeHeaderMode) // enum HeaderMode hmode
{
  GETDATA;

  ENTER();

  if(data->readMailData->headerMode != msg->hmode)
  {
    // change the header display mode
    data->readMailData->headerMode = msg->hmode;
    // issue an update of ourself
    DoMethod(obj, METHOD(UpdateHeaderDisplay), MUIF_ReadMailGroup_ReadMail_UpdateOnly);
  }

  RETURN(0);
  return 0;
}
///
/// DECLARE(ChangeSenderInfoMode)
DECLARE(ChangeSenderInfoMode) // enum SInfoMode simode
{
  GETDATA;

  ENTER();

  if(data->readMailData->senderInfoMode != msg->simode)
  {
    // change the sender info mode
    data->readMailData->senderInfoMode = msg->simode;
    // issue an update of ourself
    DoMethod(obj, METHOD(UpdateHeaderDisplay), MUIF_ReadMailGroup_ReadMail_UpdateOnly);
  }

  RETURN(0);
  return 0;
}
///
/// DECLARE(DoEditAction)
DECLARE(DoEditAction) // enum EditAction editAction, ULONG flags
{
  GETDATA;
  Object *curObj;
  BOOL result = FALSE;

  ENTER();

  // we first check which object is currently selected
  // as the 'active' Object
  curObj = (Object *)xget(_win(obj), MUIA_Window_ActiveObject);
  if(curObj == NULL)
    curObj = (Object *)xget(_win(obj), MUIA_Window_DefaultObject);

  // check if the current Object matches any of our
  // maintained objects or if the fallback option was
  // specified.
  if(curObj != data->mailTextObject &&
     curObj != data->headerListview &&
     curObj != data->headerList &&
     hasEditActionFallbackFlag(msg->flags))
  {
    // we fallback to the mailTextObject
    curObj = data->mailTextObject;
  }

  // if we still haven't got anything selected
  // something must be extremly strange ;)
  if(curObj != NULL)
  {
    // check which action we got
    switch(msg->editAction)
    {
      case EA_COPY:
      {
        if(curObj == data->mailTextObject)
        {
          DoMethod(curObj, MUIM_TextEditor_ARexxCmd, "COPY");
          result = TRUE;
        }
        else if(curObj == data->headerListview || curObj == data->headerList)
        {
          DoMethod(data->headerList, MUIM_NList_CopyToClip, MUIV_NList_CopyToClip_Selected, 0, NULL, NULL);
          DoMethod(data->headerList, MUIM_NList_Select, MUIV_NList_Select_All, MUIV_NList_Select_Off, NULL);
          result = TRUE;
        }
      }
      break;

      case EA_SELECTALL:
      {
        if(curObj == data->mailTextObject)
        {
          DoMethod(curObj, MUIM_TextEditor_ARexxCmd, "SELECTALL");
          result = TRUE;
        }
        else if(curObj == data->headerListview || curObj == data->headerList)
        {
          DoMethod(data->headerList, MUIM_NList_Select, MUIV_NList_Select_All, MUIV_NList_Select_On, NULL);
          result = TRUE;
        }
      }
      break;

      case EA_SELECTNONE:
      {
        if(curObj == data->mailTextObject)
        {
          DoMethod(curObj, MUIM_TextEditor_ARexxCmd, "SELECTNONE");
          result = TRUE;
        }
        else if(curObj == data->headerListview || curObj == data->headerList)
        {
          DoMethod(data->headerList, MUIM_NList_Select, MUIV_NList_Select_All, MUIV_NList_Select_Off, NULL);
          result = TRUE;
        }
      }
      break;

      default:
        // nothing
        W(DBF_GUI, "edit action %ld not supported by ReadMailGroup", msg->editAction);
      break;
    }
  }
  else
    E(DBF_GUI, "no active nor default object of window: 0x%08lx", _win(obj));

  RETURN(result);
  return result;
}

///
/// DECLARE(ExportSelectedText)
DECLARE(ExportSelectedText)
{
  GETDATA;
  char *result = NULL;

  ENTER();

  result = (char *)DoMethod(data->mailTextObject, MUIM_TextEditor_ExportBlock, MUIF_NONE);

  RETURN(result);
  return (ULONG)result;
}

///
