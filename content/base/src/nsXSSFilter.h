/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// TODO: do we have a MIME parser for POST requests? brandon?  jonas
// wrote one for the sjs server of html/content, but it's a javascript
// simplification and can only be copied, not reused.
// thunderbird can definitely parse mime, but it might
// be a little bit overkill. look at the AsyncConvertData
// method. right now we are just appending the whole mime data as a
// parameter, but that might hurt performance for large POSTs. I think
// chrome had the same problem, how did they solve it?

// TODO: what about report-only and report-url? we could copy the idea
// from CSPs. No other filter has them though, so we would need to
// come up with new headers.

// TODO: should i comment out the static test stuff in optimized
// builds? it might appear as static data in libxul, taking up memory.

// TODO: caching eval result? I thought it was useless, but it seems
// that the JS engine is caching eval compilation already... should we
// cache XSS checks along with the compiled code?

// TODO: the filter can be configured to try to stop HPP-based XSS as well.
// e.g. http://server/?q=<script>&q=alert(1)&q=</script>
// they get concatenated!
// we could reassemble the params w/o the param names and equal signs.
// perhaps we can trigger this iff a param has been seen twice.

// TODO: it would be great to cache jsurl and data url results as
// well, as we do with event listeners. Unfortunately, the channels
// where we interpose are recreated for each single execution, thus no
// state can be kept. Moving the interposition up in the docshell does
// not seem like a good idea either.


#ifndef nsXSSFilter_h
#define nsXSSFilter_h

#include "nsTArray.h"
#include "jsapi.h"
#include "nsXSSUtils.h"

class nsIURI;
class nsIHttpChannel;
class nsAString;
class nsIDocument;

/**
 * A refcounted C++ class that every document owns through its
 * principal. Before executing potentially injected JavaScript, hooks
 * scattered through the codebase retrieve the principal and call back
 * into these functions to check whether the code about to be executed
 * is an XSS attack. Most of the hooks are placed along with CSP
 * checks, but we don't currently use the nsIContentPolicy for all
 * external resources.
 */
class nsXSSFilter {

 public:
  nsXSSFilter(nsIDocument *parent);
  virtual ~nsXSSFilter();

  NS_INLINE_DECL_REFCOUNTING(nsXSSFilter)

  /**
   * Called after the XSS object is created to look at the
   * X-XSS-Protection header and configure the filter.
   */
  nsresult ScanRequestData();
  /**
   * Checks whether an inlined <script>...</script> element should be
   * executed.
   */
  bool PermitsInlineScript(const nsAString& aScript);
  /**
   * Checks whether an external <script src=...></script> element
   * should be fetched and executed.
   */
  bool PermitsExternalScript(nsIURI *aURI, bool isDynamic);
  /**
   * Checks whether a URL with the javascript: protocol should be
   * allowed. Lazy.
   */
  bool PermitsJSUrl(const nsAString& aURI);
  /**
   * Checks whether an event listener should be fired due to an event.
   * Lazy, and caches results for further executions of the handler.
   */
  bool PermitsEventListener(const nsAString& aScript);
  /**
   * Checks whether the base URL for a document should be changed by a
   * <base href=... /> element.
   */
  bool PermitsBaseElement(nsIURI *aOldURI, nsIURI* aNewURI);
  /**
   * Checks whether an object element specifying an external resource
   * should be fetched and loaded.
   */
  bool PermitsExternalObject(nsIURI *aURI);
  /**
   * Checks whether a data URL that contains a script should be
   * executed. Lazy.
   */
  bool PermitsDataURL(nsIURI *aURI);
  /**
   * Checks whether the string argument from certain calls in
   * JavaScript (eval, setTimeout, setInterval) should be executed.
   */
  bool PermitsJSAction(const nsAString& code);

  /**
   * Sync the whitelist preference to the memory structure
   */
  static int InitializeWhiteList(const char*, void*);
  /**
   * Sets up Observers for the static preferences described below.
   */
  static void InitializeStatics();
  /**
   * Synced to preference security.xssfilter.enabled
   * The filter allows all if this is set to false.
   */
  static bool sXSSEnabled;
  /**
   * Synced to security.xssfilter.reportOnly. When set to true,
   * policy calls will report violations to observers, but will allow
   * all script content regardless.
   */
  static bool sReportOnly;
  /**
   * Synced to security.xssfilter.blockMode. When set to true, all
   * violations will terminate execution of the page and will
   * display an error page.
   */
  static bool sBlockMode;
  /**
   * Synced to security.xssfilter.ignoreHeaders. When set to true,
   * the X-Xss-Protection headers in pages will be ignored.
   */
  static bool sIgnoreHeaders;
  /**
   * Synced to security.xssfilter.blockDynamic. When set to false, DOM
   * based vectors will be ignored.
   */
  static bool sBlockDynamic;
  /**
   * Synced to security.xssfilter.whitelist.
   */
  static DomainMap sWhiteList;

 private:
  /**
   * An array of parameters extracted from the GET/POST
   * request. Lazily evaluated through GetParams().
   */
  ParameterArray mParams;
  /**
   * Whether we still need to fill in params. (b/c empty != uninitialized)
   */
  bool mParamsInitialized;
  /**
   * The document that owns the filter (through its principal).
   */
  nsIDocument *mParentDoc;
  /**
   * A cache of safe domains to avoid checking URLs for the same
   * domain twice. Used by external URLs and objects.
   */
  DomainMap mDomainCache;
  /**
   * Whether the XSS filter is enabled or disabled by the
   * X-XSS-Protection header.
   */
  bool mIsEnabled;
  /**
   * Whether the XSS header explicitly requests block mode.
   */
  bool mBlockMode;
  /**
   * Returns true if XSS is enabled (pref && header)
   */
  bool IsEnabled();
  /**
   * Returns true if report only mode is enabled (pref).
   */
  bool IsReportOnly();
  /**
   * Returns true if block mode is enabled (pref || header).
   */
  bool IsBlockMode();
  /**
   * Returns true if DOM-Based blocking is enabled (pref).
   */
  bool IsBlockDynamic();
  /**
   * Gets the whitelist
   */
  DomainMap& GetWhiteList();
  /**
   * Gets the URL of the document for origin checks
   */
  nsIURI* GetURI();
  /**
   * Gets the array of parameters provided through the request for the
   * taint-inference algorithm. These are lazily evaluated.
   */
  ParameterArray& GetParams();
  /**
   * Lazily Initializes the domain cache
   */
  DomainMap& GetDomainCache();
  /**
   * Notifies the user of an XSS Violation. Three events are generated:
   * 1) message to error console
   * 2) notify observers of xss-on-violate-policy (Currently, there is a
   * pref that is used by browser.js to popup a notification. We plan
   * something less consipcuous for the final version)
   * 3) If block mode is enabled, it also navigates away the tab to an
   * error page.
   */
  nsresult NotifyViolation(const nsAString& policy, const nsAString& content, const nsAString& domain);
};




#endif /* nsXSSFilter_h */
