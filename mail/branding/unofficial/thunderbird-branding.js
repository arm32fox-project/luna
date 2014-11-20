// Default start page
pref("mailnews.start_page.url","about:blank");

// first launch welcome page
pref("mailnews.start_page.welcome_url","about:blank");

// start page override to load after an update
pref("mailnews.start_page.override_url","about:blank");

// Application updates
pref("app.update.interval", 86400);
pref("app.update.download.backgroundInterval", 60);
pref("app.update.cert.requireBuiltIn", false);
pref("app.update.cert.checkAttributes", false);
pref("app.update.cert.maxErrors", 5);
pref("app.update.enabled", false);               
pref("app.update.auto", false);

pref("app.update.mode", 2);
pref("app.update.silent", false);
pref("app.update.staging.enabled", false);
pref("app.update.url", "");
pref("app.update.url.manual", "");
pref("app.update.url.details", "");
pref("app.update.idletime", 600);
pref("app.update.showInstalledUI", true);

pref("app.update.service.enabled", false);

// Vendor and app URLS
pref("app.vendorURL", "http://www.fossamail.org/");
pref("mail.cloud_files.inserted_urls.footer.link", "http://www.fossamail.org/");

pref("app.releaseNotesURL","http://www.fossamail.org/releasenotes.shtml");
pref("app.support.baseURL","http://support.live.mozillamessaging.com/%LOCALE%/thunderbird/%APPBUILDID%/");

// Preferences for AMO integration
pref("extensions.getAddons.cache.enabled", true);
pref("extensions.getAddons.maxResults", 15);
pref("extensions.getAddons.get.url", "https://services.addons.mozilla.org/%LOCALE%/thunderbird/api/%API_VERSION%/search/guid:%IDS%?src=thunderbird&appOS=%OS%&appVersion=%VERSION%");
pref("extensions.getAddons.getWithPerformance.url", "https://services.addons.mozilla.org/%LOCALE%/thunderbird/api/%API_VERSION%/search/guid:%IDS%?src=thunderbird&appOS=%OS%&appVersion=%VERSION%&tMain=%TIME_MAIN%&tFirstPaint=%TIME_FIRST_PAINT%&tSessionRestored=%TIME_SESSION_RESTORED%");
pref("extensions.getAddons.search.browseURL", "https://addons.mozilla.org/%LOCALE%/thunderbird/search?q=%TERMS%");
pref("extensions.getAddons.search.url", "https://services.addons.mozilla.org/%LOCALE%/thunderbird/api/%API_VERSION%/search/%TERMS%/all/%MAX_RESULTS%/%OS%/%VERSION%/%COMPATIBILITY_MODE%?src=thunderbird");
pref("extensions.webservice.discoverURL", "https://services.addons.mozilla.org/%LOCALE%/thunderbird/discovery/pane/%VERSION%/%OS%");

// UI setup
pref("mail.main_menu.collapse_by_default", false);
pref("mail.tabs.autoHide", true);
pref("mail.tabs.drawInTitlebar", false);
pref("general.warnOnAboutConfig", false);
pref("mailnews.headers.showSender", true); //more details for security

// Misc.
pref("mail.provider.enabled", false); // no need for provisioning when no account given
pref("mail.cloud_files.enabled", false); // disabled provisioning, biased for a very short list of providers


