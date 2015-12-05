/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TestHarness.h"
#include "nsIXSSUtils.h"



int main(int argc, char** argv)
{

  ScopedXPCOM xpcom("XSSUtils");
  if (xpcom.failed()) {
    return 1;
  }

  int retval = 0;
  nsresult rv;
  nsCOMPtr<nsIXSSUtils> xss =
    do_GetService("@mozilla.com/xssutils;1", &rv);
  if (!xss) {
    printf("XSS: null ptr\n");
    retval = 1;
  }
  
  if (xss->Test() != NS_OK)
    retval = 1;
    
  return retval;
}
