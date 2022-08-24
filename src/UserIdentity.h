#ifndef USERIDENTITY_H
#define USERIDENTITY_H

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

#include "YAM_stringsizes.h"

// forward declarations
struct Config;
struct MailServerNode;
struct Folder;
struct ExtendedMail;
struct SignatureNode;

enum QuotePos { QPOS_BELOW=0, QPOS_ABOVE };
enum SigPos { SPOS_BELOW=0, SPOS_ABOVE };

// user identity data structure
struct UserIdentityNode
{
  struct MinNode node;               // required for placing it into struct Config

  int id;                            // unique id for the user identity
  BOOL active;                       // is this user identity currently active?

  char description[SIZE_LARGE];      // user definable description
  char realname[SIZE_REALNAME];      // real name (firstname + lastname)
  char address[SIZE_ADDRESS];        // email address
  char organization[SIZE_DEFAULT];   // organization
  struct MailServerNode *smtpServer; // ptr to SMTP server node
  struct SignatureNode *signature;   // ptr to Signature node (or NULL)
  char mailCC[SIZE_LARGE];           // predefined CC address field
  char mailBCC[SIZE_LARGE];          // predefined BCC address field
  char mailReplyTo[SIZE_LARGE];      // predefined ReplyTo address field
  char extraHeaders[SIZE_LARGE];     // user definable extra headers
  char photoURL[SIZE_URL];           // user definable URL to photo image

  int sentFolderID;                  // folder ID for storing sent mail to
  char sentFolderName[SIZE_NAME];    // folder name for storing sent mail to

  BOOL saveSentMail;                 // store sent mail to the folder yes/no
  BOOL quoteMails;                   // TRUE if user has quoted text enabled when replying/forwarding
  enum QuotePos quotePosition;       // position where the quote should appear
  enum SigPos signaturePosition;     // where should the signature appear
  BOOL sigReply;                     // use signature when replying
  BOOL sigForwarding;                // use signature when forwarding
  BOOL addPersonalInfo;              // add personal info when sending mails (in header)
  BOOL requestMDN;                   // request a mail disposition notification per default

  BOOL usePGP;                       // use PGP features per default
  char pgpKeyID[SIZE_DEFAULT];       // PGP key Identifier
  char pgpKeyURL[SIZE_URL];          // PGP key URL
  BOOL pgpSignUnencrypted;           // sign unencrypted mail per default
  BOOL pgpSignEncrypted;             // sign encrypted mail per default
  BOOL pgpEncryptAll;                // encrypt all outgoing mails per default
  BOOL pgpSelfEncrypt;               // when encrypting also add own key
};

// public functions
struct UserIdentityNode *CreateNewUserIdentity(const struct Config *co);
void DeleteUserIdentity(struct UserIdentityNode *uin);
void FreeUserIdentityList(struct MinList *userIdentityList);
BOOL CompareUserIdentityLists(const struct MinList *msl1, const struct MinList *msl2);
struct UserIdentityNode *GetUserIdentity(const struct MinList *userIdentityList, const unsigned int num, const BOOL activeOnly);
struct UserIdentityNode *WhichUserIdentity(const struct MinList *userIdentityList, const struct ExtendedMail *email);
struct UserIdentityNode *FindUserIdentityByID(const struct MinList *userIdentityList, const int id);
struct UserIdentityNode *FindUserIdentityByDescription(const struct MinList *userIdentityList, const char *description);
struct UserIdentityNode *FindUserIdentityByAddress(const struct MinList *userIdentityList, const char *address);
ULONG NumberOfUserIdentities(const struct MinList *userIdentityList);
LONG IndexOfUserIdentity(const struct MinList *userIdentityList, const struct UserIdentityNode *uin);

#endif // USERIDENTITY_H
