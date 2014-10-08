// Default start page
pref("mailnews.start_page.url","about:blank");

// first launch welcome page
pref("mailnews.start_page.welcome_url","about:blank");

// start page override to load after an update
pref("mailnews.start_page.override_url","about:blank");

// Interval: Time between checks for a new version (in seconds)
// nightly=8 hours, official=24 hours
pref("app.update.interval", 28800);

// The time interval between the downloading of mar file chunks in the
// background (in seconds)
pref("app.update.download.backgroundInterval", 60);

// Vendor and app URLS
pref("app.vendorURL", "http://www.fossamail.org/");
pref("mail.cloud_files.inserted_urls.footer.link", "http://www.fossamail.org/");

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


