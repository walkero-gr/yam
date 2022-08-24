#ifndef MAIN_YAM_H
#define MAIN_YAM_H

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

#include <xpk/xpk.h>

#if defined(__amigaos4__)
#include <dos/obsolete.h>
#endif
#if defined(__AROS__)
#include <sys/types.h>
#endif

#include <netinet/in.h>

#include <proto/amissl.h>    // SSL*

#include "YAM_stringsizes.h"
#include "YAM_userlist.h"    // struct Users
#include "YAM_utilities.h"   // ASL_MAX

#include "AddressBook.h"     // struct AddressBook
#include "BayesFilter.h"     // struct TokenAnalyzer
#include "Themes.h"          // struct Theme
#include "Timer.h"           // struct Timers

// forward declarations
struct DiskObject;
struct AppIcon;
struct MsgPort;
struct RexxHost;
struct FileReqCache;
struct Locale;
struct Catalog;
struct MA_ClassData;
struct ER_ClassData;
struct FI_ClassData;
struct DI_ClassData;
struct ReadMailData;
struct codeset;
struct codesetList;
struct HashTable;
struct NotifyRequest;
struct Process;
struct TZoneInfo;

/**************************************************************************/

enum GlobalDispatcherJob
{
  ID_CLOSEALL=1000,
  ID_RESTART,
  ID_RESTART_FORCE,
  ID_LOGIN
};

/*** Global Structure ***/
struct Global
{
  // pointers first
  APTR                     SharedMemPool; // MEMF_SHARED memory pool
  Object *                 App;
  Object *                 SoundDTObj;
  Object *                 SplashWinObject;
  Object *                 SearchMailWinObject;
  Object *                 UpdateNotifyWinObject;
  Object *                 ReadToolbarCacheObject;
  Object *                 WriteToolbarCacheObject;
  Object *                 AbookToolbarCacheObject;
  Object *                 ABookWinObject;
  Object *                 ConfigWinObject;
  char *                   ER_Message[MAXERR];
  struct DiskObject *      HideIcon;
  struct AppIcon *         AppIcon;
  struct MsgPort *         AppPort;
  struct MsgPort *         writeWinNotifyPort;
  struct RexxHost *        RexxHost;
  struct FileReqCache *    FileReqCache[ASL_MAX];
  struct Locale *          Locale;
  struct Catalog *         Catalog;
  struct MA_ClassData *    MA;
  struct ER_ClassData *    ER;
  struct DI_ClassData *    DI;
  struct US_ClassData *    US;
  struct ReadMailData *    ActiveRexxRMData;
  struct WriteMailData *   ActiveRexxWMData;
  struct codeset *         systemCodeset;        // the system codeset
  struct codeset *         localCodeset;         // the codeset YAM will use to display text
  struct codeset *         writeCodeset;         // the codeset YAM will use to write/encode emails
  struct codeset *         editorCodeset;        // the codeset YAM will use for external editors
  struct codesetList *     codesetsList;
  struct HashTable *       imageCacheHashTable;
  struct FolderList *      folders;
  struct MinList *         xpkPackerList;
  struct SignalSemaphore * globalSemaphore;      // a semaphore for certain variables in this structure, i.e. currentFolder
  struct SignalSemaphore * connectionSemaphore;  // a semaphore to lock all connections agains each other
  struct SignalSemaphore * hostResolveSemaphore; // a semaphore to lock all host resolve (gethostbyname) calls
  struct SignalSemaphore * configSemaphore;      // a semaphore to prevent concurrent changes to the configuration
  struct Part *            virtualMailpart[2];   // two virtual mail parts for the attachment requester window
  struct Folder *          currentFolder;        // the currently active folder
  APTR                     mailItemPool;         // item pool for struct Mail
  APTR                     mailNodeItemPool;     // item pool for struct MailNode
  struct Screen *          workbenchScreen;
  struct MailList *        mailsInTransfer;      // list of mail currently being sent
  struct Interrupt *       lowMemHandler;        // low memory handler to flush all indexes
  SSL_CTX *                sslCtx;               // global SSL protocol context (NULL == unusable SSL)

  #if defined(__amigaos4__)
  struct MsgPort *         AppLibPort;
  #endif

  LONG                     Weights[12];
  ULONG                    quickSearchViewOptions;

  int                      PGPVersion;
  int                      ER_NumErr;
  int                      currentAppIcon;
  int                      activeConnections;
  #if defined(__amigaos4__)
  int                      LastIconID;
  #endif
  time_t                   LastPGPUsage;
  int                      gmtOffset;           // the current offset to GMT in min (+/-)
  int                      sslDataIndex;        // index number of data stored

  #if defined(__amigaos4__)
  uint32                   applicationID;
  #endif

  BOOL                     SingleTask;
  BOOL                     PGP5;
  BOOL                     AppIconQuiet;
  BOOL                     PGPPassVolatile;
  BOOL                     NetLog;
  BOOL                     InStartupPhase;
  BOOL                     NoImageWarning;
  BOOL                     NoCatalogTranslation;
  BOOL                     DefIconsAvailable;
  BOOL                     Terminating;
  BOOL                     LowMemSituation;

  struct DateStamp         StartDate;
  struct TimeVal           nextDSTSwitch;        // date/time of next DST switch
  struct Users             Users;
  struct MinList           readMailDataList;
  struct MinList           writeMailDataList;
  struct MinList           zombieFileList;
  struct MinList           normalBusyList;       // list of active busy actions, normal usage
  struct MinList           arexxBusyList;        // list of active busy actions, ARexx usage
  struct MinList           tzoneContinentList;   // parsed stuff from zone.tab file
  struct Theme             theme;
  struct TokenAnalyzer     spamFilter;
  struct Timers            timerData;
  struct ABook             abook;

  // the data for our thread implementation
  struct MsgPort         * threadPort;
  struct MinList           idleThreads;
  struct MinList           workingThreads;
  ULONG                    numberOfThreads;
  ULONG                    threadCounter;
  struct Process         * mainThread;

  // the data for our methodstack implementation
  struct MsgPort         * methodStack;

  char                     ProgDir[SIZE_PATH];
  char                     ProgName[SIZE_FILE];
  char                     ThemesDir[SIZE_PATH];
  char                     PGPPassPhrase[SIZE_DEFAULT];
  char                     MA_MailDir[SIZE_PATH];
  char                     abookFilename[SIZE_PATHFILE];
  char                     CO_PrefsFile[SIZE_PATHFILE];
  char                     DI_Filename[SIZE_PATHFILE];
  char                     preselectionListLayout[SIZE_LARGE];
  char                     tzAbbr[SIZE_SMALL];  // Abbreviation string (e.g. CEST/CET) of timezone
};

extern struct Global *G;

struct xpkPackerNode
{
  struct MinNode node;
  struct XpkPackerInfo info;
};

void PopUp(void);
BOOL StayInProg(void);
void MiniMainLoop(void);
void MicroMainLoop(void);

#endif /* MAIN_YAM_H */

