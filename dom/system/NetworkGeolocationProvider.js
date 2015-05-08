/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// See https://developers.google.com/maps/documentation/business/geolocation/

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
Components.utils.import("resource://gre/modules/Services.jsm");

const Ci = Components.interfaces;
const Cc = Components.classes;

let gLoggingEnabled = false;
let gTestingEnabled = false;
let gUseScanning = true;

function LOG(aMsg) {
  if (gLoggingEnabled)
  {
    aMsg = "*** WIFI GEO: " + aMsg + "\n";
    Cc["@mozilla.org/consoleservice;1"].getService(Ci.nsIConsoleService).logStringMessage(aMsg);
    dump(aMsg);
  }
}

function WifiGeoCoordsObject(lat, lon, acc, alt, altacc) {
  this.latitude = lat;
  this.longitude = lon;
  this.accuracy = acc;
  this.altitude = alt;
  this.altitudeAccuracy = altacc;
}

WifiGeoCoordsObject.prototype = {

  QueryInterface:  XPCOMUtils.generateQI([Ci.nsIDOMGeoPositionCoords]),

  classInfo: XPCOMUtils.generateCI({interfaces: [Ci.nsIDOMGeoPositionCoords],
                                    flags: Ci.nsIClassInfo.DOM_OBJECT,
                                    classDescription: "wifi geo position coords object"}),
};

function WifiGeoPositionObject(lat, lng, acc, cc, tz, zip, city, rc, region, country, isp, org, as) {
  this.coords = new WifiGeoCoordsObject(lat, lng, acc, 0, null);
  this.address = null;
  this.countrycode = cc;
  this.timezone = tz;
  this.zipcode = zip;
  this.postalcode = zip;
  this.city = city;
  this.regioncode = rc;
  this.region = region;
  this.country = country;
  this.isp = isp;
  this.org = org;
  this.as = as;
  this.timestamp = Date.now();
}

WifiGeoPositionObject.prototype = {

  QueryInterface:   XPCOMUtils.generateQI([Ci.nsIDOMGeoPosition]),

  // Class Info is required to be able to pass objects back into the DOM.
  classInfo: XPCOMUtils.generateCI({interfaces: [Ci.nsIDOMGeoPosition],
                                    flags: Ci.nsIClassInfo.DOM_OBJECT,
                                    classDescription: "wifi geo location position object"}),
};

function WifiGeoPositionProvider() {
  try {
    gLoggingEnabled = Services.prefs.getBoolPref("geo.wifi.logging.enabled");
  } catch (e) {}

  try {
    gTestingEnabled = Services.prefs.getBoolPref("geo.wifi.testing");
  } catch (e) {}

  try {
    gUseScanning = Services.prefs.getBoolPref("geo.wifi.scan");
  } catch (e) {}

  this.wifiService = null;
  this.timer = null;
  this.hasSeenWiFi = false;
  this.started = false;
}

WifiGeoPositionProvider.prototype = {
  classID:          Components.ID("{77DA64D3-7458-4920-9491-86CC9914F904}"),
  QueryInterface:   XPCOMUtils.generateQI([Ci.nsIGeolocationProvider,
                                           Ci.nsIWifiListener,
                                           Ci.nsITimerCallback]),
  startup:  function() {
    if (this.started)
      return;
    this.started = true;
    this.hasSeenWiFi = false;

    LOG("startup called.  testing mode is" + gTestingEnabled);

    // if we don't see anything in 5 seconds, kick off one IP geo lookup.
    // if we are testing, just hammer this callback so that we are more or less
    // always sending data.  It doesn't matter if we have an access point or not.
    // Pale Moon: hammering with 200ms is problematic for our geoIP provider. Testing
    // mode adjusted to 1000 ms. Adjusted the initial interval to 2 seconds
    // as well, which should be sufficient on modern, responsive geo hardware.
    this.timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    if (!gTestingEnabled)
      this.timer.initWithCallback(this, 2000, this.timer.TYPE_ONE_SHOT);
    else
      this.timer.initWithCallback(this, 1000, this.timer.TYPE_REPEATING_SLACK);
  },

  watch: function(c) {
    LOG("watch called");

    if (!this.wifiService && gUseScanning) {
      this.wifiService = Cc["@mozilla.org/wifi/monitor;1"].getService(Components.interfaces.nsIWifiMonitor);
      this.wifiService.startWatching(this);
    }
    if (this.hasSeenWiFi) {
      this.hasSeenWiFi = false;
      if (gUseScanning) {
        this.wifiService.stopWatching(this);
        this.wifiService.startWatching(this);
      } else {
        // For testing situations, ensure that we always trigger an update.
        this.timer.initWithCallback(this, 5000, this.timer.TYPE_ONE_SHOT);
      }
    }
  },

  shutdown: function() { 
    LOG("shutdown called");
    if(this.wifiService) {
      this.wifiService.stopWatching(this);
      this.wifiService = null;
    }
    if (this.timer != null) {
      this.timer.cancel();
      this.timer = null;
    }

    this.started = false;
  },

  setHighAccuracy: function(enable) {
  },

  onChange: function(accessPoints) {
    LOG("onChange called");
    this.hasSeenWiFi = true;

    let url = Services.urlFormatter.formatURLPref("geo.wifi.uri");

    function isPublic(ap) {
        let mask = "_nomap"
        let result = ap.ssid.indexOf(mask, ap.ssid.length - mask.length) == -1;
        if (result != -1) {
            LOG("Filtering out " + ap.ssid);
        }
        return result;
    };

    function sort(a, b) {
      return b.signal - a.signal;
    };

    function encode(ap) {
      return { 'macAddress': ap.mac, 'signalStrength': ap.signal }; 
    };

    var data;
    if (accessPoints) {
        data = JSON.stringify({wifiAccessPoints: accessPoints.filter(isPublic).sort(sort).map(encode)})
    }

    LOG("************************************* Sending request:\n" + url + "\n");

    // send our request to a wifi geolocation network provider:
    let xhr = Components.classes["@mozilla.org/xmlextras/xmlhttprequest;1"]
                        .createInstance(Ci.nsIXMLHttpRequest);

    // This is a background load
  
/* Google
    xhr.open("POST", url, true);
    xhr.setRequestHeader("Content-Type", "application/json; charset=UTF-8");
*/
    xhr.open("GET", url, true);
    xhr.responseType = "json";
    xhr.mozBackgroundRequest = true;
    xhr.channel.loadFlags = Ci.nsIChannel.LOAD_ANONYMOUS;
    xhr.onerror = function() {
        LOG("onerror: " + xhr);
    };

    xhr.onload = function() {  
        LOG("gls returned status: " + xhr.status + " --> " +  JSON.stringify(xhr.response));
        if (xhr.channel instanceof Ci.nsIHttpChannel && xhr.status != 200) {
            return;
        }
/* Google
        if (!xhr.response || !xhr.response.location) {
            return;
        }

        let newLocation = new WifiGeoPositionObject(xhr.response.location.lat,
                                                    xhr.response.location.lng,
                                                    xhr.response.accuracy);
*/

//IP-API
        if (!xhr.response || !xhr.response.status || xhr.response.status == 'fail') {
            return;
        }

        let newLocation = new WifiGeoPositionObject(xhr.response.lat,
                                                    xhr.response.lon,
                                                    null, //accuracy not provided
                                                    xhr.response.countryCode,
                                                    xhr.response.timezone,
                                                    xhr.response.zip,
                                                    xhr.response.city,
                                                    xhr.response.region,
                                                    xhr.response.regionName,
                                                    xhr.response.country,
                                                    xhr.response.isp,
                                                    xhr.response.org,
                                                    xhr.response.as);
        
        Cc["@mozilla.org/geolocation/service;1"].getService(Ci.nsIGeolocationUpdate)
            .update(newLocation);
    };

    LOG("************************************* ------>>>> sending " + data);
    xhr.send(data);
  },

  onError: function (code) {
    LOG("wifi error: " + code);
  },

  notify: function (timer) {
    if (gTestingEnabled || !gUseScanning) {
      // if we are testing, timer is repeating
      this.onChange(null);
    }
    else {
      if (!this.hasSeenWiFi)
        this.onChange(null);
      this.timer = null;
    }
  },
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([WifiGeoPositionProvider]);
