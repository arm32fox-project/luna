/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _NSSSLSTATUS_H
#define _NSSSLSTATUS_H

#include "nsISSLStatus.h"
#include "nsCOMPtr.h"
#include "nsXPIDLString.h"
#include "nsIX509Cert.h"
#include "nsISerializable.h"
#include "nsIClassInfo.h"

class nsSSLStatus
  : public nsISSLStatus
  , public nsISerializable
  , public nsIClassInfo
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISSLSTATUS
  NS_DECL_NSISERIALIZABLE
  NS_DECL_NSICLASSINFO

  nsSSLStatus();
  virtual ~nsSSLStatus();

  /* public for initilization in this file */
  nsCOMPtr<nsIX509Cert> mServerCert;

  uint32_t mKeyLength;
  uint32_t mSecretKeyLength;
  nsXPIDLCString mCipherName;
  nsXPIDLCString mCipherSuite;
  
  uint32_t mProtocolVersion;

  bool mIsDomainMismatch;
  bool mIsNotValidAtThisTime;
  bool mIsUntrusted;

  bool mHaveKeyLengthAndCipher;

  /* mHaveCertErrrorBits is relied on to determine whether or not a SPDY
     connection is eligible for joining in nsNSSSocketInfo::JoinConnection() */
  bool mHaveCertErrorBits;
};

//dd0197a2-7f40-4224-bd7a-d7a372bd0032
#define NS_SSLSTATUS_CID \
{ 0xdd0197a2, 0x7f40, 0x4224, \
  { 0xbd, 0x7a, 0xd7, 0xa3, 0x72, 0xbd, 0x00, 0x32 } }

#endif
