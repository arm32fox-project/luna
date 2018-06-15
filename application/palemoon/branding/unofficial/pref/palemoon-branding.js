#filter substitution
#filter emptyLines
#include ../../shared/pref/preferences.inc
#include ../../shared/pref/uaoverrides.inc

pref("startup.homepage_override_url","http://www.palemoon.org/unofficial.shtml");
pref("app.releaseNotesURL", "http://www.palemoon.org/releasenotes.shtml");

// Native UA mode
pref("general.useragent.compatMode", 0);

// Updates disabled
pref("app.update.enabled", false);
pref("app.update.url", "");
