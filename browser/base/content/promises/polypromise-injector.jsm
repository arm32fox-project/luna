/* coded by Ketmar // Invisible Vector (psyc://ketmar.no-ip.org/~Ketmar)
 * Understanding is not required. Only obedience.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, 
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
////////////////////////////////////////////////////////////////////////////////
let EXPORTED_SYMBOLS = [];

const {utils:Cu, classes:Cc, interfaces:Ci, results:Cr} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");


const debugLog = false;


////////////////////////////////////////////////////////////////////////////////
function loadTextContentsFromUrl (url, encoding) {
  if (typeof(encoding) !== "string") encoding = "UTF-8"; else encoding = encoding||"UTF-8";

  function toUnicode (text) {
    if (typeof(text) === "string") {
      if (text.length == 0) return "";
    } else if (text instanceof ArrayBuffer) {
      if (text.byteLength == 0) return "";
      text = new Uint8Array(text);
      return new TextDecoder(encoding).decode(text);
    } else if ((text instanceof Uint8Array) || (text instanceof Int8Array)) {
      if (text.length == 0) return "";
      return new TextDecoder(encoding).decode(text);
    } else {
      return "";
    }
    let converter = Cc["@mozilla.org/intl/scriptableunicodeconverter"].createInstance(Ci.nsIScriptableUnicodeConverter);
    converter.charset = encoding;
    return converter.ConvertToUnicode(text);
  }

  // download from URL
  let req = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"].createInstance();
  req.mozBackgroundRequest = true;
  req.open("GET", url, false);
  req.timeout = 30*1000;
  let cirq = Ci.nsIRequest;
  req.channel.loadFlags |= cirq.INHIBIT_CACHING|cirq.INHIBIT_PERSISTENT_CACHING|cirq.LOAD_BYPASS_CACHE|cirq.LOAD_ANONYMOUS;
  req.responseType = "arraybuffer";
  let error = false;
  req.onerror = function () { error = true; }; // just in case
  req.ontimeout = function () { error = true; }; // just in case
  req.send(null);
  if (!error) error = (req.status != 0 && Math.floor(req.status/100) != 2);
  if (error) throw new Error("can't load URI contents: status="+req.status+"; error="+error);
  return toUnicode(req.response, encoding);
}


////////////////////////////////////////////////////////////////////////////////
// load promise library
let ppfsrc = loadTextContentsFromUrl("chrome://browser/content/promises/data/es6-promise.code.js");
//Cu.reportError("ppfsrc.length="+ppfsrc.length);


////////////////////////////////////////////////////////////////////////////////
function createSandbox (domWin) {
  // create "unwrapped" sandbox
  let sandbox = Cu.Sandbox(domWin, {
    sandboxName: "unwrapped-polypromise",
    sameZoneAs: domWin, // help GC a little
    sandboxPrototype: domWin,
    wantXrays: false,
  });

  Object.defineProperty(sandbox, "GM_info", {
    get: function () "42",
  });

  // nuke sandbox when this window unloaded (this helps GC)
  domWin.addEventListener("unload", function () {
    if (sandbox) {
      Cu.nukeSandbox(sandbox);
      sandbox = null;
    }
  }, true);

  //Cu.reportError("SBOX="+sandbox);
  return sandbox;
}


////////////////////////////////////////////////////////////////////////////////
function ContentObserver () {}


ContentObserver.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver]),

  observe: function (aSubject, aTopic, aData) {
    if (aTopic === "document-element-inserted") {
      // get `document` object
      let doc = aSubject;
      if (!doc) return;
      // don't tamper with XUL documents
      if (!(doc instanceof Ci.nsIDOMHTMLDocument)) return //not html document, so its likely an XUL document
      // get `window` object
      let win = doc.defaultView;
      if (!win) return;
      // don't tamper with chrome windows
      if (win instanceof Ci.nsIDOMChromeWindow) return;
      // just in case, this should never be `true`
      if (!(win instanceof Ci.nsIDOMWindow)) {
        if (debugLog) Cu.reportError("*** WTF: ["+(doc.documentURI ? doc.documentURI : "<unknown>")+"]");
        return;
      }
      // no need to check for URI scheme here:
      // we want this API to be universally available, so
      // inject it in *any* normal DOM window, and be happy
      if (debugLog) Cu.reportError("ppfsrc.length="+ppfsrc.length+"; ["+(doc.documentURI ? doc.documentURI : "<unknown>")+"]");
      //
      let w = (win.wrappedJSObject||win);
      if (debugLog) w.conlogx = (function () {
        let s = "";
        for (let idx = 0; idx < arguments.length; ++idx) s += arguments[idx];
        if (s) Cu.reportError("LOG: "+s);
      }).bind(win);
      // define Promise lazy loader (we don't need to inject it each time; load it on demand)
      let prmObject = null;
      let prmLoaded = false;
      let unloadListenerAdded = false;
      Object.defineProperty(w, "Promise", {
        get: (function () {
          if (debugLog) Cu.reportError("***["+doc.documentURI+"] getter");
          if (prmLoaded) return prmObject;
          prmLoaded = true;
          if (debugLog) Cu.reportError("***["+doc.documentURI+"] calculating...");
          let sandbox = createSandbox(win);
          Cu.evalInSandbox(ppfsrc, sandbox, "ECMAv5", "chrome://browser/content/promises/pm-polypromise.js", 1);
          if (debugLog) Cu.reportError("***["+doc.documentURI+"] calculated: "+typeof(prmObject));
          return prmObject;
        }).bind(win),
        set: (function (val) {
          if (debugLog) Cu.reportError("***["+doc.documentURI+"] setting to: "+typeof(val));
          prmLoaded = true;
          prmObject = val;
          if (!unloadListenerAdded) {
            unloadListenerAdded = true;
            win.addEventListener("unload", function () {
              prmObject = null;
              prmLoaded = false;
            }, true);
          }
          return val;
        }).bind(win),
        configurable: true,
        enumerable: true,
      });
      if (debugLog) Cu.reportError("***["+doc.documentURI+"] API set");
    }
  }
};


////////////////////////////////////////////////////////////////////////////////
let contentObserver = new ContentObserver();


////////////////////////////////////////////////////////////////////////////////
Services.obs.addObserver(contentObserver, "document-element-inserted", false);


////////////////////////////////////////////////////////////////////////////////
/*
win.addEventListener("unload", function () {
  Services.obs.removeObserver(contentObserver, "document-element-inserted");
}, false);
*/
