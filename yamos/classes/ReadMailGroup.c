/***************************************************************************

 YAM - Yet Another Mailer
 Copyright (C) 1995-2000 by Marcel Beck <mbeck@yam.ch>
 Copyright (C) 2000-2004 by YAM Open Source Team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 YAM Official Support Site : http://www.yam.ch
 YAM OpenSource project		 : http://sourceforge.net/projects/yamos/

 $Id$

 Superclass:  MUIC_Group
 Description: Mail read GUI group which include a texteditor and a header display

***************************************************************************/

#include "ReadMailGroup_cl.h"

#include "YAM_error.h"
#include "YAM_read.h"

/* CLASSDATA
struct Data
{
	struct ReadMailData *readMailData;
	Object *headerGroup;
	Object *headerList;
	Object *senderInfoGroup;
	Object *senderImageGroup;
	Object *senderImage;
	Object *balanceObject;
	Object *mailBodyGroup;
	Object *mailTextObject;
	Object *textEditScrollbar;
};
*/

/* Hooks */
/// HeaderDisplayHook
//  Header listview display hook
HOOKPROTONH(HeaderDisplayFunc, long, char **array, char *entry)
{
	static char hfield[40];
	char *cont = entry;
	int i = 0;

	// copy the headername into the static hfield to display it.
	while(*cont != ':' && *cont && i < 38)
		hfield[i++] = *cont++;
	
	hfield[i] = '\0';
	
	// set the array now so that the NList shows the correct values.
	array[0] = hfield;
	array[1] = stpblk(++cont);

	return 0;
}
MakeStaticHook(HeaderDisplayHook, HeaderDisplayFunc);
///
/// TextEditDoubleClickHook
//  Handles double-clicks on an URL
HOOKPROTONH(TextEditDoubleClickFunc, BOOL, APTR obj, struct ClickMessage *clickmsg)
{
	char *p;
	BOOL result = FALSE;

	DB(kprintf("DoubleClick: %ld - [%s]\n", clickmsg->ClickPosition, clickmsg->LineContents);)

	DoMethod(G->App, MUIM_Application_InputBuffered);

	// for safety reasons
	if(!clickmsg->LineContents)
		return FALSE;

	// if the user clicked on space we skip the following
	// analysis of a URL and just check if it was an attachment the user clicked at
	if(!ISpace(clickmsg->LineContents[clickmsg->ClickPosition]))
	{
		int pos = clickmsg->ClickPosition;
		char *line, *surl;
		static char url[SIZE_URL];
		enum tokenType type;

		// then we make a copy of the LineContents
		if(!(line = StrBufCpy(NULL, clickmsg->LineContents)))
			return FALSE;

		// find the beginning of the word we clicked at
		surl = &line[pos];
		while(surl != &line[0] && !ISpace(*(surl-1)))
			surl--;

		// now find the end of the word the user clicked at
		p = &line[pos];
		while(p+1 != &line[strlen(line)] && !ISpace(*(p+1)))
			p++;
		
		*(++p) = '\0';

		// now we start our quick lexical analysis to find a clickable element within
		// the doubleclick area
		if((type = ExtractURL(surl, url)))
		{
			switch(type)
			{
				case tEMAIL:
				{
					RE_ClickedOnMessage(url);
				}
				break;

				case tMAILTO:
				{
					RE_ClickedOnMessage(&url[7]);
				}
				break;

				case tHTTP:
				case tHTTPS:
				case tFTP:
				case tGOPHER:
				case tTELNET:
				case tNEWS:
				case tURL:
				{
					GotoURL(url);
				}
				break;

				default:
					// nothing
				break;
			}

			result = TRUE;
		}

		FreeStrBuf(line);
	}

	// if we still don`t have a result here we check if the user clicked on
	// a attachment.
	if(result == FALSE)
	{
		p = clickmsg->LineContents;
		if(isdigit(p[0]) && ((p[1] == ':' && p[2] == ' ') ||
			 (p[2] == ':' && p[3] == ' ')))
		{
			struct ReadMailData *rmData = (struct ReadMailData *)xget(_win(obj), MUIA_UserData);

			if(rmData)
			{
				struct Part *part;
				int partnr = atoi(p);

				for(part = rmData->firstPart; part; part = part->Next)
				{
					if(part->Nr == partnr)
						break;
				}

				if(part)
				{
					RE_DecodePart(part);
					RE_DisplayMIME(part->Filename, part->ContentType);

					result = TRUE;
				}
			}
		}
	}

	return result;
}
MakeStaticHook(TextEditDoubleClickHook, TextEditDoubleClickFunc);
///

/* Private Functions */

/* Overloaded Methods */
/// OVERLOAD(OM_NEW)
OVERLOAD(OM_NEW)
{
	struct Data *data;
	struct Data *tmpData;
	struct ReadMailData *rmData;

	// generate a temporar struct Data to which we store our data and
	// copy it later on
	if(!(data = tmpData = calloc(1, sizeof(struct Data))))
		return 0;

	// allocate the readMailData structure
	if(!(data->readMailData = rmData = calloc(1, sizeof(struct ReadMailData))))
		return 0;

	// set some default values for a new readMailGroup
	rmData->headerMode = C->ShowHeader;
	rmData->senderInfoMode = C->ShowSenderInfo;
	rmData->wrapHeaders = C->WrapHeader;
	rmData->noTextstyles = !C->UseTextstyles;
	rmData->useFixedFont = C->FixedFontEdit;
	
	// create some object before the real object
	data->textEditScrollbar = ScrollbarObject, End;

	if((obj = DoSuperNew(cl, obj,

		MUIA_Group_Horiz, FALSE,
		GroupSpacing(1),
		Child, data->headerGroup = HGroup,
			GroupSpacing(0),
			MUIA_VertWeight, 5,
			MUIA_ShowMe,		 rmData->headerMode != HM_NOHEADER,
			Child, NListviewObject,
				MUIA_NListview_NList, data->headerList = NListObject,
					InputListFrame,
					MUIA_NList_ConstructHook, 			 MUIV_NList_ConstructHook_String,
					MUIA_NList_DestructHook, 				 MUIV_NList_DestructHook_String,
					MUIA_NList_DisplayHook, 				 &HeaderDisplayHook,
					MUIA_NList_Format, 							 "P=\033r\0338 W=-1 MIW=-1,",
					MUIA_NList_Input, 							 FALSE,
					MUIA_NList_TypeSelect, 					 MUIV_NList_TypeSelect_Char,
					MUIA_NList_DefaultObjectOnClick, FALSE,
					MUIA_ContextMenu, 							 NULL,
					MUIA_CycleChain, 								 TRUE,
				End,
			End,
			Child, data->senderInfoGroup = ScrollgroupObject,
				MUIA_ShowMe, 								FALSE,
				MUIA_Scrollgroup_FreeHoriz, FALSE,
				MUIA_HorizWeight, 					0,
				MUIA_Scrollgroup_Contents, data->senderImageGroup = VGroupV,
					GroupSpacing(0),
					InputListFrame,
					Child, HVSpace,
				End,
			End,
		End,
		Child, data->balanceObject = BalanceObject,
			MUIA_ShowMe, rmData->headerMode != HM_NOHEADER,
		End,
		Child, data->mailBodyGroup = HGroup,
			GroupSpacing(0),
			Child, data->mailTextObject = MailTextEditObject,
				InputListFrame,
				MUIA_TextEditor_Slider, 	 			 data->textEditScrollbar,
				MUIA_TextEditor_FixedFont, 			 rmData->useFixedFont,
				MUIA_TextEditor_DoubleClickHook, &TextEditDoubleClickHook,
				MUIA_TextEditor_ImportHook, 		 MUIV_TextEditor_ImportHook_Plain,
				MUIA_TextEditor_ExportHook, 		 MUIV_TextEditor_ExportHook_Plain,
				MUIA_TextEditor_ReadOnly, 			 TRUE,
				MUIA_TextEditor_ColorMap, 			 G->EdColMap,
				MUIA_CycleChain, 								 TRUE,
			End,
			Child, data->textEditScrollbar,
		End,

		TAG_MORE, inittags(msg))))
	{
		GETDATA;

		if(!(data = (struct Data *)INST_DATA(cl,obj)))
			return 0;

		// copy back the data stored in our temporarly struct Data
		memcpy(data, tmpData, sizeof(struct Data));

		// place our data in the node and add it to the readMailDataList
		rmData->readMailGroup = obj;
		AddTail((struct List *)&(G->ReadMailDataList), (struct Node *)data->readMailData);
	}

	// free the temporary mem we allocated before
	free(tmpData);

	return (ULONG)obj;
}
///
/// OVERLOAD(OM_DISPOSE)
OVERLOAD(OM_DISPOSE)
{
	GETDATA;

	// Remove our readWindowNode and free it afterwards
	Remove((struct Node *)data->readMailData);
	free(data->readMailData);

	return DoSuperMethodA(cl, obj, msg);
}

///
/// OVERLOAD(OM_GET)
OVERLOAD(OM_GET)
{
	GETDATA;
	ULONG *store = ((struct opGet *)msg)->opg_Storage;

	switch(((struct opGet *)msg)->opg_AttrID)
	{
		ATTR(HGVertWeight) : *store = xget(data->headerGroup, MUIA_VertWeight); return TRUE;
		ATTR(TGVertWeight) : *store = xget(data->mailBodyGroup, MUIA_VertWeight); return TRUE;
		ATTR(ReadMailData) : *store = (ULONG)data->readMailData; return TRUE;
	}

	return DoSuperMethodA(cl, obj, msg);
}
///
/// OVERLOAD(OM_SET)
OVERLOAD(OM_SET)
{
	GETDATA;

	struct TagItem *tags = inittags(msg), *tag;
	while((tag = NextTagItem(&tags)))
	{
		switch(tag->ti_Tag)
		{
			ATTR(HGVertWeight) : set(data->headerGroup, MUIA_VertWeight, tag->ti_Data); break;
			ATTR(TGVertWeight) : set(data->mailBodyGroup, MUIA_VertWeight, tag->ti_Data); break;
		}
	}

	return DoSuperMethodA(cl, obj, msg);
}
///

/* Public Methods */
/// DECLARE(Clear)
DECLARE(Clear)
{
	GETDATA;

	// clear all relevant GUI elements
	DoMethod(data->headerList, MUIM_NList_Clear);
	set(data->mailTextObject, MUIA_TextEditor_Contents, "");

	return 0;
}

///
/// DECLARE(ReadMail)
DECLARE(ReadMail) // struct Mail *mail, BOOL updateOnly
{
	GETDATA;
	struct Mail *mail = msg->mail;
	struct Folder *folder = mail->Folder;
	struct ReadMailData *rmData = data->readMailData;
	char *cmsg;
	BOOL result = FALSE; // error per default

	// set the passed mail as the current mail read by our ReadMailData
	// structure
	rmData->mail = mail;

	// load the message now
	if(RE_LoadMessage(rmData, PM_ALL))
	{
		// now read in the Mail in a temporary buffer
		BusyText(GetStr(MSG_BusyDisplaying), "");
		if((cmsg = RE_ReadInMessage(rmData, RIM_READ)))
		{
			struct Person *from = &rmData->mail->From;
			struct ABEntry *ab = NULL;
			struct ABEntry abtmpl;
			char headername[SIZE_DEFAULT];
			char *body;
			BOOL dispheader;
			int hits;
			int i;

			dispheader = (rmData->headerMode != HM_NOHEADER);
			set(data->headerGroup, MUIA_ShowMe, dispheader);
			set(data->balanceObject, MUIA_ShowMe, dispheader);
			DoMethod(data->headerList, MUIM_NList_Clear);
		
			set(data->headerList, MUIA_NList_Quiet, TRUE);
			body = cmsg;

			// here we have to parse the header and place all header
			// information in the header listview.
			while(*body)
			{
				// if the first char in the line is a newline \n, then we found the start of
				// the body and break here.
				if(*body == '\n') { body++; break; }

				// we copy the headername from the mail as long as there is no space and
				// no interrupting character is found
				for(i = 0; body[i] != ':' && !isspace(body[i]) && body[i] != '\n' && body[i] != '\0' && i < SIZE_DEFAULT-1; i++)
				{
					headername[i] = body[i];
				}

				// if we end up here and body[i] isn`t a : then this wasn`t a proper headerline and we
				// can ignore it anyway because the RFC says that a headerline must have characters
				// without any space followed by a ":"
				if(body[i] == ':')
				{
					headername[i] = '\0'; // terminate with 0
				
					// Now we check if this is a header the user wants to be displayed if he has choosen
					// to display only shortheaders
					if(rmData->headerMode == HM_SHORTHEADER)
						dispheader = MatchNoCase(headername, C->ShortHeaders);
					else
						dispheader = (rmData->headerMode == HM_FULLHEADER);

					if(dispheader)
					{
						// we simply insert the whole thing from the actual body pointer
						// because the ConstructHook_String of NList will anyway just copy until a \n, \r or \0
						DoMethod(data->headerList, MUIM_NList_InsertSingleWrap, body,
																			 MUIV_NList_Insert_Bottom,
																			 rmData->wrapHeaders ? WRAPCOL1 : NOWRAP, ALIGN_LEFT);
					}
				}

				// then we move forward until the end of the line
				while(*body && *body != '\n')
					body++;
			
				if(*body)
					body++; // if the end of the line isn`t a \0 we have to move on
			}

			if((hits = AB_SearchEntry(from->Address, ASM_ADDRESS|ASM_USER, &ab)) == 0 &&
				 *from->RealName)
			{
				hits = AB_SearchEntry(from->RealName, ASM_REALNAME|ASM_USER, &ab);
			}

			RE_GetSenderInfo(rmData->mail, &abtmpl);

			if(!stricmp(from->Address, C->EmailAddress) || !stricmp(from->RealName, C->RealName))
			{
				if(!ab)
				{
					ab = &abtmpl;
					*ab->Photo = 0;
				}
			}
			else
			{
				if(ab)
				{
					RE_UpdateSenderInfo(ab, &abtmpl);
					if(!msg->updateOnly && C->AddToAddrbook > 0 && !*ab->Photo && *abtmpl.Photo && *C->GalleryDir)
					{
						RE_DownloadPhoto(_win(obj), abtmpl.Photo, ab);
					}
				}
				else
				{
					if(!msg->updateOnly && C->AddToAddrbook > 0 && (ab = RE_AddToAddrbook(_win(obj), &abtmpl)))
					{
						if(*abtmpl.Photo && *C->GalleryDir)
							RE_DownloadPhoto(_win(obj), abtmpl.Photo, ab);
					}
					else
					{
						ab = &abtmpl;
						*ab->Photo = 0;
					}
				}
			}
		
			if(rmData->senderInfoMode != SIM_OFF)
			{
				if(hits == 1 || ab->Type == AET_LIST)
				{
					// Add some extra headers with sender info
					char buffer[SIZE_LARGE];

					if(*ab->RealName)
					{
						sprintf(buffer, MUIX_I"%s: %s", StripUnderscore(GetStr(MSG_EA_RealName)), ab->RealName);
						DoMethod(data->headerList, MUIM_NList_InsertSingle, buffer, MUIV_NList_Insert_Bottom);
					}

					if(*ab->Street)
					{
						sprintf(buffer, MUIX_I"%s: %s", StripUnderscore(GetStr(MSG_EA_Street)), ab->Street);
						DoMethod(data->headerList, MUIM_NList_InsertSingle, buffer, MUIV_NList_Insert_Bottom);
					}

					if(*ab->City)
					{
						sprintf(buffer, MUIX_I"%s: %s", StripUnderscore(GetStr(MSG_EA_City)), ab->City);
						DoMethod(data->headerList, MUIM_NList_InsertSingle, buffer, MUIV_NList_Insert_Bottom);
					}

					if(*ab->Country)
					{
						sprintf(buffer, MUIX_I"%s: %s", StripUnderscore(GetStr(MSG_EA_Country)), ab->Country);
						DoMethod(data->headerList, MUIM_NList_InsertSingle, buffer, MUIV_NList_Insert_Bottom);
					}

					if(*ab->Phone)
					{
						sprintf(buffer, MUIX_I"%s: %s", StripUnderscore(GetStr(MSG_EA_Phone)), ab->Phone);
						DoMethod(data->headerList, MUIM_NList_InsertSingle, buffer, MUIV_NList_Insert_Bottom);
					}

					if(*AB_ExpandBD(ab->BirthDay))
					{
						sprintf(buffer, MUIX_I"%s: %s", StripUnderscore(GetStr(MSG_EA_DOB)), AB_ExpandBD(ab->BirthDay));
						DoMethod(data->headerList, MUIM_NList_InsertSingle, buffer, MUIV_NList_Insert_Bottom);
					}

					if(*ab->Comment)
					{
						sprintf(buffer, MUIX_I"%s: %s", StripUnderscore(GetStr(MSG_EA_Description)), ab->Comment);
						DoMethod(data->headerList, MUIM_NList_InsertSingle, buffer, MUIV_NList_Insert_Bottom);
					}

					if(*ab->Homepage)
					{
						sprintf(buffer, MUIX_I"%s: %s", StripUnderscore(GetStr(MSG_EA_Homepage)), ab->Homepage);
						DoMethod(data->headerList, MUIM_NList_InsertSingle, buffer, MUIV_NList_Insert_Bottom);
					}
				}

				if(rmData->senderInfoMode == SIM_ALL &&
					 DoMethod(data->senderImageGroup, MUIM_Group_InitChange))
				{
					char photopath[SIZE_PATHFILE];

					if(data->senderImage)
					{
						DoMethod(data->senderImageGroup, OM_REMMEMBER, data->senderImage);
						MUI_DisposeObject(data->senderImage);
					}
					data->senderImage = NULL;
					if(RE_FindPhotoOnDisk(ab, photopath))
					{
						data->senderImage = MakePicture(photopath);
						DoMethod(data->senderImage, OM_ADDMEMBER, data->senderImageGroup);
					}
					DoMethod(data->senderImageGroup, MUIM_Group_ExitChange);
				}
			}
			set(data->senderInfoGroup, MUIA_ShowMe, (rmData->senderInfoMode == SIM_ALL) &&
																							(data->senderImage != NULL));
			
			// enable the headerList again
			set(data->headerList, MUIA_NList_Quiet, FALSE);

			// before we can put the message body into the TextEditor, we have to preparse the text and
			// try to set some styles, as we don`t use the buggy ImportHooks of TextEditor anymore and are anyway
			// more powerful this way.
			if(!rmData->noTextstyles)
				body = ParseEmailText(body);

			SetAttrs(data->mailTextObject, MUIA_TextEditor_FixedFont, rmData->useFixedFont,
																		 MUIA_TextEditor_Contents,  body,
																		 TAG_DONE);

			// free the parsed text afterwards as the texteditor has copied it anyway.
			if(!rmData->noTextstyles)
				free(body);

			free(cmsg);

			// start the macro
			if(rmData->readWindow)
				MA_StartMacro(MACRO_READ, itoa(xget(rmData->readWindow, MUIA_ReadWindow_Num)));
			else
				MA_StartMacro(MACRO_READ, NULL);

			// if the displayed mail isn't a virtual one we flag it as read now
			if(!isVirtualMail(mail) &&
         (hasStatusNew(mail) || !hasStatusRead(mail)))
      {
				setStatusToRead(mail); // set to OLD
				DisplayStatistics(folder, TRUE);

				// and dependingnon the PGP status we either check an existing
				// PGP signature or not.
				if((hasPGPSOldFlag(rmData) || hasPGPSMimeFlag(rmData))
					 && !hasPGPSCheckedFlag(rmData))
				{
					DoMethod(obj, MUIM_ReadMailGroup_CheckPGPSignature, FALSE);
				}
				 
				// check for any MDN and allow to reply to it.
				RE_DoMDN(MDN_READ, mail, FALSE);
      }

			// everything worked out fine so lets return it
			result = TRUE;
		}
		else
			ER_NewError(GetStr(MSG_ER_ErrorReadMailfile), GetMailFile(NULL, folder, mail), NULL);

		BusyEnd();
	}
	else
	{
		// check first if the mail file exists and if not we have to exit with an error
		if(!FileExists(mail->MailFile))
		{
			ER_NewError(GetStr(MSG_ER_CantOpenFile), GetMailFile(NULL, folder, mail), NULL);
		}
	}

	return result;
}

///
/// DECLARE(CheckPGPSignature)
DECLARE(CheckPGPSignature) // BOOL forceRequester
{
	GETDATA;
	struct ReadMailData *rmData = data->readMailData;

	// Don't try to use PGP if it's not installed
	if(G->PGPVersion == 0)
		return FALSE;

	if((hasPGPSOldFlag(rmData) || hasPGPSMimeFlag(rmData)) &&
		 !hasPGPSCheckedFlag(rmData))
	{
		int error;
		char fullfile[SIZE_PATHFILE], options[SIZE_LARGE];
		
		if(!StartUnpack(GetMailFile(NULL, NULL, rmData->mail), fullfile, rmData->mail->Folder))
			return FALSE;
		
		sprintf(options, (G->PGPVersion == 5) ? "%s -o %s +batchmode=1 +force +language=us" : "%s -o %s +bat +f +lang=en", fullfile, "T:PGP.tmp");
		error = PGPCommand((G->PGPVersion == 5) ? "pgpv": "pgp", options, KEEPLOG);
		FinishUnpack(fullfile);
		DeleteFile("T:PGP.tmp");
		if(error > 0)
			SET_FLAG(rmData->signedFlags, PGPS_BADSIG);
		
		if(error >= 0)
			RE_GetSigFromLog(rmData, NULL);
		else
			return FALSE;
	}

	if(hasPGPSBadSigFlag(rmData) || msg->forceRequester)
	{
		char buffer[SIZE_LARGE];
		
		strcpy(buffer, hasPGPSBadSigFlag(rmData) ? GetStr(MSG_RE_BadSig) : GetStr(MSG_RE_GoodSig));
		if(hasPGPSAddressFlag(rmData))
		{
			strcat(buffer, GetStr(MSG_RE_SigFrom));
			strcat(buffer, rmData->sigAuthor);
		}
		
		MUI_Request(G->App, obj, 0, GetStr(MSG_RE_SigCheck), GetStr(MSG_Okay), buffer);
	}

	return TRUE;
}

///
/// DECLARE(ExtractPGPKey)
//  Extracts public PGP key from the current message
DECLARE(ExtractPGPKey)
{
	GETDATA;
	struct ReadMailData *rmData = data->readMailData;
	struct Mail *mail = rmData->mail;
	char fullfile[SIZE_PATHFILE], options[SIZE_PATHFILE];

	if(StartUnpack(GetMailFile(NULL, NULL, mail), fullfile, mail->Folder))
	{
		sprintf(options, (G->PGPVersion == 5) ? "-a %s +batchmode=1 +force" : "-ka %s +bat +f", fullfile);
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
			res.pos = MUIV_NList_GetEntryInfo_Line;
			res.line = i;
			
			DoMethod(data->headerList, MUIM_NList_GetEntryInfo, &res);
			if(!res.entry)
				break;

			ptr = (char *)res.entry;
			if(!strcmp(ptr, MUIX_I))
				ptr += strlen(MUIX_I);
			
			fputs(ptr, fh);
			fputc('\n', fh);
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
					if(!strncmp(ptr, "[s:18]", 6))
						fputs("===========================================================", fh);
					else if(!strncmp(ptr, "[s:2]", 5))
						fputs("-----------------------------------------------------------", fh);
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
	struct WritePart *p1;
	int choice;
	char mfile[SIZE_MFILE];

	if(!folder)
		return 0;

	if((choice = MUI_Request(G->App, rmData->readWindow, 0, GetStr(MSG_RE_SaveDecrypted),
																													GetStr(MSG_RE_SaveDecGads),
																													GetStr(MSG_RE_SaveDecReq))))
	{
		struct Compose comp;
		memset(&comp, 0, sizeof(struct Compose));

		if((comp.FH = fopen(MA_NewMailFile(folder, mfile), "w")))
		{
			struct ExtendedMail *email;

      comp.Mode = NEW_SAVEDEC;
			comp.OrigMail = mail;
      comp.FirstPart = p1 = NewPart(2);
			p1->Filename = rmData->firstPart->Next->Filename;
      WriteOutMessage(&comp);
      FreePartsList(p1);
      fclose(comp.FH);

			if((email = MA_ExamineMail(folder, mfile, TRUE)))
      {
				struct Mail *newmail;

				// lets set some values depending on the original message
				email->Mail.sflags = mail->sflags;
				memcpy(&email->Mail.transDate, &mail->transDate, sizeof(struct timeval));

				// add the mail to the folder now
				newmail = AddMailToList(&email->Mail, folder);

				// if this was a compressed/encrypted folder we need to pack the mail now
				if(folder->Mode > FM_SIMPLE)
					RepackMailFile(newmail, -1, NULL);

				if(FO_GetCurrentFolder() == folder)
					DoMethod(G->MA->GUI.NL_MAILS, MUIM_NList_InsertSingle, newmail, MUIV_NList_Insert_Sorted);
				
				MA_FreeEMailStruct(email);
				if(choice == 2)
				{
					MA_DeleteSingle(mail, FALSE, FALSE);

					DoMethod(rmData->readWindow, MUIM_ReadWindow_ReadMail, newmail);
				}
      }
			else
				ER_NewError(GetStr(MSG_ER_CreateMailError), NULL, NULL);
		}
	}

	return 0;
}

///

