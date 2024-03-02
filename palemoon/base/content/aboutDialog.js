// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Services = object with smart getters for common XPCOM services
Components.utils.import("resource://gre/modules/Services.jsm");

function init(aEvent)
{
  if (aEvent.target != document) {
    return;
  }

  try {
    var distroId = Services.prefs.getCharPref("distribution.id");
    if (distroId) {
      var distroVersion = Services.prefs.getCharPref("distribution.version");

      var distroIdField = document.getElementById("distributionId");
      distroIdField.value = distroId + " - " + distroVersion;
      distroIdField.style.display = "block";

      try {
        // This is in its own try catch due to bug 895473 and bug 900925.
        var distroAbout = Services.prefs.getComplexValue("distribution.about",
                                                         Components.interfaces.nsISupportsString);
        var distroField = document.getElementById("distribution");
        distroField.value = distroAbout;
        distroField.style.display = "block";
      } catch (ex) {
        // Pref is unset
        Components.utils.reportError(ex);
      }
    }
  } catch(e) {
    // Pref is unset
  }

  let versionField = document.getElementById("aboutVersion");
  
#ifdef MOZ_WIDGET_GTK
  // If on Linux-GTK, append the toolkit "(GTK2)" or "(GTK3)"
  let toolkit = Components.classes["@mozilla.org/xre/app-info;1"]
                .getService(Components.interfaces.nsIXULRuntime).widgetToolkit.toUpperCase();
  versionField.textContent += ` (${toolkit})`;
#endif

  // Include the build date if this is an "a#" or "b#" build
  let version = Services.appinfo.version;
  if (/[ab]\d+$/.test(version)) {
    let buildID = Services.appinfo.appBuildID;
    let buildDate = buildID.slice(0,4) + "-" + buildID.slice(4,6) + "-" + buildID.slice(6,8);
    versionField.textContent += " (" + buildDate + ")";
  }

#ifdef XP_MACOSX
  // it may not be sized at this point, and we need its width to calculate its position
  window.sizeToContent();
  window.moveTo((screen.availWidth / 2) - (window.outerWidth / 2), screen.availHeight / 5);
#endif

// get release notes URL from prefs
  var formatter = Components.classes["@mozilla.org/toolkit/URLFormatterService;1"]
                            .getService(Components.interfaces.nsIURLFormatter);
  var releaseNotesURL = formatter.formatURLPref("app.releaseNotesURL");
  if (releaseNotesURL != "about:blank") {
    var relnotes = document.getElementById("releaseNotesURL");
    relnotes.setAttribute("href", releaseNotesURL);
  }

  { // Halloween check, scoped for let
    let dateObj = new Date();
    let month = dateObj.getUTCMonth() + 1; //months from 1-12
    let day = dateObj.getUTCDate();
    
    if ((month == 10 && day > 13) ||
        (month == 11 && day < 5)) {
      // We're in the second half of Oct or start of Nov
      try {
        document.styleSheets[0].addRule('#aboutPMDialogContainer::before','animation: 6s fadeIn;');
      } catch (e) {
        Components.utils.reportError(e);
      }
    }
  }
}
