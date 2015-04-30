/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsGNOMERegistry.h"
#include "nsString.h"
#include "nsIComponentManager.h"
#include "nsMIMEInfoUnix.h"
#include "nsAutoPtr.h"
#include "nsIGIOService.h"

#ifdef MOZ_PLATFORM_MAEMO
#include <libintl.h>
#endif

/* static */ bool
nsGNOMERegistry::HandlerExists(const char *aProtocolScheme)
{
  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
  if (!giovfs) {
	return false;
  }

  nsCOMPtr<nsIGIOMimeApp> app;
  return NS_SUCCEEDED(giovfs->GetAppForURIScheme(nsDependentCString(aProtocolScheme),
                                                 getter_AddRefs(app)));
}

// XXX Check HandlerExists() before calling LoadURL.

/* static */ nsresult
nsGNOMERegistry::LoadURL(nsIURI *aURL)
{
  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
  if (!giovfs) {
    return NS_ERROR_FAILURE;
  }

  return giovfs->ShowURI(aURL);
}

/* static */ void
nsGNOMERegistry::GetAppDescForScheme(const nsACString& aScheme,
                                     nsAString& aDesc)
{
  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
  if (!giovfs)
    return;

  nsAutoCString name;
  nsCOMPtr<nsIGIOMimeApp> app;
  if (NS_FAILED(giovfs->GetAppForURIScheme(aScheme, getter_AddRefs(app))))
    return;

  app->GetName(name);

  CopyUTF8toUTF16(name, aDesc);
}


/* static */ already_AddRefed<nsMIMEInfoBase>
nsGNOMERegistry::GetFromExtension(const nsACString& aFileExt)
{
  nsAutoCString mimeType;
  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
  if (!giovfs) {
    return nullptr;
  }

  // Get the MIME type from the extension, then call GetFromType to
  // fill in the MIMEInfo.
  if (NS_FAILED(giovfs->GetMimeTypeFromExtension(aFileExt, mimeType)) ||
      mimeType.EqualsLiteral("application/octet-stream")) {
    return nullptr;
  }

  nsRefPtr<nsMIMEInfoBase> mi = GetFromType(mimeType);
  if (mi) {
    mi->AppendExtension(aFileExt);
  }

  return mi.forget();
}

/* static */ already_AddRefed<nsMIMEInfoBase>
nsGNOMERegistry::GetFromType(const nsACString& aMIMEType)
{
  nsRefPtr<nsMIMEInfoUnix> mimeInfo = new nsMIMEInfoUnix(aMIMEType);
  NS_ENSURE_TRUE(mimeInfo, nullptr);

  nsAutoCString name;
  nsAutoCString description;

  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
  if (!giovfs) {
    return nullptr;
  }

  nsCOMPtr<nsIGIOMimeApp> gioHandlerApp;
  if (NS_FAILED(giovfs->GetAppForMimeType(aMIMEType, getter_AddRefs(gioHandlerApp))) ||  
      !gioHandlerApp) {
    return nullptr;
  }
  gioHandlerApp->GetName(name);
  giovfs->GetDescriptionForMimeType(aMIMEType, description);

#ifdef MOZ_PLATFORM_MAEMO
  // On Maemo/Hildon, GetName ends up calling gnome_vfs_mime_application_get_name,
  // which happens to return a non-localized message-id for the application. To
  // get the localized name for the application, we have to call dgettext with 
  // the default maemo domain-name to try and translate the string into the operating 
  // system's native language.
  const char kDefaultTextDomain [] = "maemo-af-desktop";
  nsAutoCString realName (dgettext(kDefaultTextDomain, PromiseFlatCString(name).get()));
  mimeInfo->SetDefaultDescription(NS_ConvertUTF8toUTF16(realName));
#else
  mimeInfo->SetDefaultDescription(NS_ConvertUTF8toUTF16(name));
#endif
  mimeInfo->SetPreferredAction(nsIMIMEInfo::useSystemDefault);
  mimeInfo->SetDescription(NS_ConvertUTF8toUTF16(description));

  return mimeInfo.forget();
}
