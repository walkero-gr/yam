/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2001 by YAM Open Source Team

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

#include "YAM_global.h"
#include "YAM_locale.h"

#if defined(__PPC__)
  #define CPU " [PPC]"
#elif defined(_M68060) || defined(__M68060) || defined(__mc68060)
  #define CPU " [060]"
#elif defined(_M68040) || defined(__M68040) || defined(__mc68040)
  #define CPU " [040]"
#elif defined(_M68020) || defined(__M68020) || defined(__mc68020)
  #define CPU " [020]"
#else
  #define CPU ""
#endif

/* the version stuff */
char * yamversion     = "YAM 2.4-dev" CPU;
char * yamversionstring = "$VER: YAM 2.4 (" __YAM_VERDATE ")" CPU " devel";
char * yamversiondate = __YAM_VERDATE;
unsigned long yamversiondays = __YAM_VERDAYS;

#if defined(__SASC)
  __near long __stack = 32768;
  __near long __buffsize = 8192;
#elif defined(__VBCC__) /* starting with VBCC 0.8 release */
  long __stack = 32768;
#elif defined(__GNUC__)
  /* neither of these are supported by GCC
  long __stack = 32768;
  long __buffsize = 8192;
  */
#endif

struct WBStartup *WBmsg;

/* no longer external visible, this is done by proto files! */
struct Library *       CManagerBase = NULL;
struct Library *       DataTypesBase = NULL;
struct Library *       GenesisBase = NULL;
struct Library *       IconBase = NULL;
struct Library *       IFFParseBase = NULL;
struct IntuitionBase * IntuitionBase = NULL;
struct Library *       KeymapBase = NULL;
struct LocaleBase *    LocaleBase = NULL;
struct Library *       MiamiBase = NULL;
struct Library *       MUIMasterBase = NULL;
struct Library *       OpenURLBase = NULL;
struct PopupMenuBase * PopupMenuBase = NULL;
struct RxsLib *        RexxSysBase = NULL;
struct Library *       SocketBase = NULL;
struct UtilityBase *   UtilityBase = NULL;
struct Library *       WorkbenchBase = NULL;
struct Library *       XpkBase = NULL;

char *Status[9] = { "U","O","F","R","W","E","H","S","N" };
char *SigNames[3] = { ".signature", ".altsignature1", ".altsignature2" };
char *FolderNames[4] = { "incoming", "outgoing", "sent", "deleted" };

char *ContType[MAXCTYPE+1] =
{
   /*CT_TX_PLAIN */ "text/plain",
   /*CT_TX_HTML  */ "text/html",
   /*CT_TX_GUIDE */ "text/x-aguide",
   /*CT_AP_OCTET */ "application/octet-stream",
   /*CT_AP_PS    */ "application/postscript",
   /*CT_AP_RTF   */ "application/rtf",
   /*CT_AP_LHA   */ "application/x-lha",
   /*CT_AP_LZX   */ "application/x-lzx",
   /*CT_AP_ZIP   */ "application/x-zip",
   /*CT_AP_AEXE  */ "application/x-amiga-executable",
   /*CT_AP_SCRIPT*/ "application/x-amigados-script",
   /*CT_AP_REXX  */ "application/x-rexx",
   /*CT_IM_JPG   */ "image/jpeg",
   /*CT_IM_GIF   */ "image/gif",
   /*CT_IM_PNG   */ "image/png",
   /*CT_IM_TIFF  */ "image/tiff",
   /*CT_IM_ILBM  */ "image/x-ilbm",
   /*CT_AU_AU    */ "audio/basic",
   /*CT_AU_8SVX  */ "audio/x-8svx",
   /*CT_AU_WAV   */ "audio/x-wav",
   /*CT_VI_MPG   */ "video/mpeg",
   /*CT_VI_MOV   */ "video/quicktime",
   /*CT_VI_ANIM  */ "video/x-anim",
   /*CT_VI_AVI   */ "video/x-msvideo",
   /*CT_ME_EMAIL */ "message/rfc822",
   NULL,
};

APTR ContTypeDesc[MAXCTYPE] =
{
   MSG_CTtextplain, MSG_CTtexthtml, MSG_CTtextaguide,
   MSG_CTapplicationoctetstream, MSG_CTapplicationpostscript, MSG_CTapplicationrtf, MSG_CTapplicationlha, MSG_CTapplicationlzx, MSG_CTapplicationzip, MSG_CTapplicationamigaexe, MSG_CTapplicationadosscript, MSG_CTapplicationrexx,
   MSG_CTimagejpeg, MSG_CTimagegif, MSG_CTimagepng, MSG_CTimagetiff, MSG_CTimageilbm,
   MSG_CTaudiobasic, MSG_CTaudio8svx, MSG_CTaudiowav,
   MSG_CTvideompeg, MSG_CTvideoquicktime, MSG_CTvideoanim, MSG_CTvideomsvideo,
   MSG_CTmessagerfc822
};

char *wdays[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
char *months[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
char *SecCodes[5] = { "none","sign","encrypt","sign+encrypt","anonymous" };
