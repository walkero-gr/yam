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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__amigaos4__)
#include <clib/alib_protos.h>
#endif

#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <libraries/iffparse.h>
#include <mui/NBalance_mcc.h>
#include <mui/NList_mcc.h>
#include <mui/NListview_mcc.h>
#include <mui/NListtree_mcc.h>
#include <mui/TextEditor_mcc.h>
#include <mui/TheBar_mcc.h>
#include <proto/codesets.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/timer.h>
#include <proto/utility.h>

#include "extrasrc.h"

#include "SDI_hook.h"

#include "YAM.h"
#include "YAM_error.h"
#include "YAM_find.h"
#include "YAM_folderconfig.h"
#include "YAM_global.h"
#include "YAM_main.h"
#include "YAM_mainFolder.h"
#include "YAM_read.h"
#include "YAM_userlist.h"
#include "YAM_utilities.h"
#include "YAM_write.h"

#include "mui/AddressBookWindow.h"
#include "mui/ClassesExtra.h"
#include "mui/InfoBar.h"
#include "mui/MainFolderListtree.h"
#include "mui/MainMailList.h"
#include "mui/MainMailListGroup.h"
#include "mui/MainWindow.h"
#include "mui/MainWindowToolbar.h"
#include "mui/QuickSearchBar.h"
#include "mui/ReadMailGroup.h"
#include "mui/ReadWindow.h"
#include "mui/WriteWindow.h"
#include "mui/YAMApplication.h"
#include "mime/base64.h"
#include "tcp/pop3.h"
#include "tcp/smtp.h"

#include "AppIcon.h"
#include "BayesFilter.h"
#include "Busy.h"
#include "Config.h"
#include "DynamicString.h"
#include "FileInfo.h"
#include "FolderList.h"
#include "HTML2Mail.h"
#include "Locale.h"
#include "Logfile.h"
#include "MailExport.h"
#include "MailList.h"
#include "MailServers.h"
#include "MethodStack.h"
#include "MimeTypes.h"
#include "MUIObjects.h"
#include "Requesters.h"
#include "Rexx.h"
#include "Threads.h"
#include "UserIdentity.h"

#include "Debug.h"

/* local protos */
static void MA_MoveCopySingle(struct Mail *mail, struct Folder *to, const char *originator, const ULONG flags);

/***************************************************************************
 Module: Main
***************************************************************************/

/*** Private functions ***/
/// MA_GetSortType
//  Calculates value for sort indicator
static ULONG MA_GetSortType(int sort)
{
  static const ULONG sort2col[8] = { 0, 4, 7, 1, 1, 3, 5, 0 };

  if(sort > 0)
    return sort2col[sort];
  else
    return sort2col[-sort] | MUIV_NList_SortTypeAdd_2Values;
}

///
/// MA_SetSortFlag
//  Sets sort indicators in message listview header
void MA_SetSortFlag(void)
{
  struct Folder *folder;

  ENTER();

  if((folder = GetCurrentFolder()) != NULL)
  {
    xset(G->MA->GUI.PG_MAILLIST,
      MUIA_NList_SortType,  MA_GetSortType(folder->Sort[0]),
      MUIA_NList_SortType2, MA_GetSortType(folder->Sort[1]));
  }

  LEAVE();
}

///
/// MA_ChangeSelected
// function which updates some mail information on the main
// window and triggers an update of the embedded read pane if required.
void MA_ChangeSelected(BOOL forceUpdate)
{
  static struct Mail *lastMail = NULL;
  struct MA_GUIData *gui = &G->MA->GUI;
  struct Mail *mail = NULL;

  ENTER();

  // get the currently active mail entry.
  DoMethod(gui->PG_MAILLIST, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &mail);

  // now we check if the previously selected mail is the same one as
  // the currently active one, then we don't have to proceed.
  if(forceUpdate == TRUE || mail != lastMail)
  {
    struct Folder *folder = GetCurrentFolder();
    ULONG numEntries;
    ULONG numSelected = 0;
    BOOL active;
    BOOL folderEnabled;

    lastMail = mail;

    // we make sure the an eventually running timer event for setting the mail
    // status of a previous mail to read is canceled beforehand
    if(C->StatusChangeDelayOn == TRUE)
      StopTimer(TIMER_READSTATUSUPDATE);

    // ask the mail list how many entries are currently available and selected
    if((numEntries = xget(gui->PG_MAILLIST, MUIA_NList_Entries)) > 0)
      DoMethod(gui->PG_MAILLIST, MUIM_NList_Select, MUIV_NList_Select_All, MUIV_NList_Select_Ask, &numSelected);

    SHOWVALUE(DBF_MAIL, numEntries);
    SHOWVALUE(DBF_MAIL, numSelected);

    // make sure the mail is displayed in our readMailGroup of the main window
    // (if enabled) - but we do only issue a timer event here so the read pane
    // is only refreshed about 100 milliseconds after the last change in the listview
    // was recognized.
    if(C->EmbeddedReadPane == TRUE)
    {
      // but before we really issue a readpaneupdate we check whether the user has
      // selected more than one mail at a time which then should clear the
      // readpane as it might have been disabled.
      if(numSelected == 1)
        RestartTimer(TIMER_READPANEUPDATE, 0, C->EmbeddedMailDelay*1000, FALSE);
      else
      {
        // make sure an already existing readpaneupdate timer is canceled in advance.
        StopTimer(TIMER_READPANEUPDATE);

        // clear the readmail group now
        DoMethod(gui->MN_EMBEDDEDREADPANE, MUIM_ReadMailGroup_Clear, MUIF_NONE);
        lastMail = NULL;
      }
    }

    // in case the currently active maillist is the mainmaillist we
    // have to save the lastactive mail ID
    if(folder != NULL && xget(gui->PG_MAILLIST, MUIA_MainMailListGroup_ActiveList) == LT_MAIN)
      folder->LastActive = xget(gui->PG_MAILLIST, MUIA_NList_Active);

    active = (mail != NULL);
    SHOWVALUE(DBF_MAIL, active);

    // now we have to make sure that all toolbar and menu items are
    // enabled and disabled according to the folder/mail status
    folderEnabled = !isGroupFolder(folder);

    // deal with the toolbar and disable/enable certain buttons
    if(gui->TO_TOOLBAR != NULL)
    {
      DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_READ,    MUIA_TheBar_Attr_Disabled, !folderEnabled || (!active && numSelected == 0));
      DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_EDIT,    MUIA_TheBar_Attr_Disabled, !folderEnabled || (!active && numSelected == 0) || isSpamFolder(folder));
      DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_MOVE,    MUIA_TheBar_Attr_Disabled, !folderEnabled || (!active && numSelected == 0));
      DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_DELETE,  MUIA_TheBar_Attr_Disabled, !folderEnabled || (!active && numSelected == 0));
      DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_GETADDR, MUIA_TheBar_Attr_Disabled, !folderEnabled || (!active && numSelected == 0));
      DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_NEWMAIL, MUIA_TheBar_Attr_Disabled, !folderEnabled);
      DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_REPLY,   MUIA_TheBar_Attr_Disabled, !folderEnabled || (!active && numSelected == 0) || isOutgoingFolder(folder) || isSpamFolder(folder));
      DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_FORWARD, MUIA_TheBar_Attr_Disabled, !folderEnabled || (!active && numSelected == 0));
      DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_FILTER,  MUIA_TheBar_Attr_Disabled, !folderEnabled || numEntries == 0);
      DoMethod(gui->TO_TOOLBAR, MUIM_MainWindowToolbar_UpdateSpamControls);
    }

    // change the menu item title of the
    // Edit item so that we either display "Edit" or "Edit as New"
    if(isDraftsFolder(folder))
      set(gui->MI_EDIT, MUIA_Menuitem_Title, tr(MSG_MA_MEDIT));
    else
      set(gui->MI_EDIT, MUIA_Menuitem_Title, tr(MSG_MA_MEDITASNEW));

    // in the following section we define which menu item should be
    // enabled or disabled. Please note that a menu item can only be part of
    // ONE of the following groups for enabling/disabling items based on
    // certain dependencies. So if there is a menu item which is part of
    // more than one group, something is definitly wrong!

    // Enable if:
    //  * the folder is enabled
    //  * NOT in the "Outgoing" folder
    //  * NOT in the "SPAM" folder
    //  * > 0 mails selected
    DoMethod(G->App, MUIM_MultiSet, MUIA_Menuitem_Enabled, folderEnabled && !isOutgoingFolder(folder) && !isSpamFolder(folder) && (active || numSelected > 0),
                                                           gui->MI_REPLY,
                                                           NULL);

    // Enable if:
    //  * the folder is enabled
    //  * NOT in the "SPAM" folder
    //  * > 0 mails selected
    DoMethod(G->App, MUIM_MultiSet, MUIA_Menuitem_Enabled, folderEnabled && !isSpamFolder(folder) && (active || numSelected > 0),
                                                           gui->MI_EDIT,
                                                           NULL);

    // Enable if:
    //  * the folder is enabled
    //  * NOT in the "Sent" folder
    //  * > 0 mails selected
    DoMethod(G->App, MUIM_MultiSet, MUIA_Menuitem_Enabled, folderEnabled && !isSentMailFolder(folder) && (active || numSelected > 0),
                                                           gui->MI_TOREAD,
                                                           gui->MI_TOUNREAD,
                                                           gui->MI_ALLTOREAD,
                                                           gui->MI_REDIRECT,
                                                           NULL);

    // Enable if:
    //  * the folder is enabled
    //  * is in the "Outgoing" Folder
    //  * > 0 mails selected
    DoMethod(G->App, MUIM_MultiSet, MUIA_Menuitem_Enabled, folderEnabled && isOutgoingFolder(folder) && (active || numSelected > 0),
                                                           gui->MI_SEND,
                                                           NULL);

    // Enable if:
    //  * the folder is enabled
    //  * > 0 mails selected
    DoMethod(G->App, MUIM_MultiSet, MUIA_Menuitem_Enabled, folderEnabled && (active || numSelected > 0),
                                                           gui->MI_READ,
                                                           gui->MI_MOVE,
                                                           gui->MI_DELETE,
                                                           gui->MI_GETADDRESS,
                                                           gui->MI_STATUS,
                                                           gui->MI_EXPMSG,
                                                           gui->MI_COPY,
                                                           gui->MI_ARCHIVE,
                                                           gui->MI_PRINT,
                                                           gui->MI_SAVE,
                                                           gui->MI_ATTACH,
                                                           gui->MI_FORWARD,
                                                           gui->MI_CHSUBJ,
                                                           gui->MI_NEXTTHREAD,
                                                           gui->MI_PREVTHREAD,
                                                           NULL);

    // Enable if:
    //  * the folder is enabled
    //  * NOT in the "Archive" folder
    //  * > 0 mails selected
    DoMethod(G->App, MUIM_MultiSet, MUIA_Menuitem_Enabled, folderEnabled && !isArchiveFolder(folder) && (active || numSelected > 0),
                                                           gui->MI_ARCHIVE,
                                                           NULL);
    // Enable if:
    //  * the folder is enabled
    //  * > 0 mails in folder
    DoMethod(G->App, MUIM_MultiSet, MUIA_Menuitem_Enabled, folderEnabled && numEntries > 0,
                                                           gui->MI_FILTER,
                                                           gui->MI_SELECT,
                                                           NULL);


    // Enable if:
    //  * the folder is enabled
    DoMethod(G->App, MUIM_MultiSet, MUIA_Menuitem_Enabled, folderEnabled,
                                                           gui->MI_UPDINDEX,
                                                           gui->MI_IMPORT,
                                                           gui->MI_EXPORT,
                                                           gui->MI_NEW,
                                                           NULL);


    // Enable if:
    //  * TOSPAM menu item exists
    //  * > 0 mails selected or the active one isn't marked as SPAM
    //  * the folder is enabled
    //  * the mail is not spam
    if(gui->MI_TOSPAM != NULL)
      set(gui->MI_TOSPAM, MUIA_Menuitem_Enabled, folderEnabled && (numSelected > 1 || (active && !hasStatusSpam(mail))));

    // Enable if:
    //  * TOHAM menu item exists
    //  * > 0 mails selected
    //  * the folder is enabled
    //  * the mail is classified as spam
    if(gui->MI_TOHAM != NULL)
      set(gui->MI_TOHAM,  MUIA_Menuitem_Enabled, folderEnabled && (numSelected > 1 || (active && hasStatusSpam(mail))));

    // Enable if:
    //  * DELSPAM menu item exists
    //  * is in the "SPAM" folder
    if(gui->MI_DELSPAM != NULL)
      set(gui->MI_DELSPAM, MUIA_Menuitem_Enabled, folderEnabled && numEntries > 0);

    // Enable if:
    //  * CHECKSPAM menu item exists
    //  * the folder is enabled
    if(gui->MI_CHECKSPAM != NULL)
      set(gui->MI_CHECKSPAM, MUIA_Menuitem_Enabled, folderEnabled && numEntries > 0);
  }

  LEAVE();
}

///
/// MA_ChangeSelectedFunc
//  User selected some message(s) in the message list
HOOKPROTONHNONP(MA_ChangeSelectedFunc, void)
{
  MA_ChangeSelected(FALSE);
}
MakeHook(MA_ChangeSelectedHook, MA_ChangeSelectedFunc);

///
/// MA_GetActiveMail
//  Returns pointers to the active message and folder
struct Mail *MA_GetActiveMail(struct Folder *forcefolder, struct Folder **folderp, LONG *activep)
{
  struct Folder *folder = forcefolder != NULL ? forcefolder : GetCurrentFolder();
  struct Mail *mail = NULL;

  ENTER();

  if(folder != NULL)
  {
    LONG active = xget(G->MA->GUI.PG_MAILLIST, MUIA_NList_Active);

    if(active != MUIV_NList_Active_Off)
      DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_NList_GetEntry, active, &mail);

    if(folderp != NULL)
      *folderp = folder;

    if(activep != NULL)
      *activep = active;
  }

  RETURN(mail);
  return mail;
}

///
/// MA_ChangeMailStatus
//  Sets the status of a message
void MA_ChangeMailStatus(struct Mail *mail, int addflags, int clearflags)
{
  unsigned int newstatus = (mail->sflags | addflags) & ~(clearflags);

  D(DBF_MAIL, "ChangeMailStatus %08lx +%08lx -%08lx = %08lx", mail->sflags, addflags, clearflags, newstatus);

  ENTER();

  // check if the status is already set or not
  if(newstatus != mail->sflags)
  {
    if(IsMainThread() == TRUE)
    {
      struct Folder *folder = mail->Folder;

      // first substract the mail's old status from the folder's stats
      if(hasStatusNew(mail))
        folder->New--;

      if(!hasStatusRead(mail))
        folder->Unread--;

      if(hasStatusSent(mail))
        folder->Sent--;

      // set the new status
      mail->sflags = newstatus;

      // now add the mail's new status to the folder's stats
      if(hasStatusNew(mail))
        folder->New++;

      if(!hasStatusRead(mail))
        folder->Unread++;

      if(hasStatusSent(mail))
        folder->Sent++;

      // set the comment to the Mailfile
      MA_UpdateMailFile(mail);

      // flag the index as expired
      MA_ExpireIndex(mail->Folder);

      // update the status of the readmaildata (window)
      // of the mail here
      UpdateReadMailDataStatus(mail);

      // lets redraw the entry if it is actually displayed, so that
      // the status icon gets updated.
      DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_RedrawMail, mail);
    }
    else
    {
      PushMethodOnStack(G->App, 4, MUIM_YAMApplication_SetStatusTo, mail, addflags, clearflags);
    }
  }

  LEAVE();
}

///
/// MA_UpdateMailFile
// Updates the mail filename by taking the supplied mail structure
// into account
BOOL MA_UpdateMailFile(struct Mail *mail)
{
  char *dateFilePart = NULL;
  char statusFilePart[14 + 1];
  char oldFilePath[SIZE_PATHFILE];
  char *ptr;
  BOOL success = FALSE;
  int mcounter;

  ENTER();

  // modify the transferDate part
  base64encode(&dateFilePart, (char *)&mail->transDate, sizeof(mail->transDate));

  // for proper handling we have to remove an eventually existing "/" which
  // could be part of a base64 encoding
  ptr = dateFilePart;
  while((ptr = strchr(ptr, '/')) != NULL)
    *ptr = '-';

  // get the counter from the current mailfile
  mcounter = atoi(&mail->MailFile[13]);
  if(mcounter < 1 || mcounter > 999)
    mcounter = 1;

  // now modify the status part
  ptr = statusFilePart;
  if(hasStatusRead(mail))       *ptr++ = SCHAR_READ;
  if(hasStatusReplied(mail))    *ptr++ = SCHAR_REPLIED;
  if(hasStatusForwarded(mail))  *ptr++ = SCHAR_FORWARDED;
  if(hasStatusNew(mail))        *ptr++ = SCHAR_NEW;
  if(hasStatusSent(mail))       *ptr++ = SCHAR_SENT;
  if(hasStatusMarked(mail))     *ptr++ = SCHAR_MARKED;
  if(hasStatusError(mail))      *ptr++ = SCHAR_ERROR;
  if(hasStatusUserSpam(mail))   *ptr++ = SCHAR_USERSPAM;
  if(hasStatusAutoSpam(mail))   *ptr++ = SCHAR_AUTOSPAM;
  if(hasStatusHam(mail))        *ptr++ = SCHAR_HAM;
  if(getPERValue(mail) > 0)     *ptr++ = '0'+getPERValue(mail);

  *ptr = '\0'; // NUL terminate it

  // construct the full old file path
  AddPath(oldFilePath, mail->Folder->Fullpath, mail->MailFile, sizeof(oldFilePath));

  while(success == FALSE)
  {
    char newFileName[SIZE_MFILE];
    char newFilePath[SIZE_PATHFILE];

    // generate a new filename with the data we have collected
    snprintf(newFileName, sizeof(newFileName), "%s.%03d,%s", dateFilePart, mcounter, statusFilePart);

    // now check if the filename has changed or not
    if(strcmp(newFileName, mail->MailFile) == 0)
    {
      success = TRUE;
      break;
    }

    // construct new full file path
    AddPath(newFilePath, mail->Folder->Fullpath, newFileName, sizeof(newFilePath));

    // then rename it
    if(Rename(oldFilePath, newFilePath) == DOSFALSE)
    {
      LONG error = IoErr();

      E(DBF_MAIL, "could not rename '%s' to '%s', error %ld", oldFilePath, newFilePath, error);

      // if we end up here then a file with the newFileName probably already exists, so increase the mail
      // counter. We also bail out if the original file cannot be found for any reason, although this
      // should never be happening.
      if(++mcounter > 999 || error == ERROR_OBJECT_NOT_FOUND)
        break;
    }
    else
    {
      struct ReadMailData *rmData;

      D(DBF_MAIL, "renamed '%s' to '%s'", oldFilePath, newFilePath);

      strlcpy(mail->MailFile, newFileName, sizeof(mail->MailFile));
      success = TRUE;

      // before we exit we check through all our read windows if
      // they contain the mail we have changed the status, so
      // that we can update the filename in the read window structure
      // aswell
      IterateList(&G->readMailDataList, struct ReadMailData *, rmData)
      {
        if(rmData->mail == mail && strcmp(rmData->readFile, oldFilePath) == 0)
          strlcpy(rmData->readFile, newFilePath, sizeof(rmData->readFile));
      }
    }
  }

  free(dateFilePart);

  RETURN(success);
  return success;
}

///
/// MA_CreateFullList
//  Builds a list containing all messages in a folder
struct MailList *MA_CreateFullList(struct Folder *fo, BOOL onlyNew)
{
  struct MailList *mlist = NULL;

  ENTER();

  // we need a valid non-group folder and a loaded index
  if(fo != NULL && isGroupFolder(fo) == FALSE && MA_GetIndex(fo) == TRUE)
  {
    D(DBF_MAIL, "create mail list for folder '%s'", fo->Name);

    if((onlyNew == TRUE  && fo->New > 0) ||
       (onlyNew == FALSE && fo->Total > 0))
    {
      if(onlyNew == TRUE)
      {
        // create a list of new mails only
        LockMailListShared(fo->messages);

        if(IsMailListEmpty(fo->messages) == FALSE)
        {
          if((mlist = CreateMailList()) != NULL)
          {
            struct MailNode *mnode;

            ForEachMailNode(fo->messages, mnode)
            {
              struct Mail *mail = mnode->mail;

              // only if this is a new mail we add it to our list
              if(hasStatusNew(mail))
              {
                AddNewMailNode(mlist, mail);
              }
            }

            // let everything fail if there were no mails added to the list
            if(IsMailListEmpty(mlist) == TRUE)
            {
              W(DBF_MAIL, "no new mails found, destroying empty list");
              DeleteMailList(mlist);
              mlist = NULL;
            }
          }
        }

        UnlockMailList(fo->messages);
      }
      else
      {
        // create a clone copy of all messages
        // handling of empty lists is already done by CloneMailList()
        mlist = CloneMailList(fo->messages);
      }
    }
    else
      W(DBF_MAIL, "no mails to be added to the list (onlyNew %ld, new %ld, total %ld)", onlyNew, fo->New, fo->Total);
  }

  RETURN(mlist);
  return mlist;
}

///
/// MA_CreateMarkedList
//  Builds a linked list containing the selected messages
struct MailList *MA_CreateMarkedList(Object *lv, BOOL onlyNew)
{
  struct MailList *mlist = NULL;

  ENTER();

  // we first have to check whether this is a valid folder or not
  if(isGroupFolder(GetCurrentFolder()) == FALSE)
  {
    LONG selected;

    DoMethod(lv, MUIM_NList_Select, MUIV_NList_Select_All, MUIV_NList_Select_Ask, &selected);
    if(selected > 0)
    {
      if((mlist = CreateMailList()) != NULL)
      {
        LONG id = MUIV_NList_NextSelected_Start;

        while(TRUE)
        {
          struct Mail *mail;

          DoMethod(lv, MUIM_NList_NextSelected, &id);
          if(id == MUIV_NList_NextSelected_End)
            break;

          DoMethod(lv, MUIM_NList_GetEntry, id, &mail);
          if(mail != NULL)
          {
            mail->position = id;

            if(onlyNew == FALSE || hasStatusNew(mail))
               AddNewMailNode(mlist, mail);
          }
          else
            E(DBF_MAIL, "MUIM_NList_GetEntry didn't return a valid mail pointer");
        }
      }
      else
        E(DBF_MAIL, "couldn't create mail list!");
    }
    else
    {
      struct Mail *mail;

      DoMethod(lv, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &mail);
      if(mail != NULL && (onlyNew == FALSE || hasStatusNew(mail)))
      {
        if((mlist = CreateMailList()) != NULL)
        {
          mail->position = xget(lv, MUIA_NList_Active);
          AddNewMailNode(mlist, mail);
        }
        else
          E(DBF_MAIL, "couldn't create mail list!");
      }
    }
  }

  // let everything fail if there were no mails added to the list
  if(mlist != NULL && IsMailListEmpty(mlist) == TRUE)
  {
    E(DBF_MAIL, "no active or selected mails found, destroying empty list");
    DeleteMailList(mlist);
    mlist = NULL;
  }

  RETURN(mlist);
  return mlist;
}

///
/// MA_DeleteSingle
//  Deletes a single message
void MA_DeleteSingle(struct Mail *mail, const ULONG delFlags)
{
  ENTER();

  if(mail != NULL && mail->Folder != NULL)
  {
    struct Folder *folder = mail->Folder;

    if(C->RemoveAtOnce == TRUE ||
       isTrashFolder(folder) ||
       (isSpamFolder(folder) && hasStatusSpam(mail)) ||
       isFlagSet(delFlags, DELF_AT_ONCE))
    {
      char mailfile[SIZE_PATHFILE];

      D(DBF_MAIL, "deleting mail with subject '%s' from folder '%s'", mail->Subject, folder->Name);

      // before we go and delete/free the mail we have to check
      // all possible write windows if they are refering to it
      SetWriteMailDataMailRef(mail, NULL);

      AppendToLogfile(LF_VERBOSE, 21, tr(MSG_LOG_DeletingVerbose), AddrName(mail->From), mail->Subject, folder->Name);

      // make sure we delete the mailfile
      GetMailFile(mailfile, sizeof(mailfile), NULL, mail);
      DeleteFile(mailfile);

      // increase the mail's reference counter to prevent RemoveMailFromFolder() from
      // freeing the mail in its DeleteMailNode() call
      ReferenceMail(mail);

      // now remove the mail from its folder/mail list
      // this might already free the mail if the reference counter drops to zero
      RemoveMailFromFolder(mail, isFlagSet(delFlags, DELF_CLOSE_WINDOWS), isFlagSet(delFlags, DELF_CHECK_CONNECTIONS));

      // decrease the reference counter again
      // it should be freed now since the refcount should decrease to 0 now
      DereferenceMail(mail);

      // if we are allowed to make some noise we
      // update our Statistics
      if(isFlagClear(delFlags, DELF_QUIET))
        DisplayStatistics(folder, isFlagSet(delFlags, DELF_UPDATE_APPICON));
    }
    else
    {
      struct Folder *delfolder = FO_GetFolderByType(FT_TRASH, NULL);

      D(DBF_MAIL, "moving mail with subject '%s' from folder '%s' to folder 'trash'", mail->Subject, folder->Name);

      MA_MoveCopySingle(mail, delfolder, "delete", isFlagSet(delFlags, DELF_CLOSE_WINDOWS) ? MVCPF_CLOSE_WINDOWS : 0);

      // if we are allowed to make some noise we
      // update our statistics
      if(isFlagClear(delFlags, DELF_QUIET))
      {
        // don't update the appicon yet
        DisplayStatistics(delfolder, FALSE);

        // but update it now, if that is allowed
        DisplayStatistics(folder, isFlagSet(delFlags, DELF_UPDATE_APPICON));
      }
    }
  }
  else
    E(DBF_MAIL, "mail or mail->Folder is NULL!! (%08lx)", mail);

  LEAVE();
}

///
/// MA_MoveCopySingle
//  Moves or copies a single message from one folder to another
static void MA_MoveCopySingle(struct Mail *mail, struct Folder *to, const char *originator, const ULONG flags)
{
  struct Folder *from;
  struct Mail *newMail = NULL;
  char mfile[SIZE_MFILE];
  int result;

  ENTER();

  from = mail->Folder;
  strlcpy(mfile, mail->MailFile, sizeof(mfile));

  if((result = TransferMailFile(isFlagSet(flags, MVCPF_COPY), mail, to)) >= 0)
  {
    if(isFlagSet(flags, MVCPF_COPY))
    {
      AppendToLogfile(LF_VERBOSE, 25, tr(MSG_LOG_COPY_MAIL), originator, AddrName(mail->From), mail->Subject, from->Name, to->Name);

      if((newMail = CloneMail(mail)) != NULL)
      {
        // add the new mail
        AddMailToFolder(newMail, to);

        // restore the old filename in case it was changed by TransferMailFile()
        strlcpy(mail->MailFile, mfile, sizeof(mail->MailFile));
      }
    }
    else
    {
      AppendToLogfile(LF_VERBOSE, 23, tr(MSG_LOG_MOVE_MAIL), originator, AddrName(mail->From), mail->Subject, from->Name, to->Name);

      // increase the mail's reference counter to prevent RemoveMailFromFolder() from
      // freeing the mail in its DeleteMailNode() call
      ReferenceMail(mail);

      // now remove the mail from its folder/mail list
      RemoveMailFromFolder(mail, isFlagSet(flags, MVCPF_CLOSE_WINDOWS), isFlagSet(flags, MVCPF_CHECK_CONNECTIONS));

      // add the mail to the new folder
      AddMailToFolder(mail, to);

      // decrease the reference counter again
      // it will not be freed as it has been added to the new folder
      DereferenceMail(mail);

      newMail = mail;

      // there is no need to check the opened write mail window if they
      // are referring to this mail, as it is not going to magically
      // vanish from under their feet.
    }

    if(newMail != NULL)
    {
      if(to == GetCurrentFolder())
        DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_NList_InsertSingle, newMail, MUIV_NList_Insert_Sorted);

      if(C->SpamFilterEnabled == TRUE && C->SpamMarkOnMove == TRUE)
      {
        if(isSpamFolder(from) && hasStatusSpam(newMail))
        {
          // if we are moving a (non-)spam mail out of the spam folder then this one will be marked as non-spam
          BayesFilterSetClassification(newMail, BC_HAM);
          setStatusToHam(newMail);
        }
        else if(isSpamFolder(to) && !hasStatusSpam(newMail))
        {
          // if we are moving a non-spam mail to the spam folder then this one will be marked as spam
          BayesFilterSetClassification(newMail, BC_SPAM);
          setStatusToUserSpam(newMail);
        }
      }
    }
  }
  else
  {
    E(DBF_MAIL, "MA_MoveCopySingle error: %ld", result);

    switch(result)
    {
      case -2:
        ER_NewError(tr(MSG_ER_XPKUSAGE), mail->MailFile);
      break;

      default:
        ER_NewError(tr(MSG_ER_TRANSFERMAIL), mail->MailFile, to->Name);
      break;
    }
  }

  LEAVE();
}

///
/// MA_MoveCopy
//  Moves or copies messages from one folder to another
void MA_MoveCopy(struct Mail *mail, struct Folder *tobox, const char *originator, const ULONG flags)
{
  struct Folder *frombox;
  struct MailList *mlist;
  int selected;

  ENTER();

  if(mail != NULL)
    frombox = mail->Folder;
  else
    frombox = GetCurrentFolder();

  if(isFlagClear(flags, MVCPF_COPY) && frombox == tobox)
  {
    LEAVE();
    return;
  }

  // if a specific mail should be moved we do it now.
  if(mail != NULL)
  {
    selected = 1;
    MA_MoveCopySingle(mail, tobox, originator, flags);
  }
  else if((mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE)) != NULL)
  {
    struct BusyNode *busy = NULL;
    char selectedStr[SIZE_SMALL];
    struct MailNode *mnode;
    ULONG i;

    // get the list of the currently marked mails
    selected = mlist->count;

    set(G->MA->GUI.PG_MAILLIST, MUIA_NList_Quiet, TRUE);

    if(mlist->count != 1)
    {
      snprintf(selectedStr, sizeof(selectedStr), "%d", selected);
      busy = BusyBegin(BUSY_PROGRESS_ABORT);
      BusyText(busy, tr(MSG_BusyMoving), selectedStr);
    }

    i = 0;
    ForEachMailNode(mlist, mnode)
    {
      MA_MoveCopySingle(mnode->mail, tobox, originator, flags);

      // if BusyProgress() returns FALSE, then the user aborted
      if(BusyProgress(busy, ++i, selected) == FALSE)
      {
        selected = i;
        break;
      }
    }
    BusyEnd(busy);

    set(G->MA->GUI.PG_MAILLIST, MUIA_NList_Quiet, FALSE);

    DeleteMailList(mlist);
  }
  else
    selected = 0;

  if(selected != 0)
  {
    // write some log out
    if(isFlagSet(flags, MVCPF_COPY))
      AppendToLogfile(LF_NORMAL, 24, tr(MSG_LOG_Copying), selected, FolderName(frombox), FolderName(tobox));
    else
      AppendToLogfile(LF_NORMAL, 22, tr(MSG_LOG_Moving), selected, FolderName(frombox), FolderName(tobox));

    if(isFlagClear(flags, MVCPF_QUIET))
    {
      // refresh the folder statistics if necessary
      if(isFlagClear(flags, MVCPF_COPY))
        DisplayStatistics(frombox, FALSE);

      DisplayStatistics(tobox, TRUE);

      MA_ChangeSelected(FALSE);
    }
  }

  LEAVE();
}

///
/// MA_ToStatusHeader
// Function that converts the current flags of a message
// to "Status:" headerline flags
char *MA_ToStatusHeader(struct Mail *mail)
{
  static char flags[3]; // should not be more than 3 bytes

  ENTER();

  if(hasStatusRead(mail))
  {
    if(hasStatusNew(mail))
    {
      flags[0] = 'R';
      flags[1] = '\0';
    }
    else
    {
      flags[0] = 'R';
      flags[1] = 'O';
      flags[2] = '\0';
    }
  }
  else
  {
    if(hasStatusNew(mail))
    {
      flags[0] = '\0';
    }
    else
    {
      flags[0] = 'O';
      flags[1] = '\0';
    }
  }

  RETURN(flags);
  return flags;
}

///
/// MA_ToXStatusHeader
// Function that converts the current flags of a message
// to "X-Status:" headerline flags
char *MA_ToXStatusHeader(struct Mail *mail)
{
  static char flags[10]; // should not be more than 9+1 bytes
  char *ptr = flags;

  ENTER();

  if(hasStatusRead(mail))
    *ptr++ = 'R';

  if(hasStatusReplied(mail))
    *ptr++ = 'A';

  if(hasStatusMarked(mail))
    *ptr++ = 'F';

  if(hasStatusUserSpam(mail))
    *ptr++ = 'X';

  if(hasStatusAutoSpam(mail))
    *ptr++ = 'J';

  if(hasStatusHam(mail))
    *ptr++ = 'Y';

  // NUL terminate it
  *ptr = '\0';

  RETURN(flags);
  return flags;
}

///
/// MA_FromStatusHeader
// Function that converts chars from the Status: headerline to a proper
// mail status flag value
unsigned int MA_FromStatusHeader(char *statusflags)
{
  unsigned int sflags = SFLAG_NEW;
  char c;

  ENTER();

  D(DBF_MAIL, "getting flags from status '%s'", statusflags);
  while((c = *statusflags++) != '\0')
  {
    switch(c)
    {
      case 'R':
        setFlag(sflags, SFLAG_READ);
      break;

      case 'O':
        clearFlag(sflags, SFLAG_NEW);
      break;
    }
  }
  D(DBF_MAIL, "status flags %08lx", sflags);

  RETURN(sflags);
  return sflags;
}

///
/// MA_FromXStatusHeader
// Function that converts chars from the X-Status: headerline to a
// proper mail status flag value
unsigned int MA_FromXStatusHeader(char *xstatusflags)
{
  unsigned int sflags = SFLAG_NEW;
  char c;

  ENTER();

  D(DBF_MAIL, "getting flags from xstatus '%s'", xstatusflags);
  while((c = *xstatusflags++) != '\0')
  {
    switch(c)
    {
      case 'R':
        setFlag(sflags, SFLAG_READ);
        clearFlag(sflags, SFLAG_NEW);
      break;

      case 'A':
        setFlag(sflags, SFLAG_REPLIED);
      break;

      case 'F':
        setFlag(sflags, SFLAG_MARKED);
      break;

      case 'T':
        setFlag(sflags, SFLAG_HOLD);
      break;

      case 'X':
        setFlag(sflags, SFLAG_USERSPAM);
        clearFlag(sflags, SFLAG_AUTOSPAM);
        clearFlag(sflags, SFLAG_HAM);
      break;

      case 'J':
        setFlag(sflags, SFLAG_AUTOSPAM);
        clearFlag(sflags, SFLAG_USERSPAM);
        clearFlag(sflags, SFLAG_HAM);
      break;

      case 'Y':
        setFlag(sflags, SFLAG_HAM);
        clearFlag(sflags, SFLAG_USERSPAM);
        clearFlag(sflags, SFLAG_AUTOSPAM);
      break;
    }
  }
  D(DBF_MAIL, "xstatus flags %08lx", sflags);

  RETURN(sflags);
  return sflags;
}

///

/*** Mail Thread Nagivation ***/
/// FindThreadInFolder
// Find the next/prev message in a thread within one folder
struct Mail *FindThreadInFolder(const struct Mail *srcMail, const struct Folder *folder, const BOOL nextThread)
{
  struct Mail *result = NULL;
  struct MailNode *mnode;

  ENTER();

  LockMailListShared(folder->messages);

  ForEachMailNode(folder->messages, mnode)
  {
    struct Mail *mail = mnode->mail;

    if(nextThread == TRUE)
    {
      // find the answer to the srcMail
      if(mail->cIRTMsgID != 0 && mail->cIRTMsgID == srcMail->cMsgID)
      {
        result = mail;
        break;
      }
    }
    else
    {
      // else we have to find the question to the srcMail
      if(mail->cMsgID != 0 && mail->cMsgID == srcMail->cIRTMsgID)
      {
        result = mail;
        break;
      }
    }
  }

  UnlockMailList(folder->messages);

  RETURN(result);
  return result;
}

///
/// FindThread
//  Find the next/prev message in a thread and return a pointer to it
struct Mail *FindThread(const struct Mail *srcMail, const BOOL nextThread)
{
  struct Mail *mail = NULL;

  ENTER();

  if(srcMail != NULL)
  {
    // check that the index is successfully loaded and if not do it now.
    if(MA_GetIndex(srcMail->Folder) == TRUE)
    {
      // first we take the folder of the srcMail as a priority in the
      // search of the next/prev thread so we have to check that we
      // have a valid index before we are going to go on.
      if((mail = FindThreadInFolder(srcMail, srcMail->Folder, nextThread)) == NULL)
      {
        // if the user enabled the global thread search we go and walk through all
        // the other folders and search for further mails of the same threads.
        if(C->GlobalMailThreads == TRUE)
        {
          struct FolderNode *fnode;

          // if we still haven't found the mail we have to scan the other folders aswell
          LockFolderListShared(G->folders);

          ForEachFolderNode(G->folders, fnode)
          {
            struct Folder *folder = fnode->folder;

            // check if this folder isn't a group and that we haven't scanned
            // it already.
            if(isGroupFolder(folder) == FALSE && folder != srcMail->Folder)
            {
              // make sure the index is loaded and valid
              if(MA_GetIndex(folder) == TRUE)
              {
                if((mail = FindThreadInFolder(srcMail, folder, nextThread)) != NULL)
                {
                  // we found a suitable mail
                  break;
                }
              }
            }
          }

          UnlockFolderList(G->folders);
        }
      }
    }
  }

  RETURN(mail);
  return mail;
}

///

/*** Main button functions ***/
/// MA_ReadMessage
//  Loads active message into a read window
HOOKPROTONHNONP(MA_ReadMessage, void)
{
  struct Mail *mail;

  if((mail = MA_GetActiveMail(NULL, NULL, NULL)) != NULL)
  {
    struct ReadMailData *rmData;

    // Check if this mail is already in a readwindow
    IterateList(&G->readMailDataList, struct ReadMailData *, rmData)
    {
      // check if the active mail is already open in another read
      // window, and if so we just bring it to the front.
      if(rmData != G->ActiveRexxRMData &&
         rmData->readWindow != NULL &&
         rmData->mail == mail)
      {
        DoMethod(rmData->readWindow, MUIM_Window_ToFront);
        set(rmData->readWindow, MUIA_Window_Activate, TRUE);
        return;
      }
    }

    // if not, then we create/reuse a new one
    if((rmData = CreateReadWindow(FALSE)) != NULL)
    {
      // make sure it is opened correctly and then read in a mail
      if(SafeOpenWindow(rmData->readWindow) == FALSE ||
         DoMethod(rmData->readWindow, MUIM_ReadWindow_ReadMail, mail) == FALSE)
      {
        // on any error we make sure to delete the read window
        // immediatly again.
        CleanupReadMailData(rmData, TRUE);
      }
    }
  }
}
MakeHook(MA_ReadMessageHook, MA_ReadMessage);

///
/// MA_RemoveAttach
//  Removes attachments from a message
void MA_RemoveAttach(struct Mail *mail, struct Part **whichParts, BOOL warning)
{
  BOOL goOn = TRUE;

  ENTER();

  // if we need to warn the user of this operation we put up a requester
  // before we go on
  if(warning == FALSE)
    goOn = TRUE;
  else
  {
    if(whichParts == NULL)
      goOn = (MUI_Request(G->App, G->MA->GUI.WI, MUIF_NONE, NULL, tr(MSG_YesNoReq2), tr(MSG_MA_DELETEATTREQUEST)) != 0);
    else
    {
      // build a list of filenames which will be deleted
      char *fileList = NULL;
      ULONG i = 0;

      while(whichParts[i] != NULL)
      {
        if(whichParts[i]->CParName != NULL)
          dstrcat(&fileList, whichParts[i]->CParName);
        else
          dstrcat(&fileList, whichParts[i]->CParFileName);

        dstrcat(&fileList, "\n");
        i++;
      }

      goOn = (MUI_Request(G->App, G->MA->GUI.WI, MUIF_NONE, NULL, tr(MSG_YesNoReq2), tr(MSG_MA_DELETESELECTEDREQUEST), fileList) != 0);

      dstrfree(fileList);
    }
  }

  if(goOn == TRUE)
  {
    struct ReadMailData *rmData;

    if((rmData = AllocPrivateRMData(mail, PM_ALL)) != NULL)
    {
      struct Part *part;
      char *cmsg;
      char fname[SIZE_PATHFILE];
      char tfname[SIZE_PATHFILE];

      GetMailFile(fname, sizeof(fname), NULL, mail);
      snprintf(tfname, sizeof(tfname), "%s.tmp", fname);

      if((cmsg = RE_ReadInMessage(rmData, RIM_QUIET)) != NULL)
      {
        if((part = rmData->firstPart->Next) != NULL && part->Next != NULL)
        {
          FILE *out;
          struct Part *headerPart = rmData->firstPart;

          SHOWSTRING(DBF_MAIL, tfname);
          if((out = fopen(tfname, "w")) != NULL)
          {
            ULONG keptParts = 0;
            LONG size;
            struct ReadMailData *rmData2;

            for(part = rmData->firstPart; part; part = part->Next)
            {
              if(part == headerPart)
              {
                FILE *in;

                D(DBF_MAIL, "keeping header part '%s'", part->Filename);

                // For the header part we simply copy from the raw mail file instead of the
                // parsed mail part file, because the raw file may contain additonal MIME
                // warning texts which we want to keep, because they belong to the original
                // mail.
                if((in = fopen(rmData->readFile, "r")) != NULL)
                {
                  char stopBoundary[SIZE_DEFAULT];
                  int stopBoundaryLen = 0;
                  char *buf = NULL;
                  size_t buflen = 0;
                  ssize_t curlen;

                  setvbuf(in, NULL, _IOFBF, SIZE_FILEBUF);

                  stopBoundaryLen = snprintf(stopBoundary, sizeof(stopBoundary), "--%s", headerPart->CParBndr);

                  while((curlen = getline(&buf, &buflen, in)) > 0)
                  {
                    // copy all lines until we find the first boundary marker which terminates
                    // the first mail part
                    if(strncmp(buf, stopBoundary, stopBoundaryLen) == 0)
                      break;
                    else
                      fwrite(buf, curlen, 1, out);
                  }

                  fclose(in);

                  // write out the boundary
                  fwrite(stopBoundary, stopBoundaryLen, 1, out);

                  free(buf);
                }
              }
              else
              {
                BOOL keepThisPart = TRUE;

                if(part->Nr == rmData->letterPartNum)
                {
                  // we keep the letter part in any case
                  D(DBF_MAIL, "keeping letter part '%s'", part->Filename);
                }
                else if(isAlternativePart(part))
                {
                  // don't delete alternative parts
                  D(DBF_MAIL, "keeping alternative part '%s'", part->Filename);
                }
                else if(stricmp(part->ContentType, "text/deleted") == 0)
                {
                  // don't delete the replacement part of already deleted parts
                  D(DBF_MAIL, "keeping deleted part '%s'", part->Filename);
                }
                else if(whichParts == NULL)
                {
                  // a NULL list indicates that all attachments are to be deleted
                  keepThisPart = FALSE;
                }
                else
                {
                  ULONG i = 0;

                  while(whichParts[i] != NULL)
                  {
                    if(whichParts[i]->Nr == part->Nr)
                    {
                      keepThisPart = FALSE;
                      break;
                    }
                    i++;
                  }
                }

                if(keepThisPart == TRUE)
                {
                  // write out this part to the new mail file

                  D(DBF_MAIL, "keeping part '%s' '%s'", part->CParName, part->Filename);

                  if(isDecoded(part) == TRUE)
                  {
                    struct WritePart writePart;

                    // keep as much as information as possible from the original part
                    memset(&writePart, 0, sizeof(writePart));
                    writePart.ContentType = part->ContentType;
                    writePart.Filename = part->Filename;
                    writePart.Description = part->CParDesc;
                    writePart.Name = part->CParName;
                    // declare plain texts as UTF8 as this is our in-memory representation
                    // after having decoded them. Other parts get their charset preserved.
                    if(stricmp(part->ContentType, "text/plain") == 0)
                      writePart.Codeset = CodesetsFind((char *)"UTF8", TAG_DONE);
                    else
                      writePart.Codeset = CodesetsFind(part->CParCSet, TAG_DONE);
                    writePart.EncType = part->EncodingCode;

                    // create a new header, since decoded parts have these stripped
                    fputc('\n', out);
                    WriteContentTypeAndEncoding(out, &writePart);
                    fputc('\n', out);
                    EncodePart(out, &writePart);
                    fprintf(out, "\n--%s", headerPart->CParBndr);
                  }
                  else
                  {
                    // undecoded parts are simply appended without change
                    FILE *in;

                    if((in = fopen(part->Filename, "r")) != NULL)
                    {
                      char *buf = NULL;
                      size_t buflen = 0;
                      ssize_t curlen = 0;

                      setvbuf(in, NULL, _IOFBF, SIZE_FILEBUF);

                      fputc('\n', out);
                      while((curlen = getline(&buf, &buflen, in)) > 0)
                        fwrite(buf, curlen, 1, out);

                      fclose(in);
                      fprintf(out, "\n--%s", headerPart->CParBndr);

                      free(buf);
                    }
                  }

                  keptParts++;
                }
                else
                {
                  // write out a new mail part which just contains some information
                  // about the deleted part
                  struct WritePart writePart;
                  char tempName[SIZE_PATHFILE];
                  char tempFileName[SIZE_PATHFILE];

                  D(DBF_MAIL, "deleting part '%s' '%s'", part->CParName, part->Filename);

                  snprintf(tempName, sizeof(tempName), "Deleted: %s", part->CParName);
                  snprintf(tempFileName, sizeof(tempFileName), "Deleted_%s", part->CParFileName);

                  memset(&writePart, 0, sizeof(writePart));
                  writePart.ContentType = "text/deleted";
                  writePart.Filename = tempFileName;
                  writePart.Description = part->CParDesc;
                  writePart.Name = tempName;
                  writePart.Codeset = G->writeCodeset;
                  writePart.EncType = ENC_8BIT;

                  fputc('\n', out);
                  WriteContentTypeAndEncoding(out, &writePart);
                  fputc('\n', out);
                  fprintf(out, "The original MIME headers for this attachment are:\n");
                  fprintf(out, "Content-Type: %s; name=\"%s\"\n", part->ContentType, part->CParName);
                  fprintf(out, "Content-Transfer-Encoding: %s\n", EncodingName(part->EncodingCode));
                  fprintf(out, "Content-Disposition: %s; filename=\"%s\"\n", part->ContentDisposition, part->CParFileName);
                  fprintf(out, "\n--%s", headerPart->CParBndr);
                }
              }
            }

            fputs("--\n\n", out);

            fclose(out);

            // update the size information
            if(ObtainFileInfo(tfname, FI_SIZE, &size) == TRUE)
            {
              mail->Folder->Size += size - mail->Size;
              mail->Size = size;
            }
            else
              mail->Size = -1;

            // clear the multipart/mixed flag only if we just removed all attachments
            if(keptParts == 0)
              clearFlag(mail->mflags, MFLAG_MP_MIXED);

            // flag the mail's folder as modified
            setFlag(mail->Folder->Flags, FOFL_MODIFY);

            if(mail->Folder == GetCurrentFolder())
              DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_MainMailListGroup_RedrawMail, mail);

            // replace the old mail file by the new one
            DeleteFile(fname);
            if(mail->Folder->Mode > FM_SIMPLE)
              DoPack(tfname, fname, mail->Folder);
            else
              RenameFile(tfname, fname);

            if((rmData2 = GetReadMailData(mail)) != NULL)
            {
              // make sure to refresh the mail of this window as we do not
              // have any attachments anymore
              if(rmData2->readWindow != NULL)
                DoMethod(rmData2->readWindow, MUIM_ReadWindow_RereadMail, mail, MUIF_ReadMailGroup_ReadMail_UpdateOnly);
              else if(rmData2->readMailGroup != NULL)
                DoMethod(rmData2->readMailGroup, MUIM_ReadMailGroup_ReadMail, mail, MUIF_ReadMailGroup_ReadMail_UpdateOnly);
            }

            MA_ChangeSelected(TRUE);
            DisplayStatistics(mail->Folder, TRUE);

            AppendToLogfile(LF_ALL, 81, tr(MSG_LOG_DELETEDATT), mail->MailFile, mail->Folder->Name);
          }
        }

        dstrfree(cmsg);
      }

      FreePrivateRMData(rmData);
    }
  }

  LEAVE();
}

///
/// MA_RemoveAttachFunc
//  Removes attachments from selected messages
HOOKPROTONHNONP(MA_RemoveAttachFunc, void)
{
  BOOL goOn;

  ENTER();

  // we need to warn the user of this operation we put up a requester
  // before we go on
  if(C->ConfirmRemoveAttachments == TRUE)
    goOn = (MUI_Request(G->App, G->MA->GUI.WI, MUIF_NONE, NULL, tr(MSG_YesNoReq2), tr(MSG_MA_DELETEATTREQUEST)) > 0);
  else
    goOn = TRUE;

  if(goOn == TRUE)
  {
    struct MailList *mlist;

    // get the list of all selected mails
    if((mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE)) != NULL)
    {
      struct BusyNode *busy;
      int i;
      struct MailNode *mnode;

      busy = BusyBegin(BUSY_PROGRESS_ABORT);
      BusyText(busy, tr(MSG_BusyRemovingAtt), "");

      i = 0;
      ForEachMailNode(mlist, mnode)
      {
        // delete the attachments without further confirmation
        MA_RemoveAttach(mnode->mail, NULL, FALSE);

        // if BusyProgress() returns FALSE, then the user aborted
        if(BusyProgress(busy, ++i, mlist->count) == FALSE)
          break;
      }

      DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_NList_Redraw, MUIV_NList_Redraw_All);

      MA_ChangeSelected(TRUE);
      DisplayStatistics(NULL, TRUE);
      BusyEnd(busy);

      // free the mail list again
      DeleteMailList(mlist);
    }
  }

  LEAVE();
}
MakeHook(MA_RemoveAttachHook, MA_RemoveAttachFunc);

///
/// MA_SaveAttachFunc
//  Saves all attachments of selected messages to disk
HOOKPROTONHNONP(MA_SaveAttachFunc, void)
{
  struct MailList *mlist;

  ENTER();

  if((mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE)) != NULL)
  {
    struct FileReqCache *frc;

    if((frc = ReqFile(ASL_DETACH, G->MA->GUI.WI, tr(MSG_RE_SaveMessage), (REQF_SAVEMODE|REQF_DRAWERSONLY), C->DetachDir, "")) != NULL)
    {
      struct BusyNode *busy;
      struct MailNode *mnode;

      busy = BusyBegin(BUSY_TEXT);
      BusyText(busy, tr(MSG_BusyDecSaving), "");

      ForEachMailNode(mlist, mnode)
      {
        struct ReadMailData *rmData;

        if((rmData = AllocPrivateRMData(mnode->mail, PM_ALL)) != NULL)
        {
          char *cmsg;

          if((cmsg = RE_ReadInMessage(rmData, RIM_QUIET)) != NULL)
          {
            struct Part *part;

            // free the message again as we don't need its content here.
            dstrfree(cmsg);

            if((part = rmData->firstPart->Next) != NULL && part->Next != NULL)
              RE_SaveAll(rmData, frc->drawer);
          }

          FreePrivateRMData(rmData);
        }
      }

      BusyEnd(busy);
    }

    DeleteMailList(mlist);
  }

  LEAVE();
}
MakeHook(MA_SaveAttachHook, MA_SaveAttachFunc);

///
/// MA_SavePrintFunc
//  Prints selected messages
HOOKPROTONHNO(MA_SavePrintFunc, void, int *arg)
{
  BOOL doprint = (*arg != 0);

  ENTER();

  if(doprint == FALSE || CheckPrinter(G->MA->GUI.WI) == TRUE)
  {
    struct MailList *mlist;

    if((mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE)) != NULL)
    {
      struct MailNode *mnode;
      BOOL abortPrint = FALSE;

      ForEachMailNode(mlist, mnode)
      {
        struct ReadMailData *rmData;

        if((rmData = AllocPrivateRMData(mnode->mail, PM_ALL)) != NULL)
        {
          char *cmsg;

          if((cmsg = RE_ReadInMessage(rmData, RIM_PRINT)) != NULL)
          {
            struct TempFile *tf;

            if((tf = OpenTempFile("w")) != NULL)
            {
              fputs(cmsg, tf->FP);
              fclose(tf->FP);
              tf->FP = NULL;

              if(doprint == TRUE)
              {
                if(CopyFile("PRT:", 0, tf->Filename, 0) == FALSE)
                {
                  MUI_Request(G->App, G->MA->GUI.WI, MUIF_NONE, tr(MSG_ErrorReq), tr(MSG_OkayReq), tr(MSG_ER_PRINTER_FAILED));
                  abortPrint = TRUE;
                }
              }
              else
              {
                // export the mail but abort our iteration in
                // case the user pressed 'Cancel' or the export failed
                if(RE_Export(rmData, tf->Filename, "", "", 0, FALSE, FALSE, IntMimeTypeArray[MT_TX_PLAIN].ContentType) == FALSE)
                  abortPrint = TRUE;
              }

              CloseTempFile(tf);
            }

            dstrfree(cmsg);
          }

          FreePrivateRMData(rmData);
        }

        if(abortPrint == TRUE)
          break;
      }

      DeleteMailList(mlist);
    }
  }

  LEAVE();
}
MakeHook(MA_SavePrintHook, MA_SavePrintFunc);

///
/// NewMessage
//  Starts a new message
struct WriteMailData *NewMessage(enum NewMailMode mode, const int flags)
{
  struct WriteMailData *wmData = NULL;

  ENTER();

  switch(mode)
  {
    case NMM_NEW:
      wmData = NewWriteMailWindow(NULL, flags);
    break;

    case NMM_EDIT:
    {
      struct Mail *mail;

      if((mail = MA_GetActiveMail(NULL, NULL, NULL)) != NULL)
        wmData = NewEditMailWindow(mail, flags);
    }
    break;

    case NMM_REDIRECT:
    {
      struct MailList *mlist;

      if((mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE)) != NULL)
      {
        wmData = NewRedirectMailWindow(mlist, flags);

        DeleteMailList(mlist);
      }
    }
    break;

    case NMM_FORWARD:
    case NMM_FORWARD_ATTACH:
    case NMM_FORWARD_INLINE:
    {
      struct MailList *mlist;

      if((mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE)) != NULL)
      {
        wmData = NewForwardMailWindow(mlist, flags);

        DeleteMailList(mlist);
      }
    }
    break;

    case NMM_REPLY:
    {
      struct MailList *mlist;

      if((mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE)) != NULL)
      {
        char *replytext = NULL;

        // in case there is only one mail selected we have to check wheter there is
        // text currently selected by the user
        if(C->EmbeddedReadPane == TRUE &&
           NextMailNode(FirstMailNode(mlist)) == NULL)
        {
          replytext = (char *)DoMethod(G->MA->GUI.MN_EMBEDDEDREADPANE, MUIM_ReadMailGroup_ExportSelectedText);
        }

        // now we create a new reply mail window
        wmData = NewReplyMailWindow(mlist, flags, replytext);

        // free the replytext in case we go one
        if(replytext != NULL)
          FreeVec(replytext);

        // cleanup the temporarly created mail list
        DeleteMailList(mlist);
      }
    }
    break;

    case NMM_EDITASNEW:
    case NMM_SAVEDEC:
      // not used
    break;
  }

  RETURN(wmData);
  return wmData;
}

///
/// MA_NewMessageFunc
HOOKPROTONHNO(MA_NewMessageFunc, void, int *arg)
{
  enum NewMailMode mode = arg[0];
  ULONG qual = arg[1];
  int flags;

  ENTER();

  // get the newmail flags depending on the currently
  // set qualifier keys. We submit these flags to the
  // NewMessage() function later on
  mode = CheckNewMailQualifier(mode, qual, &flags);

  // call the main NewMessage function which will
  // then in turn call the correct subfunction for
  // performing the mail action.
  NewMessage(mode, flags);

  LEAVE();
}
MakeHook(MA_NewMessageHook, MA_NewMessageFunc);

///
/// CheckNewMailQualifier
enum NewMailMode CheckNewMailQualifier(const enum NewMailMode mode, const ULONG qualifier, int *flags)
{
  enum NewMailMode newMode = mode;

  ENTER();

  // reset the flags
  *flags = 0;

  // depending on the newmode we go and check different qualifiers
  if(mode == NMM_FORWARD)
  {
    // if the user pressed LSHIFT or RSHIFT while pressing
    // the 'forward' toolbar we do a REDIRECT message action
    // instead.
    if(isAnyFlagSet(qualifier, (IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)))
      newMode = NMM_REDIRECT;
    else
    {
      // flag the forward message action to not
      // add any attachments from the original mail if
      // the CONTROL qualifier was pressed
      if(isFlagSet(qualifier, IEQUALIFIER_CONTROL))
        setFlag(*flags, NEWF_FWD_NOATTACH);

      // flag the foward message action to use the
      // alternative (not configured) forward mode
      if(isAnyFlagSet(qualifier, (IEQUALIFIER_LALT|IEQUALIFIER_RALT)))
        setFlag(*flags, NEWF_FWD_ALTMODE);
    }
  }
  else if(mode == NMM_FORWARD_ATTACH)
  {
    setFlag(*flags, NEWF_FWD_AS_ATTACHMENT);

    // flag the forward message action to not
    // add any attachments from the original mail if
    // the CONTROL qualifier was pressed
    if(isFlagSet(qualifier, IEQUALIFIER_CONTROL))
      setFlag(*flags, NEWF_FWD_NOATTACH);
  }
  else if(mode == NMM_FORWARD_INLINE)
  {
    setFlag(*flags, NEWF_FWD_INLINED);

    // flag the forward message action to not
    // add any attachments from the original mail if
    // the CONTROL qualifier was pressed
    if(isFlagSet(qualifier, IEQUALIFIER_CONTROL))
      setFlag(*flags, NEWF_FWD_NOATTACH);
  }
  else if(mode == NMM_REPLY)
  {
    // flag the reply mail action to reply to the
    // sender of the mail directly
    if(isAnyFlagSet(qualifier, (IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)))
      setFlag(*flags, NEWF_REP_PRIVATE);

    // flag the reply mail action to reply to the mailing list
    // address instead.
    if(isAnyFlagSet(qualifier, (IEQUALIFIER_LALT|IEQUALIFIER_RALT)))
      setFlag(*flags, NEWF_REP_MLIST);

    // flag the reply mail action to not quote any text
    // of the original mail.
    if(isFlagSet(qualifier, IEQUALIFIER_CONTROL))
      setFlag(*flags, NEWF_REP_NOQUOTE);
  }

  RETURN(newMode);
  return newMode;
}

///
/// MA_DeleteMessage
//  Deletes selected messages
void MA_DeleteMessage(BOOL delatonce, BOOL force)
{
  struct Folder *delfolder;

  ENTER();

  if(GetCurrentFolder() != NULL && (delfolder = FO_GetFolderByType(FT_TRASH, NULL)) != NULL)
  {
    struct MA_GUIData *gui = &G->MA->GUI;
    Object *lv = gui->PG_MAILLIST;
    struct MailList *mlist;

    // create a list of all selected mails first
    if((mlist = MA_CreateMarkedList(lv, FALSE)) != NULL)
    {
      BOOL okToDelete = TRUE;

      // if there are more mails selected than the user allowed to be deleted
      // silently then ask him first
      if(C->Confirm == TRUE && mlist->count >= (ULONG)C->ConfirmDelete && force == FALSE)
      {
        char buffer[SIZE_DEFAULT];

        snprintf(buffer, sizeof(buffer), tr(MSG_MA_CONFIRMDELETION), mlist->count);

        if(MUI_Request(G->App, G->MA->GUI.WI, MUIF_NONE, tr(MSG_MA_ConfirmReq), tr(MSG_YesNoReq2), buffer) == 0)
          okToDelete = FALSE;
      }

      if(okToDelete == TRUE)
      {
        struct Folder *folder = GetCurrentFolder();
        struct BusyNode *busy = NULL;
        char selectedStr[SIZE_SMALL];
        struct MailNode *mnode;
        ULONG deleted;
        BOOL ignoreall = FALSE;
        ULONG delFlags = (delatonce == TRUE) ? DELF_AT_ONCE|DELF_QUIET|DELF_CLOSE_WINDOWS|DELF_UPDATE_APPICON|DELF_CHECK_CONNECTIONS : DELF_QUIET|DELF_CLOSE_WINDOWS|DELF_UPDATE_APPICON|DELF_CHECK_CONNECTIONS;
        LONG selectNext = -1;

        D(DBF_MAIL, "going to delete %ld mails from folder '%s'", mlist->count, folder->Name);

        if(mlist->count == 1)
        {
          // there is only one mail to be deleted
          // now determine the next to be selected mail
          if(xget(G->MA->GUI.PG_MAILLIST, MUIA_MainMailList_SortOrderReversed) == TRUE)
          {
            LONG pos;

            pos = xget(G->MA->GUI.PG_MAILLIST, MUIA_NList_Active);
            if(pos >= 1)
              selectNext = pos-1;
          }
        }

        set(lv, MUIA_NList_Quiet, TRUE);

        // modify the toolbar buttons, if the toolbar is visible
        if(gui->TO_TOOLBAR != NULL)
          DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_DELETE, MUIA_TheBar_Attr_Disabled, TRUE);

        // modify the menu items
        set(gui->MI_DELETE, MUIA_Menuitem_Enabled, FALSE);

        // clear the readmail group ahead of the deletion. This is necessary, because deleting mails
        // from the context menu might trigger a redraw of certain objects if these were covered by
        // the menu. For the active mail this might be lethal if it gets deleted at first. In this
        // case the redraw of the header lines list will access invalid memory as the mail plus all
        // dynamically allocated structures are no longer available, but YAM did not yet activate
        // another mail.
        if(C->EmbeddedReadPane == TRUE)
          DoMethod(gui->MN_EMBEDDEDREADPANE, MUIM_ReadMailGroup_Clear, MUIF_NONE);

        // show the abortable busy bar only if we are deleting more than a single mail
        if(mlist->count != 1)
        {
          snprintf(selectedStr, sizeof(selectedStr), "%d", (int)mlist->count);
          busy = BusyBegin(BUSY_PROGRESS_ABORT);
          BusyText(busy, tr(MSG_BusyDeleting), selectedStr);
        }

        deleted = 0;
        ForEachMailNode(mlist, mnode)
        {
          struct Mail *mail = mnode->mail;

          if(isSendMDNMail(mail) && ignoreall == FALSE &&
             (hasStatusNew(mail) || !hasStatusRead(mail)))
          {
            ignoreall = RE_ProcessMDN(MDN_MODE_DELETE, mail, (mlist->count >= 2), FALSE, G->MA->GUI.WI);
          }

          // call our subroutine with quiet option
          MA_DeleteSingle(mail, delFlags);

          // if BusyProgress() returns FALSE, then the user aborted
          if(BusyProgress(busy, ++deleted, mlist->count) == FALSE)
            break;
        }

        BusyEnd(busy);

        if(selectNext != -1)
          set(G->MA->GUI.PG_MAILLIST, MUIA_NList_Active, selectNext);

        set(lv, MUIA_NList_Quiet, FALSE);

        // modify the toolbar buttons, if the toolbar is visible
        if(gui->TO_TOOLBAR != NULL)
          DoMethod(gui->TO_TOOLBAR, MUIM_TheBar_SetAttr, TB_MAIN_DELETE, MUIA_TheBar_Attr_Disabled, FALSE);

        // modify the menu items
        set(gui->MI_DELETE, MUIA_Menuitem_Enabled, TRUE);

        if(delatonce == TRUE || C->RemoveAtOnce == TRUE || folder == delfolder || isSpamFolder(folder))
          AppendToLogfile(LF_NORMAL, 20, tr(MSG_LOG_Deleting), deleted, folder->Name);
        else
          AppendToLogfile(LF_NORMAL, 22, tr(MSG_LOG_Moving), deleted, folder->Name, delfolder->Name);

        // update the stats for the deleted folder,
        // but only if it isn't the current one and only
        // if the mail was not instantly deleted without moving
        // it to the delfolder
        if(delatonce == FALSE && delfolder != folder)
          DisplayStatistics(delfolder, FALSE);

        // then update the statistics for the folder we moved the
        // mail from as well.
        DisplayStatistics(folder, TRUE);

        MA_ChangeSelected(FALSE);
      }

      // free the mail list again
      DeleteMailList(mlist);
    }
  }

  LEAVE();
}

///
/// MA_DeleteMessageFunc
HOOKPROTONHNO(MA_DeleteMessageFunc, void, int *arg)
{
   BOOL delatonce = isAnyFlagSet(arg[0], (IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT));

   MA_DeleteMessage(delatonce, FALSE);
}
MakeHook(MA_DeleteMessageHook, MA_DeleteMessageFunc);

///
/// MA_ClassifyMessage
//  Classifies a message and moves it to spam folder if spam
void MA_ClassifyMessage(enum BayesClassification bclass)
{
  struct Folder *spamFolder;
  struct Folder *incomingFolder;

  ENTER();

  spamFolder = FO_GetFolderByType(FT_SPAM, NULL);
  incomingFolder = FO_GetFolderByType(FT_INCOMING, NULL);

  if(GetCurrentFolder() != NULL && spamFolder != NULL && incomingFolder != NULL)
  {
    Object *lv = G->MA->GUI.PG_MAILLIST;
    struct MailList *mlist;

    if((mlist = MA_CreateMarkedList(lv, FALSE)) != NULL)
    {
      struct BusyNode *busy;
      char selectedStr[SIZE_SMALL];
      struct MailNode *mnode;
      ULONG i;
      LONG selectNext = -1;

      if(mlist->count == 1)
      {
        // there is only one mail to be reclassified
        // now determine the next to be selected mail
        if(xget(G->MA->GUI.PG_MAILLIST, MUIA_MainMailList_SortOrderReversed) == TRUE)
        {
          LONG pos;

          pos = xget(G->MA->GUI.PG_MAILLIST, MUIA_NList_Active);
          if(pos >= 1)
            selectNext = pos-1;
        }
      }

      set(lv, MUIA_NList_Quiet, TRUE);
      snprintf(selectedStr, sizeof(selectedStr), "%d", (int)mlist->count);
      busy = BusyBegin(BUSY_PROGRESS_ABORT);
      BusyText(busy, tr(MSG_BusyMoving), selectedStr);

      i = 0;
      ForEachMailNode(mlist, mnode)
      {
        struct Mail *mail = mnode->mail;

        if(mail != NULL)
        {
          if(hasStatusSpam(mail) == FALSE && bclass == BC_SPAM)
          {
            // mark the mail as spam
            AppendToLogfile(LF_VERBOSE, 90, tr(MSG_LOG_MAILISSPAM), AddrName(mail->From), mail->Subject);
            BayesFilterSetClassification(mail, BC_SPAM);
            setStatusToUserSpam(mail);

            // move the mail
            MA_MoveCopySingle(mail, spamFolder, "mark as spam", MVCPF_CLOSE_WINDOWS);
          }
          else if(hasStatusHam(mail) == FALSE && bclass == BC_HAM)
          {
            // mark the mail as ham
            AppendToLogfile(LF_VERBOSE, 90, tr(MSG_LOG_MAILISNOTSPAM), AddrName(mail->From), mail->Subject);
            BayesFilterSetClassification(mail, BC_HAM);
            setStatusToHam(mail);

            // move the mail back to the Incoming folder, if requested
            if(C->MoveHamToIncoming == TRUE)
            {
              BOOL moveToIncoming = TRUE;

              // first try to apply the filters to this mail, if requested
              if(C->FilterHam == TRUE)
              {
                struct MinList *filterList;

                if((filterList = CloneFilterList(APPLY_USER)) != NULL)
                {
                  // FI_FilterSingleMail() returns TRUE if the filters didn't move or delete the mail.
                  // If the mail is still in place after filtering we will move it back to the incoming
                  // folder later.
                  moveToIncoming = FI_FilterSingleMail(filterList, mail, NULL, NULL);
                  DeleteFilterList(filterList);
                }
              }

              // if the mail has not been moved to another folder before we move it to the incoming folder now.
              if(moveToIncoming == TRUE)
                MA_MoveCopySingle(mail, incomingFolder, "mark as ham", MVCPF_CLOSE_WINDOWS);
              else
                selectNext = -1;
            }
          }
          else
            selectNext = -1;
        }

        // if BusyProgress() returns FALSE, then the user aborted
        if(BusyProgress(busy, ++i, mlist->count) == FALSE)
          break;
      }
      BusyEnd(busy);

      if(selectNext != -1)
        set(lv, MUIA_NList_Active, selectNext);

      set(lv, MUIA_NList_Quiet, FALSE);

      DeleteMailList(mlist);

      AppendToLogfile(LF_NORMAL, 22, tr(MSG_LOG_Moving), i, GetCurrentFolder()->Name, spamFolder->Name);
      DisplayStatistics(spamFolder, FALSE);
      DisplayStatistics(incomingFolder, FALSE);

      DisplayStatistics(NULL, TRUE);
      // force an update of the toolbar
      MA_ChangeSelected(TRUE);
    }
  }

  LEAVE();
}

///
/// MA_ClassifyMessageFunc
HOOKPROTONHNO(MA_ClassifyMessageFunc, void, int *arg)
{
   MA_ClassifyMessage(arg[0]);
}
MakeHook(MA_ClassifyMessageHook, MA_ClassifyMessageFunc);

///
/// MA_GetAddress
//  Stores address from a list of messages to the address book
void MA_GetAddress(struct MailList *mlist, struct MUI_NListtree_TreeNode *dropTarget, ULONG dropType)
{
  struct MailNode *mnode = FirstMailNode(mlist);
  struct Mail *mail = mnode->mail;
  struct Folder *folder = mail->Folder;
  BOOL isSentFolder = (folder != NULL) ? isSentMailFolder(folder) : FALSE;
  BOOL isMLFolder = (folder != NULL) ? folder->MLSupport : FALSE;
  struct ExtendedMail *email;
  struct Person *pe = NULL;
  struct ABookNode abn;

  ENTER();

  // check whether we want to create a single addressbook
  // entry or a list of addresses
  if(mlist->count == 1 && !(isSentFolder == TRUE && isMultiRCPTMail(mail)))
  {
    if(isSentFolder == TRUE)
      pe = &mail->To;
    else
    {
      // now ask the user which one of the two
      // adresses it should consider for adding it to the
      // addressbook
      if(isMLFolder == FALSE &&
         C->CompareAddress == TRUE && mail->ReplyTo.Address[0] != '\0' &&
         stricmp(mail->From.Address, mail->ReplyTo.Address) != 0)
      {
        char buffer[SIZE_LARGE];
        snprintf(buffer, sizeof(buffer), tr(MSG_MA_ADD_WHICH_ADDRESS), mail->From.Address, mail->ReplyTo.Address);

        switch(MUI_Request(G->App, G->MA->GUI.WI, MUIF_NONE, NULL, tr(MSG_MA_Compare2ReqOpt), buffer))
        {
          case 2: pe = &mail->ReplyTo; break;
          case 1: pe = &mail->From; break;
          case 0:
          {
            // make a user abort
            LEAVE();
            return;
          }
        }
      }
      else if(isMLFolder == FALSE && mail->ReplyTo.Address[0] != '\0')
        pe = &mail->ReplyTo;
      else
        pe = &mail->From;
    }

    InitABookNode(&abn, ABNT_USER);

    // if there is a "," in the realname of the new address
    // we have to encapsulate it in quotes
    if(strchr(pe->RealName, ','))
      snprintf(abn.RealName, sizeof(abn.RealName), "\"%s\"", pe->RealName);
    else
      strlcpy(abn.RealName, pe->RealName, sizeof(abn.RealName));

    strlcpy(abn.Address, pe->Address, sizeof(abn.Address));
  }
  else
  {
    InitABookNode(&abn, ABNT_LIST);

    LockMailListShared(mlist);

    ForEachMailNode(mlist, mnode)
    {
      char address[SIZE_LARGE];

      mail = mnode->mail;

      if(isSentFolder == TRUE)
      {
        dstrcat(&abn.ListMembers, BuildAddress(address, sizeof(address), mail->To.Address, mail->To.RealName));
        dstrcat(&abn.ListMembers, "\n");

        if(isMultiRCPTMail(mail) &&
           (email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)) != NULL)
        {
          int j;

          for(j=0; j < email->NumSTo; j++)
          {
            dstrcat(&abn.ListMembers, BuildAddress(address, sizeof(address), email->STo[j].Address, email->STo[j].RealName));
            dstrcat(&abn.ListMembers, "\n");
          }

          for(j=0; j < email->NumCC; j++)
          {
            dstrcat(&abn.ListMembers, BuildAddress(address, sizeof(address), email->CC[j].Address, email->CC[j].RealName));
            dstrcat(&abn.ListMembers, "\n");
          }

          MA_FreeEMailStruct(email);
        }
      }
      else
      {
        // now we check whether the mail got ReplyTo addresses which we should add
        // or if we should add all From: addresses
        if(isMLFolder == FALSE && mail->ReplyTo.Address[0] != '\0')
        {
          dstrcat(&abn.ListMembers, BuildAddress(address, sizeof(address), mail->ReplyTo.Address, mail->ReplyTo.RealName));
          dstrcat(&abn.ListMembers, "\n");

          if(isMultiReplyToMail(mail) &&
             (email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)) != NULL)
          {
            int j;

            for(j=0; j < email->NumSReplyTo; j++)
            {
              dstrcat(&abn.ListMembers, BuildAddress(address, sizeof(address), email->SReplyTo[j].Address, email->SReplyTo[j].RealName));
              dstrcat(&abn.ListMembers, "\n");
            }

            MA_FreeEMailStruct(email);
          }
        }
        else
        {
          // there seem to exist no ReplyTo: addresses, so lets go and
          // add all From: addresses to our addressbook.
          dstrcat(&abn.ListMembers, BuildAddress(address, sizeof(address), mail->From.Address, mail->From.RealName));
          dstrcat(&abn.ListMembers, "\n");

          if(isMultiSenderMail(mail) &&
             (email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)) != NULL)
          {
            int j;

            for(j=0; j < email->NumSFrom; j++)
            {
              dstrcat(&abn.ListMembers, BuildAddress(address, sizeof(address), email->SFrom[j].Address, email->SFrom[j].RealName));
              dstrcat(&abn.ListMembers, "\n");
            }

            MA_FreeEMailStruct(email);
          }
        }
      }
    }

    UnlockMailList(mlist);
  }

  // lets open an addressbook window or bring one to the front
  if(DoMethod(G->App, MUIM_YAMApplication_OpenAddressBookWindow, ABM_EDIT, -1, NULL) == TRUE)
    DoMethod(G->ABookWinObject, MUIM_AddressBookWindow_EditNewEntry, &abn, dropTarget, dropType);

  // free the list members, it is handled now by the edit window
  dstrfree(abn.ListMembers);

  LEAVE();
}

///
/// MA_GetAddressFunc
//  Stores addresses from selected messages to the address book
HOOKPROTONHNONP(MA_GetAddressFunc, void)
{
  struct MailList *mlist;

  ENTER();

  if((mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE)) != NULL)
  {
    MA_GetAddress(mlist, NULL, 0);
    DeleteMailList(mlist);
  }

  LEAVE();
}
MakeHook(MA_GetAddressHook, MA_GetAddressFunc);

///
/// MA_ExchangeMail
//  send and get mails
void MA_ExchangeMail(const ULONG receiveFlags)
{
  ENTER();

  MA_PopNow(NULL, receiveFlags, NULL);
  MA_Send(isFlagSet(receiveFlags, RECEIVEF_USER) ? SENDMAIL_ALL_USER : SENDMAIL_ALL_AUTO, SENDF_SIGNAL);

  LEAVE();
}

///
/// ReceiveMailsFromPOP
BOOL ReceiveMailsFromPOP(struct MailServerNode *msn, const ULONG flags, struct DownloadResult *dlResult)
{
  BOOL success = FALSE;

  ENTER();

  LockMailServer(msn);
  if(msn->useCount == 0)
  {
    // mark the server as being "in use"
    msn->useCount = 1;

    success = (DoAction(NULL, TA_ReceiveMails, TT_ReceiveMails_MailServer, msn,
                                               TT_ReceiveMails_Flags, flags,
                                               TT_ReceiveMails_Result, dlResult,
                                               TAG_DONE) != NULL);

    if(success == FALSE)
    {
      // clear the "in use" flag again in case of an error
      msn->useCount = 0;
    }
  }
  else
    W(DBF_NET, "server '%s' is already in use", msn->description);
  UnlockMailServer(msn);

  RETURN(success);
  return success;
}

///
/// MA_PopNow
//  Fetches new mail from POP3 account(s)
BOOL MA_PopNow(struct MailServerNode *msn, const ULONG flags, struct DownloadResult *dlResult)
{
  BOOL success = FALSE;

  ENTER();

  // make sure the mail server nodes do not vanish
  ObtainSemaphoreShared(G->configSemaphore);

  // the USER flag must be checked at last, because STARTUP and USER might be used together
  if(isFlagSet(flags, RECEIVEF_STARTUP))
    DoMethod(G->App, MUIM_YAMApplication_StartMacro, MACRO_PREGET, "1");
  else if(isFlagSet(flags, RECEIVEF_TIMER))
    DoMethod(G->App, MUIM_YAMApplication_StartMacro, MACRO_PREGET, "2");
  else if(isFlagSet(flags, RECEIVEF_AREXX))
    DoMethod(G->App, MUIM_YAMApplication_StartMacro, MACRO_PREGET, "3");
  else if(isFlagSet(flags, RECEIVEF_USER))
    DoMethod(G->App, MUIM_YAMApplication_StartMacro, MACRO_PREGET, "0");

  if(msn == NULL)
  {
    success = TRUE;

    IterateList(&C->pop3ServerList, struct MailServerNode *, msn)
    {
      // fetch mails from active servers only
      if(isServerActive(msn) == TRUE)
      {
        // download mails from this server if this either is no startup action
        // or "download on startup" is enabled
        if(isFlagClear(flags, RECEIVEF_STARTUP) || hasServerDownloadOnStartup(msn) == TRUE)
          success &= ReceiveMailsFromPOP(msn, flags, dlResult);
      }
      else
        W(DBF_NET, "POP3 server of identity '%s' is inactive'", msn->description);
    }
  }
  else
  {
    // we ignore the active state and possible startup flags for single servers
    success = ReceiveMailsFromPOP(msn, flags, dlResult);
  }

  // now we are done
  ReleaseSemaphore(G->configSemaphore);

  RETURN(success);
  return success;
}

///
/// MA_PopNowFunc
HOOKPROTONHNO(MA_PopNowFunc, void, int *arg)
{
  ULONG qual = (ULONG)arg[2];

  ENTER();

  // if the "get" button was clicked while a shift button was
  // pressed then a mail exchange is done rather than a simple
  // download of mails
  if(isAnyFlagSet(qual, (IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)))
    MA_ExchangeMail(arg[1]);
  else
    MA_PopNow((struct MailServerNode *)arg[0], arg[1], NULL);

  LEAVE();
}
MakeHook(MA_PopNowHook, MA_PopNowFunc);

///

/*** Sub-button functions ***/
/// MA_Send
//  Sends selected or all messages
BOOL MA_Send(enum SendMailMode mode, ULONG flags)
{
  BOOL success = FALSE;
  struct MailList *mlist = NULL;
  struct Folder *outfolder = FO_GetFolderByType(FT_OUTGOING, NULL);

  ENTER();

  D(DBF_MAIL, "send mails from outgoing folder '%s', current folder is '%s', mode %ld", outfolder->Name, GetCurrentFolder()->Name, mode);

  // make sure the mail server nodes do not vanish
  ObtainSemaphoreShared(G->configSemaphore);

  // now we get the list of all/selected mails
  switch(mode)
  {
    case SENDMAIL_ALL_USER:
    case SENDMAIL_ALL_AUTO:
      mlist = MA_CreateFullList(outfolder, FALSE);
    break;

    case SENDMAIL_ACTIVE_USER:
    case SENDMAIL_ACTIVE_AUTO:
    {
      if(outfolder == GetCurrentFolder())
        mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE);
    }
    break;
  }

  if(mlist != NULL)
  {
    struct WriteMailData *wmData;

    // Now check if any mail currently being edited is contained in the list of
    // mails to be sent. This may happen if a mail has been put on hold before
    // and is edited again now. If the user hits the "Send" button in this moment
    // the currently edited mail should not be sent.
    IterateList(&G->writeMailDataList, struct WriteMailData *, wmData)
    {
      struct MailNode *mnode;

      ForEachMailNode(mlist, mnode)
      {
        struct Mail *mail = mnode->mail;

        if(mail == wmData->refMail)
        {
          // remove the mail from the list
          RemoveMailNode(mlist, mnode);
          break;
        }
      }
    }

    D(DBF_MAIL, "%ld mails waiting to be sent", mlist->count);

    // if there is anything todo we need to find out
    // which user identity a certain mail belongs to (by using MA_ExamineMail)
    // and then create a subgroup of mlist with them.
    if(mlist->count != 0)
    {
      struct MailList **mailsToSend;
      ULONG numUINs = NumberOfUserIdentities(&C->userIdentityList);

      // create an array of mail lists for all user identities
      if((mailsToSend = calloc(numUINs, sizeof(*mailsToSend))) != NULL)
      {
        struct MailNode *mnode;
        struct MailNode *succ;
        ULONG i;

        SafeIterateList(mlist, struct MailNode *, mnode, succ)
        {
          struct Mail *mail = mnode->mail;
          struct ExtendedMail *email;

          // we need to use ExamineMail here so that it parses all headers and
          // eventually stores the identityID in the email structure
          if((email = MA_ExamineMail(mail->Folder, mail->MailFile, TRUE)) != NULL)
          {
            // make sure the identity exists
            if(email->identity != NULL)
            {
              LONG idx;

              if((idx = IndexOfUserIdentity(&C->userIdentityList, email->identity)) != -1)
              {
                // check if the mail is being sent already
                LockMailList(G->mailsInTransfer);
                if(FindMailByAddress(G->mailsInTransfer, mnode->mail) == NULL)
                {
                  // create a new mail list if this user identity
                  // hasn't one yet
                  if(mailsToSend[idx] == NULL)
                    mailsToSend[idx] = CreateMailList();

                  if(mailsToSend[idx] != NULL)
                  {
                    // remove from global list add the current mail to the list
                    RemoveMailNode(mlist, mnode);
                    AddMailNode(mailsToSend[idx], mnode);

                    // add the mail to the global list of mails being sent currently
                    AddNewMailNode(G->mailsInTransfer, mnode->mail);
                  }
                }

                UnlockMailList(G->mailsInTransfer);
              }
            }
            else
              E(DBF_MAIL, "no user identity found for mail with subject '%s'", mail->Subject);

            // free the ExtendedMail struct
            MA_FreeEMailStruct(email);
          }
          else
            W(DBF_MAIL, "ExamineMail() returned NULL");
        }

        // make sure mlist is empty after the move operations
        if(IsMailListEmpty(mlist) == FALSE)
          E(DBF_MAIL, "couldn't move all mail from main mail list to submail lists");

        // now we walk through our user identities and if
        // they have a non empty sentMailList we start the mail transfer
        success = TRUE;
        for(i = 0; i < numUINs; i++)
        {
          BOOL sendMailSuccess = FALSE;

          if(mailsToSend[i] != NULL && IsMailListEmpty(mailsToSend[i]) == FALSE)
          {
            struct UserIdentityNode *uin = GetUserIdentity(&C->userIdentityList, i, TRUE);

            D(DBF_MAIL, "%ld mails waiting to be sent to SMTP server '%s'", mailsToSend[i]->count, uin->smtpServer->description);

            // mark the server as "in use"
            LockMailServer(uin->smtpServer);
            uin->smtpServer->useCount++;

            // call the thread action now signaling that we want to sent the
            // associated mails
            sendMailSuccess = (DoAction(NULL, TA_SendMails,
              TT_SendMails_UserIdentity, uin,
              TT_SendMails_Mails, mailsToSend[i],
              TT_SendMails_Mode, mode,
              TT_SendMails_Flags, flags,
              TAG_DONE) != NULL);

            if(sendMailSuccess == FALSE)
            {
              // reset everything in case of failure
              E(DBF_MAIL, "sending to SMTP server '%s' failed", uin->smtpServer->description);
              uin->smtpServer->useCount--;
              CleanMailsInTransfer(mailsToSend[i]);

              success = FALSE;
            }
            else
            {
              // starting the thread succeeded, the list of mails will be deleted within
              // the thread, thus we must erase the pointer here to avoid deleting it
              // below
              mailsToSend[i] = NULL;
            }

            UnlockMailServer(uin->smtpServer);
          }
        }

        for(i = 0; i < numUINs; i++)
        {
          if(mailsToSend[i] != NULL)
            DeleteMailList(mailsToSend[i]);
        }

        free(mailsToSend);
      }
    }

    // delete the mlist mail list as this is not required anymore
    DeleteMailList(mlist);
  }
  else
    W(DBF_MAIL, "failed to create mail list for folder '%s'", outfolder->Name);

  // now we are done
  ReleaseSemaphore(G->configSemaphore);

  RETURN(success);
  return success;
}

///
/// MA_SendHook
HOOKPROTONHNO(MA_SendFunc, void, int *arg)
{
  ENTER();

  MA_Send(arg[0], SENDF_SIGNAL);

  LEAVE();
}
MakeHook(MA_SendHook, MA_SendFunc);

///

/*** Menu options ***/
/// MA_SetStatusTo
//  Sets status of selectes messages
void MA_SetStatusTo(int addflags, int clearflags, BOOL all)
{
  Object *lv = G->MA->GUI.PG_MAILLIST;
  struct MailList *mlist;

  ENTER();

  // generate a mail list of either all or just the selected
  // (marked) mails.
  if(all == TRUE)
    mlist = MA_CreateFullList(GetCurrentFolder(), FALSE);
  else
    mlist = MA_CreateMarkedList(lv, FALSE);

  if(mlist != NULL)
  {
    struct MailNode *mnode;

    set(lv, MUIA_NList_Quiet, TRUE);

    ForEachMailNode(mlist, mnode)
    {
      MA_ChangeMailStatus(mnode->mail, addflags, clearflags);
    }

    set(lv, MUIA_NList_Quiet, FALSE);

    DeleteMailList(mlist);
    DisplayStatistics(NULL, TRUE);
  }

  LEAVE();
}

///
/// MA_SetStatusToFunc
HOOKPROTONHNO(MA_SetStatusToFunc, void, int *arg)
{
  MA_SetStatusTo(arg[0], arg[1], FALSE);
}
MakeHook(MA_SetStatusToHook, MA_SetStatusToFunc);

///
/// MA_SetAllStatusToFunc
HOOKPROTONHNO(MA_SetAllStatusToFunc, void, int *arg)
{
  MA_SetStatusTo(arg[0], arg[1], TRUE);
}
MakeHook(MA_SetAllStatusToHook, MA_SetAllStatusToFunc);

///
/// MA_RescanIndexFunc
//  Updates index of current folder
HOOKPROTONHNONP(MA_RescanIndexFunc, void)
{
  struct Folder *folder = GetCurrentFolder();

  ENTER();

  // on groups we don't allow any index rescanning operation
  if(folder != NULL && isGroupFolder(folder) == FALSE)
  {
    // we start a rescan by expiring the current index and issueing
    // a new MA_GetIndex(). That will also cause the GUI to refresh!
    folder->LoadedMode = LM_UNLOAD;

    MA_ExpireIndex(folder);
    if(MA_GetIndex(folder) == TRUE)
    {
      // if we are still in the folder we wanted to rescan,
      // we can refresh the list.
      if(folder == GetCurrentFolder())
        MA_ChangeFolder(NULL, FALSE);
    }
  }

  LEAVE();
}
MakeHook(MA_RescanIndexHook, MA_RescanIndexFunc);

///
/// MA_ExportMessages
//  Saves messages to a MBOX mailbox file
BOOL MA_ExportMessages(char *filename, const BOOL all, ULONG flags)
{
  BOOL success = FALSE;
  char outname[SIZE_PATHFILE];
  struct MailList *mlist;

  ENTER();

  // check that a real folder is active
  if(GetCurrentFolder() != NULL && isGroupFolder(GetCurrentFolder()) == FALSE)
  {
    if(all == TRUE)
      mlist = MA_CreateFullList(GetCurrentFolder(), FALSE);
    else
      mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE);

    if(mlist != NULL)
    {
      if(filename == NULL)
      {
        struct FileReqCache *frc;
        char suggestedName[SIZE_FILE];

        if(mlist->count == 1)
        {
          // if there is only one mail to be exported by we use single mail's subject
          // as filename and append ".eml"
          struct MailNode *mnode = FirstMailNode(mlist);

          snprintf(suggestedName, sizeof(suggestedName), "%s.eml", mnode->mail->Subject);
        }
        else
        {
          // for multiple mail we use the folder's name and append ".mbox"
          snprintf(suggestedName, sizeof(suggestedName), "%s.mbox", GetCurrentFolder()->Name);
        }

        // remove possible invalid characters
        ReplaceInvalidChars(suggestedName);

        if((frc = ReqFile(ASL_EXPORT, G->MA->GUI.WI, tr(MSG_MA_MESSAGEEXPORT), REQF_SAVEMODE, C->DetachDir, suggestedName)) != NULL)
        {
          // avoid empty file names
          if(frc->file == NULL || frc->file[0] == '\0')
            filename = NULL;
          else
            filename = AddPath(outname, frc->drawer, frc->file, sizeof(outname));

          // now check whether the file exists and ask if it should be overwritten
          if(FileExists(filename) == TRUE)
          {
            switch(MUI_Request(G->App, G->MA->GUI.WI, MUIF_NONE, tr(MSG_MA_MESSAGEEXPORT), tr(MSG_MA_ExportAppendOpts), tr(MSG_MA_ExportAppendReq)))
            {
              case 1: clearFlag(flags, EXPORTF_APPEND); break;
              case 2: setFlag(flags, EXPORTF_APPEND); break;
              case 0: filename = NULL;
            }
          }
        }
      }

      if(filename != NULL)
      {
        success = (DoAction(NULL, TA_ExportMails, TT_ExportMails_File, filename,
                                                  TT_ExportMails_Mails, mlist,
                                                  TT_ExportMails_Flags, flags,
                                                  TAG_DONE) != NULL);
      }
    }
  }

  RETURN(success);
  return success;
}

///
/// MA_ExportMessagesFunc
HOOKPROTONHNO(MA_ExportMessagesFunc, void, int *arg)
{
   MA_ExportMessages(NULL, arg[0] != 0, 0);
}
MakeHook(MA_ExportMessagesHook, MA_ExportMessagesFunc);

///
/// MA_ImportMessages
//  Imports messages from a MBOX mailbox file
BOOL MA_ImportMessages(const char *fname, const ULONG flags)
{
  BOOL result = FALSE;

  ENTER();

  // check that a real folder is active
  if(GetCurrentFolder() != NULL && isGroupFolder(GetCurrentFolder()) == FALSE)
  {
    result = (DoAction(NULL, TA_ImportMails, TT_ImportMails_File, fname,
                                             TT_ImportMails_Folder, GetCurrentFolder(),
                                             TT_ImportMails_Flags, flags,
                                             TAG_DONE) != NULL);
  }

  RETURN(result);
  return result;
}

///
/// MA_ImportMessagesFunc
HOOKPROTONHNONP(MA_ImportMessagesFunc, void)
{
  ENTER();

  if(GetCurrentFolder() != NULL && isGroupFolder(GetCurrentFolder()) == FALSE)
  {
    struct FileReqCache *frc;

    // pop up a requester to query the user for the input file.
    if((frc = ReqFile(ASL_IMPORT, G->MA->GUI.WI, tr(MSG_MA_MessageImport), REQF_MULTISELECT, C->DetachDir, "")))
    {
      char filename[SIZE_PATHFILE];

      if(frc->numArgs == 0)
      {
        D(DBF_GUI, "chosen file: [%s] from drawer: [%s]", frc->file, frc->drawer);

        AddPath(filename, frc->drawer, frc->file, sizeof(filename));

        // now start the actual importing of the messages
        if(MA_ImportMessages(filename, 0) == FALSE)
          ER_NewError(tr(MSG_ER_MESSAGEIMPORT), filename);
      }
      else
      {
        int i;

        for(i=0; i < frc->numArgs; i++)
        {
          D(DBF_GUI, "chosen file: [%s] from drawer: [%s]", frc->argList[i], frc->drawer);

          AddPath(filename, frc->drawer, frc->argList[i], sizeof(filename));

          // now start the actual importing of the messages
          if(MA_ImportMessages(filename, 0) == FALSE)
            ER_NewError(tr(MSG_ER_MESSAGEIMPORT), filename);
        }
      }
    }
  }

  LEAVE();
}
MakeStaticHook(MA_ImportMessagesHook, MA_ImportMessagesFunc);

///
/// MA_MoveMessageFunc
//  Moves selected messages to a user specified folder
HOOKPROTONHNONP(MA_MoveMessageFunc, void)
{
  struct Folder *dst;

  ENTER();

  if((dst = FolderRequest(tr(MSG_MA_MoveMsg), tr(MSG_MA_MoveMsgReq), tr(MSG_MA_MoveGad), tr(MSG_Cancel), NULL, G->MA->GUI.WI)) != NULL)
    MA_MoveCopy(NULL, dst, "manual move", MVCPF_CLOSE_WINDOWS);

  LEAVE();
}
MakeHook(MA_MoveMessageHook, MA_MoveMessageFunc);

///
/// MA_CopyMessageFunc
//  Copies selected messages to a user specified folder
HOOKPROTONHNONP(MA_CopyMessageFunc, void)
{
  struct Folder *dst;

  ENTER();

  if((dst = FolderRequest(tr(MSG_MA_CopyMsg), tr(MSG_MA_MoveMsgReq), tr(MSG_MA_CopyGad), tr(MSG_Cancel), NULL, G->MA->GUI.WI)) != NULL)
    MA_MoveCopy(NULL, dst, "manual copy", MVCPF_COPY);

  LEAVE();
}
MakeHook(MA_CopyMessageHook, MA_CopyMessageFunc);

///
/// ArchiveMail
// move a mail to the archive folder
void MA_ArchiveMail(struct Mail *mail)
{
  ldiv_t d;
  char archiveDisplayName[SIZE_SMALL];
  char archivePathName[SIZE_PATH];
  struct Folder *archive;
  BOOL saveTree = FALSE;

  ENTER();

  // extract the year from the mail's date
  d = ldiv(DateStamp2Long(&mail->Date), 10000);
  // generate suitable names for the path and display name of the archive folder
  snprintf(archiveDisplayName, sizeof(archiveDisplayName), "%ld", d.rem);
  snprintf(archivePathName, sizeof(archivePathName), "%s%ld", FolderName[FT_ARCHIVE], d.rem);

  // hide the possible add and move operations within the listtree
  set(G->MA->GUI.LT_FOLDERS, MUIA_NListtree_Quiet, TRUE);

  // create the "Archive" folder group if it doesn't exist yet
  if(FO_GetFolderGroup(tr(MSG_MA_ARCHIVE), NULL) == NULL)
  {
    if(DoMethod(G->MA->GUI.LT_FOLDERS, MUIM_MainFolderListtree_NewFolderGroup, tr(MSG_MA_ARCHIVE)) != FALSE)
    {
      // move the new archive group folder after the sent folder
      struct Folder *this = FO_GetFolderGroup(tr(MSG_MA_ARCHIVE), NULL);
      struct Folder *prev = FO_GetFolderByType(FT_SENT, NULL);

      DoMethod(G->MA->GUI.LT_FOLDERS, MUIM_NListtree_Move, MUIV_NListtree_Move_OldListNode_Root, this->Treenode, MUIV_NListtree_Move_NewListNode_Root, prev->Treenode, MUIF_NONE);
      saveTree = TRUE;
    }
  }

  // create the year folder if it doesn't exist yet
  if(FO_GetFolderByPath(archivePathName, NULL) == NULL)
  {
    if(FO_CreateFolder(FT_ARCHIVE, archivePathName, archiveDisplayName) == TRUE)
    {
      // move the new archive year folder to the archive group folder
      struct Folder *this = FO_GetFolderByPath(archivePathName, NULL);
      struct Folder *group = FO_GetFolderGroup(tr(MSG_MA_ARCHIVE), NULL);

      // move the folder to the end of the group
      // Note: there is no point in trying to sort the folders as they can be rearranged
      // by the user which contradicts the requirement of a sorted list to be able
      // to insert the new entry at the correct position
      DoMethod(G->MA->GUI.LT_FOLDERS, MUIM_NListtree_Move, MUIV_NListtree_Move_OldListNode_Root, this->Treenode, group->Treenode, MUIV_NListtree_Move_NewTreeNode_Tail, MUIF_NONE);
      saveTree = TRUE;
    }
  }

  set(G->MA->GUI.LT_FOLDERS, MUIA_NListtree_Quiet, FALSE);

  if(saveTree == TRUE)
    FO_SaveTree();

  // finally move the mail to the archive folder
  if((archive = FO_GetFolderByPath(archivePathName, NULL)) != NULL)
  {
    // archived mails are considered to be read before, why should one
    // archive them otherwise without knowing the content?
    setStatusToRead(mail);
    MA_MoveCopy(mail, archive, "archive", 0);
  }

  LEAVE();
}

///
/// MA_ArchiveMessageFunc
// moves selected messages to the archive folder
HOOKPROTONHNONP(MA_ArchiveMessageFunc, void)
{
  struct MailList *mlist;

  ENTER();

  // get the list of the currently marked mails
  if((mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE)) != NULL)
  {
    struct BusyNode *busy;
    char selectedStr[SIZE_SMALL];
    struct MailNode *mnode;
    ULONG i;
    LONG selectNext = -1;

    if(mlist->count == 1)
    {
      // there is only one mail to be deleted and the embedded read pane is active
      // now determine the next to be selected mail
      if(xget(G->MA->GUI.PG_MAILLIST, MUIA_MainMailList_SortOrderReversed) == TRUE)
      {
        LONG pos;

        pos = xget(G->MA->GUI.PG_MAILLIST, MUIA_NList_Active);
        if(pos >= 1)
          selectNext = pos-1;
      }
    }

    snprintf(selectedStr, sizeof(selectedStr), "%ld", mlist->count);
    set(G->MA->GUI.PG_MAILLIST, MUIA_NList_Quiet, TRUE);
    busy = BusyBegin(BUSY_PROGRESS_ABORT);
    BusyText(busy, tr(MSG_BusyMoving), selectedStr);

    i = 0;
    ForEachMailNode(mlist, mnode)
    {
      if(mnode->mail != NULL)
        MA_ArchiveMail(mnode->mail);

      // if BusyProgress() returns FALSE, then the user aborted
      if(BusyProgress(busy, ++i, mlist->count) == FALSE)
        break;
    }
    BusyEnd(busy);

    if(selectNext != -1)
      set(G->MA->GUI.PG_MAILLIST, MUIA_NList_Active, selectNext);

    set(G->MA->GUI.PG_MAILLIST, MUIA_NList_Quiet, FALSE);

    DeleteMailList(mlist);
  }

  MA_ChangeSelected(FALSE);

  LEAVE();
}
MakeHook(MA_ArchiveMessageHook, MA_ArchiveMessageFunc);

///
/// MA_ChangeSubject
//  Changes subject of a message
void MA_ChangeSubject(struct Mail *mail, char *subj)
{
  ENTER();

  if(strcmp(subj, mail->Subject) != 0)
  {
    struct Folder *fo = mail->Folder;
    char oldfile[SIZE_PATHFILE];
    char fullfile[SIZE_PATHFILE];

    GetMailFile(oldfile, sizeof(oldfile), NULL, mail);
    if(StartUnpack(oldfile, fullfile, fo) != NULL)
    {
      char tfname[SIZE_MFILE];
      char newfile[SIZE_PATHFILE];
      FILE *newfh;

      snprintf(tfname, sizeof(tfname), "YAMt%08x.tmp", (unsigned int)GetUniqueID());
      AddPath(newfile, fo->Fullpath, tfname, sizeof(newfile));

      if((newfh = fopen(newfile, "w")) != NULL)
      {
        FILE *oldfh;
        LONG size;

        setvbuf(newfh, NULL, _IOFBF, SIZE_FILEBUF);

        if((oldfh = fopen(fullfile, "r")) != NULL)
        {
          char *buf = NULL;
          size_t buflen = 0;
          BOOL infield = FALSE;
          BOOL inbody = FALSE;
          BOOL hasorigsubj = FALSE;

          setvbuf(oldfh, NULL, _IOFBF, SIZE_FILEBUF);

          while(getline(&buf, &buflen, oldfh) > 0)
          {
            if(*buf == '\n' && inbody == FALSE)
            {
              inbody = TRUE;

              if(hasorigsubj == FALSE)
                EmitHeader(newfh, "X-Original-Subject", mail->Subject, G->writeCodeset);

              EmitHeader(newfh, "Subject", subj, G->writeCodeset);
            }

            if(!isspace(*buf))
            {
              infield = (strnicmp(buf, "subject:", 8) == 0);
              if(strnicmp(buf, "x-original-subject:", 19) == 0)
                hasorigsubj = TRUE;
            }

            if(infield == FALSE || inbody == TRUE)
              fputs(buf, newfh);
          }

          fclose(oldfh);
          DeleteFile(oldfile);

          free(buf);
        }
        fclose(newfh);

        if(ObtainFileInfo(newfile, FI_SIZE, &size) == TRUE)
        {
          fo->Size += size - mail->Size;
          mail->Size = size;
        }
        else
          mail->Size = -1;

        AppendToLogfile(LF_ALL, 82, tr(MSG_LOG_ChangingSubject), mail->Subject, mail->MailFile, fo->Name, subj);
        strlcpy(mail->Subject, subj, sizeof(mail->Subject));
        MA_ExpireIndex(fo);

        if(fo->Mode > FM_SIMPLE)
          DoPack(newfile, oldfile, fo);
        else
          RenameFile(newfile, oldfile);
      }

      FinishUnpack(fullfile);
    }
  }

  LEAVE();
}

///
/// MA_ChangeSubjectFunc
//  Changes subject of selected messages
HOOKPROTONHNONP(MA_ChangeSubjectFunc, void)
{
  struct MailList *mlist;

  ENTER();

  if((mlist = MA_CreateMarkedList(G->MA->GUI.PG_MAILLIST, FALSE)) != NULL)
  {
    struct MailNode *mnode;
    ULONG i;
    BOOL ask = TRUE;
    BOOL goOn = TRUE;
    char subj[SIZE_SUBJECT];

    i = 0;
    ForEachMailNode(mlist, mnode)
    {
      struct Mail *mail = mnode->mail;

      if(mail != NULL)
      {
        if(ask == TRUE)
        {
          strlcpy(subj, mail->Subject, sizeof(subj));

          switch(StringRequest(subj, SIZE_SUBJECT, tr(MSG_MA_ChangeSubj), tr(MSG_MA_ChangeSubjReq), tr(MSG_Okay), (i > 0 || mlist->count == 1) ? NULL : tr(MSG_MA_All), tr(MSG_Cancel), FALSE, G->MA->GUI.WI))
          {
            case 0:
            {
              goOn = FALSE;
            }
            break;

            case 2:
            {
              ask = FALSE;
            }
            break;

            default:
              // nothing
            break;
          }
        }

        if(goOn == TRUE)
          MA_ChangeSubject(mail, subj);
        else
          // the user cancelled the whole thing, bail out
          break;
      }

      i++;
    }

    DeleteMailList(mlist);

    DoMethod(G->MA->GUI.PG_MAILLIST, MUIM_NList_Redraw, MUIV_NList_Redraw_All);
  }

  LEAVE();
}
MakeHook(MA_ChangeSubjectHook, MA_ChangeSubjectFunc);

///
/// MA_GetRealSubject
//  Strips reply prefix / mailing list name from subject
char *MA_GetRealSubject(char *sub)
{
  char *p;
  int sublen;
  char *result = sub;

  ENTER();

  sublen = strlen(sub);

  if(sublen >= 3)
  {
    if(sub[2] == ':' && !sub[3])
    {
      result = (char *)"";
    }
    // check if the subject contains some strings embedded in brackets like [test]
    // and return only the real subject after the last bracket.
    else if(sub[0] == '[' && (p = strchr(sub, ']')) && p < (&sub[sublen])-3 && p < &sub[20])
    {
     // if the following char isn't a whitespace we return the real
     // subject directly after the last bracket
     if(isspace(p[1]))
       result = MA_GetRealSubject(p+2);
     else
       result = MA_GetRealSubject(p+1);
    }
    else if(strchr(":[({", sub[2]) != NULL)
    {
      if((p = strchr(sub, ':')) != NULL)
        result = MA_GetRealSubject(TrimStart(++p));
    }
  }

  RETURN(result);
  return result;
}

///

/*** Hooks ***/
/// MA_DelKey
//  User pressed DEL key
HOOKPROTONHNO(MA_DelKeyFunc, void, int *arg)
{
  Object *actobj;

  ENTER();

  actobj = (Object *)xget(G->MA->GUI.WI, MUIA_Window_ActiveObject);
  if(actobj == NULL || actobj == MUIV_Window_ActiveObject_None)
    actobj = (Object *)xget(G->MA->GUI.WI, MUIA_Window_DefaultObject);

  if(actobj == G->MA->GUI.LV_FOLDERS || actobj == G->MA->GUI.LT_FOLDERS)
  {
    DoMethod(G->MA->GUI.LT_FOLDERS, MUIM_MainFolderListtree_DeleteFolder);
  }
  else if(actobj == G->MA->GUI.PG_MAILLIST ||
          actobj == (Object *)xget(G->MA->GUI.PG_MAILLIST, MUIA_MainMailListGroup_ActiveListObject) ||
          actobj == (Object *)xget(G->MA->GUI.PG_MAILLIST, MUIA_MainMailListGroup_ActiveListviewObject) ||
          (C->EmbeddedReadPane == TRUE && (Object *)xget(G->MA->GUI.MN_EMBEDDEDREADPANE, MUIA_ReadMailGroup_ActiveObject) != NULL))
  {
    MA_DeleteMessage(arg[0], FALSE);
  }

  LEAVE();
}
MakeStaticHook(MA_DelKeyHook, MA_DelKeyFunc);

///
/// FollowThreadHook
//  Hook that is called to find the next/prev message in a thread and change to it
HOOKPROTONHNO(FollowThreadFunc, void, int *arg)
{
  int direction = arg[0];
  struct MA_GUIData *gui = &G->MA->GUI;
  struct Mail *mail = NULL;
  struct Mail *fmail;

  ENTER();

  // get the currently active mail entry.
  DoMethod(gui->PG_MAILLIST, MUIM_NList_GetEntry, MUIV_NList_GetEntry_Active, &mail);

  // depending on the direction we get the Question or Answer to the current Message
  if(mail != NULL && (fmail = FindThread(mail, direction > 0)) != NULL)
  {
    LONG pos = MUIV_NList_GetPos_Start;

    // we have to make sure that the folder where the message will be showed
    // from is active and ready to display the mail
    MA_ChangeFolder(fmail->Folder, TRUE);

    // get the position of the mail in the currently active listview
    DoMethod(gui->PG_MAILLIST, MUIM_NList_GetPos, fmail, &pos);

    // if the mail is displayed we make it the active one
    if(pos != MUIV_NList_GetPos_End)
      set(gui->PG_MAILLIST, MUIA_NList_Active, pos);
  }
  else
    DisplayBeep(_screen(gui->WI));


  LEAVE();
}
MakeStaticHook(FollowThreadHook, FollowThreadFunc);

///

/*** GUI ***/
/// MA_SetupDynamicMenus
//  Updates ARexx and POP3 account menu items
void MA_SetupDynamicMenus(void)
{
  Object *executeItem;

  ENTER();

  // update the "Forward" items' shortcut
  if(C->ForwardMode == FWM_ATTACH)
  {
    set(G->MA->GUI.MI_FORWARD_ATTACH, MUIA_Menuitem_Shortcut, "W");
    set(G->MA->GUI.MI_FORWARD_INLINE, MUIA_Menuitem_Shortcut, NULL);
  }
  else if(C->ForwardMode == FWM_INLINE)
  {
    set(G->MA->GUI.MI_FORWARD_ATTACH, MUIA_Menuitem_Shortcut, NULL);
    set(G->MA->GUI.MI_FORWARD_INLINE, MUIA_Menuitem_Shortcut, "W");
  }

  // generate the dynamic REXX Menu of the main window.
  // make sure we remove an old dynamic menu first
  if(G->MA->GUI.MN_REXX != NULL)
  {
    DoMethod(G->MA->GUI.MS_MAIN, MUIM_Family_Remove, G->MA->GUI.MN_REXX);
    MUI_DisposeObject(G->MA->GUI.MN_REXX);
  }

  // now we generate a new one.
  G->MA->GUI.MN_REXX = MenuObject,
    MUIA_Menu_Title, tr(MSG_MA_Scripts),
    MUIA_Menu_CopyStrings, FALSE,
    MenuChild, executeItem = Menuitem(tr(MSG_MA_ExecuteScript), "_", TRUE, NULL, MMEN_SCRIPT),
  End;

  if(G->MA->GUI.MN_REXX != NULL)
  {
    static const char *const shortcuts[MAXRX_MENU] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };
    int i;
    BOOL scriptsAdded = FALSE;

    // the first ten entries of our user definable
    // rexx script array is for defining rexx items
    // linked to the main menu.
    for(i=0; i < MAXRX_MENU; i++)
    {
      if(C->RX[i].Name[0] != '\0' && C->RX[i].Script[0] != '\0')
      {
        Object *newObj = Menuitem(C->RX[i].Name, shortcuts[i], TRUE, FALSE, MMEN_MACRO+i);

        if(newObj != NULL)
        {
          DoMethod(G->MA->GUI.MN_REXX, MUIM_Family_AddTail, newObj);
          // remember that we just added a custom scriptitem
          scriptsAdded = TRUE;
        }
      }
    }

    // add a separator bar only if at least one custom script item has been added
    if(scriptsAdded == TRUE)
    {
      Object *separator = MenuBarLabel;

      if(separator != NULL)
        DoMethod(G->MA->GUI.MN_REXX, MUIM_Family_Insert, separator, executeItem);
    }

    // add the new dynamic menu to our main menu
    DoMethod(G->MA->GUI.MS_MAIN, MUIM_Family_Insert, G->MA->GUI.MN_REXX, G->MA->GUI.MN_SETTINGS);
  }

  // dynamic Folder/Check menu items
  if(G->MA->GUI.MI_CSINGLE != NULL)
  {
    // no need to kill the notifications of each item, these die with their objects
    DoMethod(G->MA->GUI.MN_FOLDER, MUIM_Family_Remove, G->MA->GUI.MI_CSINGLE);
    MUI_DisposeObject(G->MA->GUI.MI_CSINGLE);
    G->MA->GUI.MI_CSINGLE = NULL;
  }

  G->MA->GUI.MI_CSINGLE = MenuitemObject,
    MUIA_Menuitem_Title, tr(MSG_MA_CheckSingle),
    MUIA_Menuitem_CopyStrings, FALSE,
  End;

  if(G->MA->GUI.MI_CSINGLE != NULL)
  {
    int i;
    struct MailServerNode *msn;

    // we iterate through our mail server list and ouput the POP3 servers in it
    i = 0;
    IterateList(&C->pop3ServerList, struct MailServerNode *, msn)
    {
      Object *newObj;

      newObj = Menuitem(msn->description, NULL, TRUE, FALSE, MMEN_POPHOST+i);
      if(newObj != NULL)
      {
        // add the new menu item to the sublist
        DoMethod(G->MA->GUI.MI_CSINGLE, MUIM_Family_AddTail, newObj);

        // add a notify for this item as well.
        DoMethod(newObj, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime, MUIV_Notify_Application, 5, MUIM_CallHook, &MA_PopNowHook, msn, RECEIVEF_USER, 0L);

        i++;
      }

      // we add the first MAXP3_MENU POP3 servers at most
      if(i == MAXP3_MENU)
        break;
    }

    // add the new dynamic menu to our
    // main menu
    DoMethod(G->MA->GUI.MN_FOLDER, MUIM_Family_AddTail, G->MA->GUI.MI_CSINGLE);
  }

  // handle the spam filter menu items
  if(C->SpamFilterEnabled == TRUE)
  {
    // for each entry check if it exists and if it is part of the menu
    // if not, create a new entry and add it to the current layout
    if(G->MA->GUI.MI_CHECKSPAM == NULL || isChildOfFamily(G->MA->GUI.MN_FOLDER, G->MA->GUI.MI_CHECKSPAM) == FALSE)
    {
      G->MA->GUI.MI_CHECKSPAM = Menuitem(tr(MSG_MA_CHECKSPAM), NULL, TRUE, FALSE, MMEN_CLASSIFY);

      if(G->MA->GUI.MI_CHECKSPAM != NULL)
        DoMethod(G->MA->GUI.MN_FOLDER, MUIM_Family_Insert, G->MA->GUI.MI_CHECKSPAM, G->MA->GUI.MI_FILTER);
    }

    if(G->MA->GUI.MI_DELSPAM == NULL || isChildOfFamily(G->MA->GUI.MN_FOLDER, G->MA->GUI.MI_DELSPAM) == FALSE)
    {
      G->MA->GUI.MI_DELSPAM = Menuitem(tr(MSG_MA_REMOVESPAM), NULL, TRUE, FALSE, MMEN_DELSPAM);

      if(G->MA->GUI.MI_DELSPAM != NULL)
        DoMethod(G->MA->GUI.MN_FOLDER, MUIM_Family_Insert, G->MA->GUI.MI_DELSPAM, G->MA->GUI.MI_DELDEL);
    }

    if(G->MA->GUI.MI_TOHAM == NULL || isChildOfFamily(G->MA->GUI.MI_STATUS, G->MA->GUI.MI_TOHAM) == FALSE)
    {
      G->MA->GUI.MI_TOHAM = Menuitem(tr(MSG_MA_TONOTSPAM), NULL, TRUE, FALSE, MMEN_TOHAM);

      if(G->MA->GUI.MI_TOHAM != NULL)
        DoMethod(G->MA->GUI.MI_STATUS, MUIM_Family_Insert, G->MA->GUI.MI_TOHAM, G->MA->GUI.MI_TOUNREAD);
    }

    if(G->MA->GUI.MI_TOSPAM == NULL || isChildOfFamily(G->MA->GUI.MI_STATUS, G->MA->GUI.MI_TOSPAM) == FALSE)
    {
      G->MA->GUI.MI_TOSPAM = Menuitem(tr(MSG_MA_TOSPAM), NULL, TRUE, FALSE, MMEN_TOSPAM);

      if(G->MA->GUI.MI_TOSPAM != NULL)
        DoMethod(G->MA->GUI.MI_STATUS, MUIM_Family_Insert, G->MA->GUI.MI_TOSPAM, G->MA->GUI.MI_TOUNREAD);
    }
  }
  else
  {
    // for each entry check if it exists and if it is part of the menu
    // if yes, then remove the entry and dispose it
    if(G->MA->GUI.MI_TOSPAM != NULL && isChildOfFamily(G->MA->GUI.MI_STATUS, G->MA->GUI.MI_TOSPAM) == TRUE)
    {
      DoMethod(G->MA->GUI.MI_STATUS, MUIM_Family_Remove, G->MA->GUI.MI_TOSPAM);
      MUI_DisposeObject(G->MA->GUI.MI_TOSPAM);
      G->MA->GUI.MI_TOSPAM = NULL;
    }
    if(G->MA->GUI.MI_TOHAM != NULL && isChildOfFamily(G->MA->GUI.MI_STATUS, G->MA->GUI.MI_TOHAM) == TRUE)
    {
      DoMethod(G->MA->GUI.MI_STATUS, MUIM_Family_Remove, G->MA->GUI.MI_TOHAM);
      MUI_DisposeObject(G->MA->GUI.MI_TOHAM);
      G->MA->GUI.MI_TOHAM = NULL;
    }
    if(G->MA->GUI.MI_DELSPAM != NULL && isChildOfFamily(G->MA->GUI.MN_FOLDER, G->MA->GUI.MI_DELSPAM) == TRUE)
    {
      DoMethod(G->MA->GUI.MN_FOLDER, MUIM_Family_Remove, G->MA->GUI.MI_DELSPAM);
      MUI_DisposeObject(G->MA->GUI.MI_DELSPAM);
      G->MA->GUI.MI_DELSPAM = NULL;
    }
    if(G->MA->GUI.MI_CHECKSPAM != NULL && isChildOfFamily(G->MA->GUI.MN_FOLDER, G->MA->GUI.MI_CHECKSPAM) == TRUE)
    {
      DoMethod(G->MA->GUI.MN_FOLDER, MUIM_Family_Remove, G->MA->GUI.MI_CHECKSPAM);
      MUI_DisposeObject(G->MA->GUI.MI_CHECKSPAM);
      G->MA->GUI.MI_CHECKSPAM = NULL;
    }
  }

  if(C->QuickSearchBarPos != QSB_POS_OFF || C->EmbeddedReadPane == TRUE)
  {
    if(G->MA->GUI.MN_EDIT == NULL || isChildOfFamily(G->MA->GUI.MS_MAIN, G->MA->GUI.MN_EDIT) == FALSE)
    {
      G->MA->GUI.MN_EDIT = MenuObject,
        MUIA_Menu_Title, tr(MSG_MA_EDIT),
        MUIA_Menu_CopyStrings, FALSE,
        MenuChild, Menuitem(tr(MSG_MA_EDIT_UNDO), "Z", TRUE, FALSE, MMEN_EDIT_UNDO),
        MenuChild, Menuitem(tr(MSG_MA_EDIT_REDO), NULL, TRUE, FALSE, MMEN_EDIT_REDO),
        MenuChild, MenuBarLabel,
        MenuChild, Menuitem(tr(MSG_MA_EDIT_CUT), "X", TRUE, FALSE, MMEN_EDIT_CUT),
        MenuChild, Menuitem(tr(MSG_MA_EDIT_COPY), "C", TRUE, FALSE, MMEN_EDIT_COPY),
        MenuChild, Menuitem(tr(MSG_MA_EDIT_PASTE), "V", TRUE, FALSE, MMEN_EDIT_PASTE),
        MenuChild, Menuitem(tr(MSG_MA_EDIT_DELETE), NULL, TRUE, FALSE, MMEN_EDIT_DELETE),
        MenuChild, MenuBarLabel,
        MenuChild, Menuitem(tr(MSG_MA_EDIT_SALL), "A", TRUE, FALSE, MMEN_EDIT_SALL),
        MenuChild, Menuitem(tr(MSG_MA_EDIT_SNONE), NULL, TRUE, FALSE, MMEN_EDIT_SNONE),
      End;

      if(G->MA->GUI.MN_EDIT != NULL)
      {
        DoMethod(G->MA->GUI.MS_MAIN, MUIM_Family_Insert, G->MA->GUI.MN_EDIT, G->MA->GUI.MN_PROJECT);
      }
    }
  }
  else
  {
    if(G->MA->GUI.MN_EDIT != NULL && isChildOfFamily(G->MA->GUI.MS_MAIN, G->MA->GUI.MN_EDIT) == TRUE)
    {
      DoMethod(G->MA->GUI.MS_MAIN, MUIM_Family_Remove, G->MA->GUI.MN_EDIT);
      MUI_DisposeObject(G->MA->GUI.MN_EDIT);
      G->MA->GUI.MN_EDIT = NULL;
    }
  }

  LEAVE();
}

///
/// MA_New
//  Creates main window
struct MA_ClassData *MA_New(void)
{
  struct MA_ClassData *data;

  ENTER();

  if((data = calloc(1, sizeof(struct MA_ClassData))) != NULL)
  {
    char *username = NULL;
    struct User *user;
    struct UserIdentityNode *uin;

    // get the first user identity to grab the realname from it
    if((uin = GetUserIdentity(&C->userIdentityList, 0, TRUE)) != NULL)
      username = uin->realname;

    // get the RealName and/or username of the current user
    if(username == NULL && (user = US_GetCurrentUser()) != NULL)
      username = user->Name;

    // prepare the generic window title of the main window
    snprintf(data->WinTitle, sizeof(data->WinTitle), tr(MSG_MA_WinTitle), yamver, username != NULL ? username : "");

    //
    // now we create the Menustrip object with all the menu items
    // and corresponding shortcuts
    //
    // The follwong shortcut list should help to identify the hard-coded
    // shortcuts:
    //
    //  A   reserved for 'Select All' operation (MMEN_EDIT_SALL)
    //  B   Addressbook (MMEN_ABOOK)
    //  C   reserved for 'Copy' operation (MMEN_EDIT_COPY)
    //  D   Read mail (MMEN_READ)
    //  E   Edit mail (MMEN_EDIT)
    //  F   Find/Search (MMEN_SEARCH)
    //  G   Get mail (MMEN_GETMAIL)
    //  H   Hide application (MMEN_HIDE)
    //  I   Filter mail (MMEN_FILTER)
    //  J   Save address (MMEN_SAVEADDR)
    //  K   Remove deleted mail (MMEN_DELDEL)
    //  L   Exchange mail (MMEN_EXMAIL)
    //  M   Move mail (MMEN_MOVE)
    //  N   New mail (MMEN_NEW)
    //  O   Delete attachments (MMEN_DELETEATT)
    //  P   Print (MMEN_PRINT)
    //  Q   Quit application (MMEN_QUIT)
    //  R   Reply mail (MMEN_REPLY)
    //  S   Send mail (MMEN_SEND)
    //  T   Save attachment (MMEN_DETACH)
    //  U   Save mail (MMEN_SAVE)
    //  V   reserved for 'Paste' operation (MMEN_EDIT_PASTE)
    //  W   Forward mail (MMEN_FORWARD_ATTACH or MMEN_FORWARD_INLINE)
    //  X   reserved for 'Cut' operation (MMEN_EDIT_CUT)
    //  Y   Copy mail (MMEN_COPY)
    //  Z   reserved for 'Undo' operation (MMEN_EDIT_UNDO)
    //  1   reserved for Arexx-Script 1
    //  2   reserved for Arexx-Script 2
    //  3   reserved for Arexx-Script 3
    //  4   reserved for Arexx-Script 4
    //  5   reserved for Arexx-Script 5
    //  6   reserved for Arexx-Script 6
    //  7   reserved for Arexx-Script 7
    //  8   reserved for Arexx-Script 8
    //  9   reserved for Arexx-Script 9
    //  0   reserved for Arexx-Script 10
    // Del  Remove selected mail (MMEN_DELETE)
    //  +   Select all (MMEN_SELALL)
    //  -   Select no mail (MMEN_SELNONE)
    //  =   Toggle selection (MMEN_SELTOGG)
    //  ,   Set status to marked (MMEN_TOMARKED)
    //  .   Set status to unmarked (MMEN_TOUNMARKED)
    //  [   Set status to unread (MMEN_TOUNREAD)
    //  ]   Set status to read (MMEN_TOREAD)
    //  #   Set all mails to read (MMEN_ALLTOREAD)
    //  *   Configuration (MMEN_CONFIG)
    //  _   Execute script (MMEN_SCRIPT)
    //  ?   Open about window (MMEN_ABOUT)
    //
    // InputEvent shortcuts:
    //
    // -capslock del                : delete mail (MMEN_DELETE)
    // -capslock shift del          : delete mail at once
    // -repeat -capslock alt left   : Display previous mail in message thread (MMEN_PREVTH)
    // -repeat -capslock alt right  : Display next mail in message thread (MMEN_NEXTTH)
    //

    data->GUI.MS_MAIN = MenustripObject,
      MenuChild, data->GUI.MN_PROJECT = MenuObject,
        MUIA_Menu_Title, tr(MSG_MA_Project),
        MUIA_Menu_CopyStrings, FALSE,
        MenuChild, Menuitem(tr(MSG_PROJECT_MABOUT), "?", TRUE, FALSE, MMEN_ABOUT),
        MenuChild, Menuitem(tr(MSG_MA_AboutMUI), NULL, TRUE, FALSE, MMEN_ABOUTMUI),
        MenuChild, data->GUI.MI_UPDATECHECK = Menuitem(tr(MSG_MA_UPDATECHECK), NULL, TRUE, FALSE, MMEN_VERSION),
        MenuChild, data->GUI.MI_ERRORS = Menuitem(tr(MSG_MA_LastErrors), NULL, G->ER_NumErr > 0, FALSE, MMEN_ERRORS),
        MenuChild, data->GUI.MI_TRANSFERS = Menuitem(tr(MSG_SHOW_TRANSFER_WINDOW), NULL, FALSE, FALSE, MMEN_TRANSFERS),
        MenuChild, MenuBarLabel,
        MenuChild, Menuitem(tr(MSG_MA_Restart), NULL, TRUE, FALSE, MMEN_LOGIN),
        MenuChild, Menuitem(tr(MSG_MA_HIDE), "H", TRUE, FALSE, MMEN_HIDE),
        MenuChild, Menuitem(tr(MSG_MA_QUIT), "Q", TRUE, FALSE, MMEN_QUIT),
      End,
      MenuChild, data->GUI.MI_NAVIG = MenuObject,
        MUIA_Menu_Title, tr(MSG_MA_NAVIGATION),
        MUIA_Menu_CopyStrings, FALSE,
        MenuChild, data->GUI.MI_NEXTTHREAD = Menuitem(tr(MSG_MA_MNEXTTH), "alt right", TRUE, TRUE, MMEN_NEXTTH),
        MenuChild, data->GUI.MI_PREVTHREAD = Menuitem(tr(MSG_MA_MPREVTH), "alt left", TRUE, TRUE, MMEN_PREVTH),
      End,
      MenuChild, data->GUI.MN_FOLDER = MenuObject,
        MUIA_Menu_Title, tr(MSG_Folder),
        MUIA_Menu_CopyStrings, FALSE,
        MenuChild, Menuitem(tr(MSG_FOLDER_NEWFOLDER), NULL, TRUE, FALSE, MMEN_NEWF),
        MenuChild, Menuitem(tr(MSG_FOLDER_NEWFOLDERGROUP), NULL, TRUE, FALSE, MMEN_NEWFG),
        MenuChild, Menuitem(tr(MSG_FOLDER_EDIT), NULL, TRUE, FALSE, MMEN_EDITF),
        MenuChild, Menuitem(tr(MSG_FOLDER_DELETE), NULL, TRUE, FALSE, MMEN_DELETEF),
        MenuChild, MenuBarLabel,
        MenuChild, Menuitem(tr(MSG_MA_MSEARCH), "F", TRUE, FALSE, MMEN_SEARCH),
        MenuChild, data->GUI.MI_FILTER = Menuitem(tr(MSG_MA_MFILTER), "I", TRUE, FALSE, MMEN_FILTER),
        MenuChild, MenuBarLabel,
        MenuChild, data->GUI.MI_DELDEL = Menuitem(tr(MSG_MA_REMOVEDELETED), "K", TRUE, FALSE, MMEN_DELDEL),
        MenuChild, data->GUI.MI_UPDINDEX = Menuitem(tr(MSG_MA_UPDATEINDEX), NULL, TRUE, FALSE, MMEN_INDEX),
        MenuChild, Menuitem(tr(MSG_MA_FLUSH_ALL_INDICES), NULL, TRUE, FALSE, MMEN_FLUSH),
        MenuChild, MenuBarLabel,
        MenuChild, data->GUI.MI_IMPORT = Menuitem(tr(MSG_FOLDER_IMPORT), NULL, TRUE, FALSE, MMEN_IMPORT),
        MenuChild, data->GUI.MI_EXPORT = Menuitem(tr(MSG_FOLDER_EXPORT), NULL, TRUE, FALSE, MMEN_EXPORT),
        MenuChild, MenuBarLabel,
        MenuChild, data->GUI.MI_SENDALL = Menuitem(tr(MSG_MA_SEND_MAIL), "S", TRUE, FALSE, MMEN_SENDMAIL),
        MenuChild, data->GUI.MI_EXCHANGE = Menuitem(tr(MSG_MA_SEND_AND_GET_MAIL), "L", TRUE, FALSE, MMEN_EXMAIL),
        MenuChild, data->GUI.MI_GETMAIL = Menuitem(tr(MSG_MA_GET_MAIL), "G", TRUE, FALSE, MMEN_GETMAIL),
      End,
      MenuChild, MenuObject,
        MUIA_Menu_Title, tr(MSG_Message),
        MUIA_Menu_CopyStrings, FALSE,
        MenuChild, data->GUI.MI_READ = Menuitem(tr(MSG_MA_MREAD), "D", TRUE, FALSE, MMEN_READ),
        MenuChild, data->GUI.MI_EDIT = Menuitem(tr(MSG_MA_MEDITASNEW), "E", TRUE, FALSE, MMEN_EDIT),
        MenuChild, data->GUI.MI_MOVE = Menuitem(tr(MSG_MA_MMOVE), "M", TRUE, FALSE, MMEN_MOVE),
        MenuChild, data->GUI.MI_COPY = Menuitem(tr(MSG_MA_MCOPY), "Y", TRUE, FALSE, MMEN_COPY),
        MenuChild, data->GUI.MI_ARCHIVE = Menuitem(tr(MSG_MA_MARCHIVE), NULL, TRUE, FALSE, MMEN_ARCHIVE),
        MenuChild, data->GUI.MI_DELETE = Menuitem(tr(MSG_MA_MDelete), "Del", TRUE, TRUE, MMEN_DELETE),
        MenuChild, MenuBarLabel,
        MenuChild, data->GUI.MI_PRINT = Menuitem(tr(MSG_MA_MPRINT), "P", TRUE, FALSE, MMEN_PRINT),
        MenuChild, data->GUI.MI_SAVE = Menuitem(tr(MSG_MA_MSAVE), "U", TRUE, FALSE, MMEN_SAVE),
        MenuChild, data->GUI.MI_ATTACH = MenuitemObject,
          MUIA_Menuitem_Title, tr(MSG_Attachments),
          MUIA_Menuitem_CopyStrings, FALSE,
          MenuChild, data->GUI.MI_SAVEATT = Menuitem(tr(MSG_MA_MSAVEATT), "T", TRUE, FALSE, MMEN_DETACH),
          MenuChild, data->GUI.MI_REMATT = Menuitem(tr(MSG_MA_MDELETEATT), "O", TRUE, FALSE, MMEN_DELETEATT),
        End,
        MenuChild, data->GUI.MI_EXPMSG = Menuitem(tr(MSG_MESSAGE_EXPORT), NULL, TRUE, FALSE, MMEN_EXPMSG),
        MenuChild, MenuBarLabel,
        MenuChild, data->GUI.MI_NEW = Menuitem(tr(MSG_MA_MNEW), "N", TRUE, FALSE, MMEN_NEW),
        MenuChild, data->GUI.MI_REPLY = Menuitem(tr(MSG_MA_MREPLY), "R", TRUE, FALSE, MMEN_REPLY),
        MenuChild, data->GUI.MI_FORWARD = MenuitemObject,
          MUIA_Menuitem_Title, tr(MSG_MA_MFORWARD),
          MUIA_Menuitem_CopyStrings, FALSE,
          MenuChild, data->GUI.MI_FORWARD_ATTACH = Menuitem(tr(MSG_MA_MFORWARD_ATTACH), NULL, TRUE, FALSE, MMEN_FORWARD_ATTACH),
          MenuChild, data->GUI.MI_FORWARD_INLINE = Menuitem(tr(MSG_MA_MFORWARD_INLINE), NULL, TRUE, FALSE, MMEN_FORWARD_INLINE),
        End,
        MenuChild, data->GUI.MI_REDIRECT = Menuitem(tr(MSG_MA_MREDIRECT), NULL, TRUE, FALSE, MMEN_REDIRECT),
        MenuChild, MenuBarLabel,
        MenuChild, data->GUI.MI_GETADDRESS = Menuitem(tr(MSG_MA_MSAVEADDRESS), "J", TRUE, FALSE, MMEN_SAVEADDR),
        MenuChild, data->GUI.MI_SELECT = MenuitemObject,
          MUIA_Menuitem_Title, tr(MSG_MA_Select),
          MUIA_Menuitem_CopyStrings, FALSE,
          MenuChild, Menuitem(tr(MSG_MA_SELECTALL), "+", TRUE, FALSE, MMEN_SELALL),
          MenuChild, Menuitem(tr(MSG_MA_SELECTNONE), "-", TRUE, FALSE, MMEN_SELNONE),
          MenuChild, Menuitem(tr(MSG_MA_SELECTTOGGLE), "=", TRUE, FALSE, MMEN_SELTOGG),
        End,
        MenuChild, data->GUI.MI_STATUS = MenuitemObject,
          MUIA_Menuitem_Title, tr(MSG_MA_SetStatus),
          MUIA_Menuitem_CopyStrings, FALSE,
          MenuChild, data->GUI.MI_TOMARKED = Menuitem(tr(MSG_MA_TOMARKED), ",", TRUE, FALSE, MMEN_TOMARKED),
          MenuChild, data->GUI.MI_TOUNMARKED = Menuitem(tr(MSG_MA_TOUNMARKED), ".", TRUE, FALSE, MMEN_TOUNMARKED),
          MenuChild, data->GUI.MI_TOREAD = Menuitem(tr(MSG_MA_TOREAD), "]", TRUE, FALSE, MMEN_TOREAD),
          MenuChild, data->GUI.MI_TOUNREAD = Menuitem(tr(MSG_MA_TOUNREAD), "[", TRUE, FALSE, MMEN_TOUNREAD),
          MenuChild, MenuBarLabel,
          MenuChild, data->GUI.MI_ALLTOREAD = Menuitem(tr(MSG_MA_ALLTOREAD), "#", TRUE, FALSE, MMEN_ALLTOREAD),
        End,
        MenuChild, data->GUI.MI_CHSUBJ = Menuitem(tr(MSG_MA_ChangeSubj), NULL, TRUE, FALSE, MMEN_CHSUBJ),
        MenuChild, MenuBarLabel,
        MenuChild, data->GUI.MI_SEND = Menuitem(tr(MSG_MA_MSend), NULL, TRUE, FALSE, MMEN_SEND),
      End,
      MenuChild, data->GUI.MN_SETTINGS = MenuObject,
        MUIA_Menu_Title, tr(MSG_MA_Settings),
        MUIA_Menu_CopyStrings, FALSE,
        MenuChild, Menuitem(tr(MSG_MA_MADDRESSBOOK), "B", TRUE, FALSE, MMEN_ABOOK),
        MenuChild, Menuitem(tr(MSG_MA_MCONFIG), "*", TRUE, FALSE, MMEN_CONFIG),
        MenuChild, Menuitem(tr(MSG_SETTINGS_USERS), NULL, TRUE, FALSE, MMEN_USER),
        MenuChild, Menuitem(tr(MSG_SETTINGS_MUI), NULL, TRUE, FALSE, MMEN_MUI),
      End,
      MenuChild, MenuObject,
        MUIA_Menu_Title, tr(MSG_MA_HELP),
        MUIA_Menu_CopyStrings, FALSE,
        MenuChild, Menuitem(tr(MSG_MA_HELP_CONTENTS), "Help", TRUE, FALSE, MMEN_HELP_CONTENTS),
        MenuChild, Menuitem(tr(MSG_MA_HELP_AREXX), NULL, TRUE, FALSE, MMEN_HELP_AREXX),
      End,
    End;

    data->GUI.WI = MainWindowObject,
      MUIA_Window_Title, data->WinTitle,
      MUIA_Window_ScreenTitle, data->WinTitle,
      MUIA_HelpNode, "Windows/Mainwindow",
      MUIA_Window_ID, MAKE_ID('M','A','I','N'),
      MUIA_Window_Menustrip, data->GUI.MS_MAIN,
      WindowContents, data->GUI.GR_MAIN = VGroup,
        Child, data->GUI.GR_TOP = hasHideToolBarFlag(C->HideGUIElements) ?
        VSpace(1) :
        (HGroupV,
          Child, data->GUI.TO_TOOLBAR = MainWindowToolbarObject,
            MUIA_HelpNode, "Windows/Mainwindow#Toolbar",
          End,
        End),
        Child, data->GUI.GR_HIDDEN = HGroup,
          MUIA_ShowMe, FALSE,
          Child, data->GUI.ST_LAYOUT = StringObject,
            MUIA_ObjectID, MAKE_ID('S','T','L','A'),
            MUIA_String_MaxLen, SIZE_LARGE,
          End,
        End,
        Child, data->GUI.IB_INFOBAR = InfoBarObject,
          MUIA_ShowMe, (C->InfoBarPos != IB_POS_OFF),
        End,
        Child, data->GUI.GR_BOTTOM = HGroup,
          GroupSpacing(0),
          Child, data->GUI.LV_FOLDERS = NListviewObject,
            MUIA_HelpNode,    "Windows/Mainwindow#Folderlist",
            MUIA_CycleChain,  TRUE,
            MUIA_Listview_DragType, MUIV_Listview_DragType_Immediate,
            MUIA_NListview_NList, data->GUI.LT_FOLDERS = MainFolderListtreeObject,
            End,
          End,
          Child, NBalanceObject,
            MUIA_ObjectID, MAKE_ID('B','0','0','1'),
            MUIA_Balance_Quiet, TRUE,
          End,
          Child, data->GUI.GR_MAILVIEW = VGroup,
            GroupSpacing(0),
            Child, data->GUI.GR_MAILLIST = VGroup,
              GroupSpacing(1),
              Child, data->GUI.GR_QUICKSEARCHBAR = QuickSearchBarObject,
                MUIA_ShowMe, (C->QuickSearchBarPos != QSB_POS_OFF),
              End,
              Child, data->GUI.PG_MAILLIST = MainMailListGroupObject,
                MUIA_HelpNode, "Windows/Mainwindow#Messagelist",
              End,
            End,
          End,
        End,
      End,
    End;

    // check if we were able to generate the main
    // window object
    if(data->GUI.WI != NULL)
    {
      ULONG i;

      DoMethod(G->App, OM_ADDMEMBER, data->GUI.WI);

      // set the maillist group as the active object of that window
      set(data->GUI.WI, MUIA_Window_ActiveObject, xget(data->GUI.PG_MAILLIST, MUIA_MainMailListGroup_ActiveListviewObject));

      // make sure to set the KeyLeft/Right Focus for the mainmaillist and
      // folder listtree objects
      set(data->GUI.PG_MAILLIST, MUIA_NList_KeyLeftFocus, data->GUI.LV_FOLDERS);
      set(data->GUI.LV_FOLDERS, MUIA_NList_KeyRightFocus, data->GUI.PG_MAILLIST);

      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_ABOUT,          MUIV_Notify_Application, 1, MUIM_YAMApplication_OpenAboutWindow);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_VERSION,        MUIV_Notify_Application, 2, MUIM_YAMApplication_UpdateCheck,      FALSE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_ERRORS,         MUIV_Notify_Self,        1, MUIM_MainWindow_ShowErrors);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_TRANSFERS,      MUIV_Notify_Application, 1, MUIM_YAMApplication_ShowTransferWindow);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_LOGIN,          MUIV_Notify_Application, 2, MUIM_Application_ReturnID, ID_RESTART);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_HIDE,           MUIV_Notify_Application, 3, MUIM_Set,                  MUIA_Application_Iconified, TRUE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_QUIT,           MUIV_Notify_Application, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EDIT_UNDO,      MUIV_Notify_Self,        2, MUIM_MainWindow_DoEditAction, EA_UNDO);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EDIT_REDO,      MUIV_Notify_Self,        2, MUIM_MainWindow_DoEditAction, EA_REDO);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EDIT_CUT,       MUIV_Notify_Self,        2, MUIM_MainWindow_DoEditAction, EA_CUT);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EDIT_COPY,      MUIV_Notify_Self,        2, MUIM_MainWindow_DoEditAction, EA_COPY);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EDIT_PASTE,     MUIV_Notify_Self,        2, MUIM_MainWindow_DoEditAction, EA_PASTE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EDIT_DELETE,    MUIV_Notify_Self,        2, MUIM_MainWindow_DoEditAction, EA_DELETE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EDIT_SALL,      MUIV_Notify_Self,        2, MUIM_MainWindow_DoEditAction, EA_SELECTALL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EDIT_SNONE,     MUIV_Notify_Self,        2, MUIM_MainWindow_DoEditAction, EA_SELECTNONE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_NEWF,           data->GUI.LT_FOLDERS,    1, MUIM_MainFolderListtree_NewFolder);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_NEWFG,          data->GUI.LT_FOLDERS,    2, MUIM_MainFolderListtree_NewFolderGroup, NULL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EDITF,          data->GUI.LT_FOLDERS,    2, MUIM_MainFolderListtree_EditFolder, FALSE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_DELETEF,        data->GUI.LT_FOLDERS,    1, MUIM_MainFolderListtree_DeleteFolder);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_SELALL,         data->GUI.PG_MAILLIST,   4, MUIM_NList_Select,         MUIV_NList_Select_All, MUIV_NList_Select_On, NULL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_SELNONE,        data->GUI.PG_MAILLIST,   4, MUIM_NList_Select,         MUIV_NList_Select_All, MUIV_NList_Select_Off, NULL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_SELTOGG,        data->GUI.PG_MAILLIST,   4, MUIM_NList_Select,         MUIV_NList_Select_All, MUIV_NList_Select_Toggle, NULL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_SEARCH,         MUIV_Notify_Application, 2, MUIM_YAMApplication_OpenSearchMailWindow, NULL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_FILTER,         MUIV_Notify_Self,        4, MUIM_MainWindow_ApplyFilters, APPLY_USER, 0, NULL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_CLASSIFY,       MUIV_Notify_Self,        4, MUIM_MainWindow_ApplyFilters, APPLY_SPAM, 0, NULL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_DELDEL,         MUIV_Notify_Application, 2, MUIM_YAMApplication_EmptyTrashFolder, FALSE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_DELSPAM,        MUIV_Notify_Application, 2, MUIM_YAMApplication_DeleteSpamMails, FALSE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_INDEX,          MUIV_Notify_Application, 1, MUIM_YAMApplication_RebuildFolderIndex);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_FLUSH,          MUIV_Notify_Application, 2, MUIM_YAMApplication_FlushFolderIndexes, FALSE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_ABOOK,          MUIV_Notify_Application, 4, MUIM_YAMApplication_OpenAddressBookWindow, ABM_EDIT, -1, NULL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EXPORT,         MUIV_Notify_Application, 3, MUIM_CallHook,             &MA_ExportMessagesHook, TRUE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_IMPORT,         MUIV_Notify_Application, 2, MUIM_CallHook,             &MA_ImportMessagesHook);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_GETMAIL,        MUIV_Notify_Application, 5, MUIM_CallHook,             &MA_PopNowHook, NULL, RECEIVEF_USER, 0);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_SENDMAIL,       MUIV_Notify_Application, 3, MUIM_CallHook,             &MA_SendHook, SENDMAIL_ALL_USER);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EXMAIL,         MUIV_Notify_Application, 5, MUIM_CallHook,             &MA_PopNowHook, NULL, RECEIVEF_USER, IEQUALIFIER_LSHIFT);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_READ,           MUIV_Notify_Application, 2, MUIM_CallHook,             &MA_ReadMessageHook);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EDIT,           MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_NewMessageHook, NMM_EDIT, 0);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_MOVE,           MUIV_Notify_Application, 2, MUIM_CallHook,             &MA_MoveMessageHook);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_COPY,           MUIV_Notify_Application, 2, MUIM_CallHook,             &MA_CopyMessageHook);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_ARCHIVE,        MUIV_Notify_Application, 2, MUIM_CallHook,             &MA_ArchiveMessageHook);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_DELETE,         MUIV_Notify_Application, 3, MUIM_CallHook,             &MA_DeleteMessageHook, 0);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_PRINT,          MUIV_Notify_Application, 3, MUIM_CallHook,             &MA_SavePrintHook, TRUE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_SAVE,           MUIV_Notify_Application, 3, MUIM_CallHook,             &MA_SavePrintHook, FALSE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_DETACH,         MUIV_Notify_Application, 2, MUIM_CallHook,             &MA_SaveAttachHook);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_DELETEATT,      MUIV_Notify_Application, 2, MUIM_CallHook,             &MA_RemoveAttachHook);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_EXPMSG,         MUIV_Notify_Application, 3, MUIM_CallHook,             &MA_ExportMessagesHook, FALSE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_NEW,            MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_NewMessageHook, NMM_NEW, 0);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_REPLY,          MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_NewMessageHook, NMM_REPLY, 0);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_FORWARD_ATTACH, MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_NewMessageHook, NMM_FORWARD_ATTACH, 0);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_FORWARD_INLINE, MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_NewMessageHook, NMM_FORWARD_INLINE, 0);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_REDIRECT,       MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_NewMessageHook, NMM_REDIRECT, 0);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_SAVEADDR,       MUIV_Notify_Application, 2, MUIM_CallHook,             &MA_GetAddressHook);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_CHSUBJ,         MUIV_Notify_Application, 2, MUIM_CallHook,             &MA_ChangeSubjectHook);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_SEND,           MUIV_Notify_Application, 3, MUIM_CallHook,             &MA_SendHook, SENDMAIL_ACTIVE_USER);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_TOUNREAD,       MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_SetStatusToHook, SFLAG_NONE,              SFLAG_NEW|SFLAG_READ);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_TOREAD,         MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_SetStatusToHook, SFLAG_READ,              SFLAG_NEW);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_TOMARKED,       MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_SetStatusToHook, SFLAG_MARKED, SFLAG_NONE);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_TOUNMARKED,     MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_SetStatusToHook, SFLAG_NONE,   SFLAG_MARKED);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_ALLTOREAD,      MUIV_Notify_Application, 4, MUIM_CallHook,             &MA_SetAllStatusToHook, SFLAG_READ, SFLAG_NEW);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_TOSPAM,         MUIV_Notify_Application, 3, MUIM_CallHook,             &MA_ClassifyMessageHook, BC_SPAM);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_TOHAM,          MUIV_Notify_Application, 3, MUIM_CallHook,             &MA_ClassifyMessageHook, BC_HAM);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_CONFIG,         MUIV_Notify_Application, 1, MUIM_YAMApplication_OpenConfigWindow);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_USER,           MUIV_Notify_Application, 2, MUIM_CallHook,             &US_OpenHook);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_MUI,            MUIV_Notify_Application, 3, MUIM_Application_OpenConfigWindow, MUIF_NONE, NULL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_ABOUTMUI,       MUIV_Notify_Application, 2, MUIM_Application_AboutMUI, data->GUI.WI);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_SCRIPT,         MUIV_Notify_Application, 3, MUIM_YAMApplication_StartMacro, MACRO_PROMPT_USER, NULL);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_PREVTH,         MUIV_Notify_Application, 3, MUIM_CallHook,             &FollowThreadHook, -1);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_NEXTTH,         MUIV_Notify_Application, 3, MUIM_CallHook,             &FollowThreadHook, +1);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_HELP_CONTENTS,  MUIV_Notify_Application, 5, MUIM_Application_ShowHelp, data->GUI.WI, NULL, NULL, 0);
      DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_HELP_AREXX,     MUIV_Notify_Application, 5, MUIM_Application_ShowHelp, data->GUI.WI, NULL, "ARexxAPI", 0);

      for(i=0; i < MAXRX_MENU; i++)
        DoMethod(data->GUI.WI, MUIM_Notify, MUIA_Window_MenuAction, MMEN_MACRO+i, MUIV_Notify_Application, 3, MUIM_YAMApplication_StartMacro, i, NULL);

      DoMethod(data->GUI.WI,            MUIM_Notify, MUIA_Window_CloseRequest,  TRUE,                   MUIV_Notify_Application,  2, MUIM_Application_ReturnID, ID_CLOSEALL);

      // input events
      DoMethod(data->GUI.WI,            MUIM_Notify, MUIA_Window_InputEvent,    "-capslock del",        MUIV_Notify_Application,  3, MUIM_CallHook,             &MA_DelKeyHook, FALSE);
      DoMethod(data->GUI.WI,            MUIM_Notify, MUIA_Window_InputEvent,    "-capslock shift del",  MUIV_Notify_Application,  3, MUIM_CallHook,             &MA_DelKeyHook, TRUE);
      DoMethod(data->GUI.WI,            MUIM_Notify, MUIA_Window_InputEvent,    "-repeat -capslock alt left",   MUIV_Notify_Application,  3, MUIM_CallHook,             &FollowThreadHook, -1);
      DoMethod(data->GUI.WI,            MUIM_Notify, MUIA_Window_InputEvent,    "-repeat -capslock alt right",  MUIV_Notify_Application,  3, MUIM_CallHook,             &FollowThreadHook, +1);

      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_READ,     MUIA_Pressed, FALSE, MUIV_Notify_Application, 2, MUIM_CallHook, &MA_ReadMessageHook);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_EDIT,     MUIA_Pressed, FALSE, MUIV_Notify_Application, 4, MUIM_CallHook, &MA_NewMessageHook, NMM_EDIT, MUIV_TheBar_Qualifier);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_MOVE,     MUIA_Pressed, FALSE, MUIV_Notify_Application, 2, MUIM_CallHook, &MA_MoveMessageHook);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_DELETE,   MUIA_Pressed, FALSE, MUIV_Notify_Application, 3, MUIM_CallHook, &MA_DeleteMessageHook, MUIV_TheBar_Qualifier);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_GETADDR,  MUIA_Pressed, FALSE, MUIV_Notify_Application, 2, MUIM_CallHook, &MA_GetAddressHook);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_NEWMAIL,  MUIA_Pressed, FALSE, MUIV_Notify_Application, 4, MUIM_CallHook, &MA_NewMessageHook, NMM_NEW, MUIV_TheBar_Qualifier);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_REPLY,    MUIA_Pressed, FALSE, MUIV_Notify_Application, 4, MUIM_CallHook, &MA_NewMessageHook, NMM_REPLY, MUIV_TheBar_Qualifier);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_FORWARD,  MUIA_Pressed, FALSE, MUIV_Notify_Application, 4, MUIM_CallHook, &MA_NewMessageHook, NMM_FORWARD, MUIV_TheBar_Qualifier);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_GETMAIL,  MUIA_Pressed, FALSE, MUIV_Notify_Application, 5, MUIM_CallHook, &MA_PopNowHook, NULL, RECEIVEF_USER, MUIV_TheBar_Qualifier);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_SENDALL,  MUIA_Pressed, FALSE, MUIV_Notify_Application, 3, MUIM_CallHook, &MA_SendHook, SENDMAIL_ALL_USER);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_SPAM,     MUIA_Pressed, FALSE, MUIV_Notify_Application, 3, MUIM_CallHook, &MA_ClassifyMessageHook, BC_SPAM);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_HAM,      MUIA_Pressed, FALSE, MUIV_Notify_Application, 3, MUIM_CallHook, &MA_ClassifyMessageHook, BC_HAM);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_FILTER,   MUIA_Pressed, FALSE, data->GUI.WI,            4, MUIM_MainWindow_ApplyFilters, APPLY_USER, MUIV_TheBar_Qualifier, NULL);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_FIND,     MUIA_Pressed, FALSE, MUIV_Notify_Application, 2, MUIM_YAMApplication_OpenSearchMailWindow, NULL);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_ADDRBOOK, MUIA_Pressed, FALSE, MUIV_Notify_Application, 4, MUIM_YAMApplication_OpenAddressBookWindow, ABM_EDIT, -1, NULL);
      DoMethod(data->GUI.TO_TOOLBAR, MUIM_TheBar_Notify, TB_MAIN_CONFIG,   MUIA_Pressed, FALSE, MUIV_Notify_Application, 1, MUIM_YAMApplication_OpenConfigWindow);
    }
  }
  else
  {
    E(DBF_GUI, "Couldn't create main window object!");
    free(data);
    data = NULL;
  }

  RETURN(data);
  return data;
}

///
