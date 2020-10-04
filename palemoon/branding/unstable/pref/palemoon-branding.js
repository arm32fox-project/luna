#filter substitution
#filter emptyLines
#include ../../shared/pref/preferences.inc
#include ../../shared/pref/uaoverrides.inc

pref("startup.homepage_override_url","http://www.palemoon.org/unstable/releasenotes.shtml");
pref("app.releaseNotesURL", "http://www.palemoon.org/unstable/releasenotes.shtml");

// Enable Firefox compatmode by default.
pref("general.useragent.compatMode", 2);
pref("general.useragent.compatMode.gecko", true);
pref("general.useragent.compatMode.firefox", true);

// Enable dynamic UA updates
pref("general.useragent.updates.enabled", true);
pref("general.useragent.updates.interval", 86400); // Once per day
pref("general.useragent.updates.retry", 7200);     // Retry getting update every 2 hours if failed
pref("general.useragent.updates.url", "https://dua.palemoon.org/?app=palemoon&version=%APP_VERSION%&channel=%CHANNEL%");

// Geolocation
pref("geo.wifi.uri", "https://pro.ip-api.com/json/?fields=lat,lon,status,message&key=K3TirHYiysBjnmD");

// ========================= updates ========================
#if defined(XP_WIN) || defined(XP_LINUX)
// Enable auto-updates for this channel
pref("app.update.auto", true);

// Updates enabled
pref("app.update.enabled", true);
pref("app.update.cert.checkAttributes", true);
pref("app.update.certs.1.issuerName", "CN=COMODO RSA Domain Validation Secure Server CA,O=COMODO CA Limited,L=Salford,ST=Greater Manchester,C=GB");
pref("app.update.certs.1.commonName", "*.palemoon.org");
pref("app.update.certs.2.issuerName", "CN=Sectigo RSA Domain Validation Secure Server CA,O=Sectigo Limited,L=Salford,ST=Greater Manchester,C=GB");
pref("app.update.certs.2.commonName", "*.palemoon.org");

// Interval: Time between checks for a new version (in seconds) -- 6 hours for unstable
pref("app.update.interval", 21600);
pref("app.update.promptWaitTime", 86400);

// URL user can browse to manually if for some reason all update installation
// attempts fail.
#ifndef XP_LINUX
pref("app.update.url.manual", "http://www.palemoon.org/unstable/");
#else
pref("app.update.url.manual", "http://linux.palemoon.org/download/unstable/");
#endif
// A default value for the "More information about this update" link
// supplied in the "An update is available" page of the update wizard. 
#ifndef XP_LINUX
pref("app.update.url.details", "http://www.palemoon.org/unstable/");
#else
pref("app.update.url.details", "http://linux.palemoon.org/download/unstable/");
#endif

#else
// Updates disabled (Mac, etc.)
pref("app.update.enabled", false);
pref("app.update.url", "");
#endif
