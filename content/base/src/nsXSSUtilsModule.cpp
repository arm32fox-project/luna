/* -*-  Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string.h>

#include "nscore.h"
#include "pratom.h"
#include "prmem.h"
#include "prio.h"
#include "plstr.h"
#include "prlog.h"

#include "nsID.h"
#include "nsIComponentManager.h"
#include "nsIServiceManager.h"
#include "nsCOMPtr.h"
#include "nsIModule.h"
#include "mozilla/ModuleUtils.h"
#include "nsXSSUtils.h"

// XXX Need help with guard for ENABLE_TEST
NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(nsXSSUtilsWrapper,
                                         nsXSSUtilsWrapper::GetSingleton)
NS_DEFINE_NAMED_CID(NS_XSSUTILS_CID);

static const mozilla::Module::CIDEntry kXSSUtilsCIDs[] = {
    { &kNS_XSSUTILS_CID, false, NULL, nsXSSUtilsWrapperConstructor },
    { NULL }
};

static const mozilla::Module::ContractIDEntry kXSSUtilsContracts[] = {
    { NS_XSSUTILS_CONTRACTID, &kNS_XSSUTILS_CID },
    { NULL }
};

static const mozilla::Module kXSSUtilsModule = {
    mozilla::Module::kVersion,
    kXSSUtilsCIDs,
    kXSSUtilsContracts,
    NULL,
    NULL,
    NULL,
    NULL
};

NSMODULE_DEFN(XSSUtilsModule) = &kXSSUtilsModule;
