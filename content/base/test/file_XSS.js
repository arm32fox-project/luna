netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
const Cu = Components.utils;
const Ci = Components.interfaces;
const Cc = Components.classes;
const env = Cc["@mozilla.org/process/environment;1"].
  getService(Ci.nsIEnvironment);
// we use this for clarity
var GET = "GET", POST = "POST";
var ATTACK = "ATTACK", ATTACK_BLOCK = "ATTACK_BLOCK", SAFE = "SAFE";
var TODO = "TODO", DONE = "DONE";
var NONE = "none";
// format:
// method, description, server context, content, header, encoding
// (form only), attack, todo

var TESTS = [
  [GET, "partial injection inline", "inside",
   'abc\'; parent.postMessage("xss", "*");//', NONE, NONE, ATTACK, TODO],

  [GET, "whole script injection", "outside",
   '<script>parent.postMessage("xss", "*");</script>', NONE, NONE, ATTACK, DONE],
  [GET, "whole script with HTML quirk", "outside",
   '<IMG """><SCRIPT>parent.postMessage("xss", "*");</SCRIPT>">',
   NONE, NONE, ATTACK, DONE],

  [GET, "external script w/ quirk", "outside",
   '<SCRIPT/XSS SRC="http://example.org/tests/content/base/test/file_XSS_title.js"></SCRIPT>', NONE, NONE, ATTACK, DONE],
  [GET, "script tag w/ quirk", "outside",
   '<<SCRIPT>parent.postMessage("xss", "*");//<</SCRIPT>', NONE, NONE, ATTACK, DONE],
  //  '<' does not work on windows
  // ["external_outside5_bad", "outside_script",
  //  '<SCRIPT SRC=http://example.org/tests/content/base/test/file_XSS_title.js?<B>'],
  [GET, "external w/ quirk", "outside_script",
   '<SCRIPT SRC=//example.org/tests/content/base/test/file_XSS_title.js>',
   NONE, NONE, ATTACK, DONE],
  [GET, "whole script w/ eval w/o quotes", "outside",
   '<SCRIPT>eval(/parent.postMessage("xss", /\\*/.source[1])/.source)</SCRIPT>',
   NONE, NONE, ATTACK, DONE],
  [GET, "iframe src w/ jsurl", "outside",
   '<IFRAME ID="frm" SRC="javascript:void(parent.parent.postMessage(\'xss\', \'*\'));"></IFRAME>', NONE, NONE, ATTACK, DONE],
  [GET, "svg object", "outside",
   '<EMBED SRC="data:image/svg+xml;base64,PHN2ZyB4bWxuczpzdmc9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIiB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHhtbG5zOnhsaW5rPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5L3hsaW5rIiB2ZXJzaW9uPSIxLjAiIHg9IjAiIHk9IjAiIHdpZHRoPSIxOTQiIGhlaWdodD0iMjAwIiBpZD0ieHNzIj48c2NyaXB0IHR5cGU9InRleHQvZWNtYXNjcmlwdCI+cGFyZW50LnBhcmVudC5wb3N0TWVzc2FnZSgieHNzIiwgIioiKTs8L3NjcmlwdD48L3N2Zz4=" type="image/svg+xml" AllowScriptAccess="always"></EMBED>', NONE, NONE, ATTACK, DONE],
  [GET, "external w/ fake jpg", "outside",
   '<SCRIPT SRC="http://example.org/tests/content/base/test/file_XSS_title.jpg"></SCRIPT>', NONE, NONE, ATTACK, DONE],
  [GET, "external script w/ quirk", "outside",
   '<SCRIPT a=">" SRC="http://example.org/tests/content/base/test/file_XSS_title.js"></SCRIPT>', NONE, NONE, ATTACK, DONE],
  [GET, "whole script w/ doc.write", "outside",
   '<SCRIPT>document.write("<SCRI");</SCRIPT>PT SRC="http://example.org/tests/content/base/test/file_XSS_title.js"></SCRIPT>', NONE, NONE, ATTACK, DONE],
  [GET, "inline_outside_esc_bad", "outside_esc",
   '<script>parent.postMessage("xss", "*");</script>', NONE, NONE, ATTACK, DONE],
  [GET, "handler_plain_bad", "handler_plain",
   'parent.postMessage("xss", "*");', NONE, NONE, ATTACK, DONE],
  [GET, "handler_inside_bad", "handler_inside",
   '= 3; parent.postMessage("xss", "*"); //', NONE, NONE, ATTACK, TODO],
  [GET, "external_outside_bad", "outside",
'<script src="http://example.org/tests/content/base/test/file_XSS_title.js">' +
   '</script>', NONE, NONE, ATTACK, DONE],
  [GET, "benign external script", "external_path",
   "file_XSS_title.js", NONE, NONE, SAFE, DONE],
  // p larger than s?
  [GET, "external w/o http", "external_domain",
   "example.org/tests/content/base/test/file_XSS_title.js",
   NONE, NONE, ATTACK, DONE],
  [GET, "jsurl in frame", "html_nobody",
   '<FRAMESET><FRAME SRC="javascript:void(parent.parent.postMessage(\'xss\', \'*\'));"></FRAMESET>', NONE, NONE, ATTACK, DONE],
  [GET, "jsurl with html entities/utf8", "outside",
   '<IFRAME SRC=&#106&#97&#118&#97&#115&#99&#114&#105&#112&#116&#58&#112&#97&#114&#101&#110&#116&#46&#112&#97&#114&#101&#110&#116&#46&#112&#111&#115&#116&#77&#101&#115&#115&#97&#103&#101&#40&#34&#120&#115&#115&#34&#44&#32&#34&#42&#34&#41></iframe>',
   NONE, NONE, ATTACK, DONE],
  [GET, "jsurl with html entities 2", "outside",
   '<IFRAME SRC=&#0000106&#0000097&#0000118&#0000097&#0000115&#0000099&#0000114&#0000105&#0000112&#0000116&#0000058&#0000112&#0000097&#0000114&#0000101&#0000110&#0000116&#0000046&#0000112&#0000097&#0000114&#0000101&#0000110&#0000116&#0000046&#0000112&#0000111&#0000115&#0000116&#0000077&#0000101&#0000115&#0000115&#0000097&#0000103&#0000101&#0000040&#0000034&#0000120&#0000115&#0000115&#0000034&#0000044&#0000032&#0000034&#0000042&#0000034&#0000041></iframe>', NONE, NONE, ATTACK, DONE],
  // TODO: create a flash file?
  //["inline_outside10_bad", "outside",
  // '<EMBED SRC="http://ha.ckers.org/xss.swf" AllowScriptAccess="always"></EMBED>'],
  [GET, "inside js url", "js_url",
   "javascript:void(parent.parent.postMessage(\'xss\', \'*\'));",
   NONE, NONE, ATTACK, DONE],
  [GET, "body onload event", "html_nobody",
   '<body onload="parent.postMessage(\'xss\', \'*\');">Hello</body>',
   NONE, NONE, ATTACK, DONE],
  // does not seem to be a valid vector for firefox
  // ["meta1_bad", "meta",
  // '<META HTTP-EQUIV="refresh" CONTENT="0;url=javascript:void(parent.parent.postMessage(\'xss\', \'*\'));">'],
  // document domain has not been set
  // ["meta2_bad", "meta",
  // '<META HTTP-EQUIV="refresh" CONTENT="1;url=data:text/html;base64,c2V0VGltZW91dCgiZG9jdW1lbnQudGl0bGUgPSAnWWVzJyIsIDUwMCk7">'],
  // does not work. maybe server sends content type?
  // ["utf7_bad", "meta",
  // '<META HTTP-EQUIV="CONTENT-TYPE" CONTENT="text/html; charset=UTF-7"> </HEAD><body>+ADw-SCRIPT+AD4-alert(\'XSS\');+ADw-/SCRIPT+AD4-</body><!--'],
  [GET, "eval", "eval",
   'parent.postMessage("xss", "*");', NONE, NONE, ATTACK, DONE],
  [GET, "eval after ready", "eval_ready",
   'parent.postMessage("xss", "*");', NONE, NONE, ATTACK, DONE],
  [GET, 'js function', 'function',
   'parent.postMessage("xss", "*");', NONE, NONE, ATTACK, DONE],
  [GET, 'setTimeout', 'timeout',
   'parent.postMessage("xss", "*");', NONE, NONE, ATTACK, DONE],

  [POST, "whole script injection", "outside",
   '<script>parent.postMessage("xss", "*");</script>', NONE,
   'application/x-www-form-urlencoded', ATTACK, DONE],
  [POST, "whole scr w/ mime post", "outside",
   '<script>parent.postMessage("xss", "*");</script>', NONE,
   'multipart/form-data', ATTACK, DONE],
  [POST, "partial inj w/POST", "inside",
   'abc\'; parent.postMessage("xss", "*");//', NONE,
   "application/x-www-form-urlencoded", ATTACK, TODO],

  [GET, "blank hdr", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "", NONE, ATTACK, DONE],
  [GET, "hdr enabled", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "1", NONE, ATTACK, DONE],
  [GET, "hdr enabled w/space", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "  1", NONE, ATTACK, DONE],
  [GET, "wrong hdr", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "pippo", NONE, ATTACK, DONE],
  [GET, "hdr disable", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "0", NONE, SAFE, DONE],
  [GET, "hdr disable w/spaces", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "  0", NONE, SAFE, DONE],
  [GET, "hdr disable w/junk", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "0dfdf", NONE, SAFE, DONE],
  [GET, "enable w/block mode", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "1; mode=block",
   NONE, ATTACK_BLOCK, DONE],
  [GET, "enable w/block mode 2", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "1;mode =   block  ",
   NONE, ATTACK_BLOCK, DONE],
  [GET, "enable w/block mode 3", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "1 ; mODe= bLock",
   NONE, ATTACK_BLOCK, DONE],
  [GET, "wrong block mode", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "1 ; mODe= bLock;",
   NONE, ATTACK, DONE],
  [GET, "hdr wrong", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "0; disabled;",
   NONE, SAFE, DONE],
  [GET, "junk", "outside",
   '<script>parent.postMessage("xss", "*");</script>', "a0",
   NONE, ATTACK, DONE],

];

function setXss (state){
  netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
  var prefService = Components.classes["@mozilla.org/preferences-service;1"]
    .getService(Components.interfaces.nsIPrefService);
  var cspBranch = prefService.getBranch("security.xssfilter.");
  cspBranch.setBoolPref("enabled", state);
}

// caution, text is unescaped, do not output html chars
function say(text) {
  jQuery('body').append(text + '<br>\n');
}

function sayLink(url, link, msg) {
  say("<a target='_blank' href='" + url.replace(/'/g, "\'") + "'>" + link +
      "</a> " + msg);
}

function getObserverService() {
  netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
  return Cc["@mozilla.org/observer-service;1"].
    getService(Ci.nsIObserverService);
}

function parseXSSArray(xpcomarray) {
  if (xpcomarray.length == 0)
    return null;
  return {policy: policy, content: content, url: url, blockMode: blockMode};
}

const Observer = {

  observe: function (aSubject, aTopic, aData)
  {
    netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
    var t = Tester.getCase();
    aSubject.QueryInterface(Ci.nsIArray);
    var policy = aSubject.queryElementAt(0, Ci.nsISupportsCString).data;
    var content = aSubject.queryElementAt(1, Ci.nsISupportsCString).data;
    var url = aSubject.queryElementAt(2, Ci.nsISupportsCString).data;
    var blockMode = aSubject.queryElementAt(3, Ci.nsISupportsPRBool).data;
    var shouldBlock = t.attack == ATTACK_BLOCK;
    var shouldAttack = t.attack == ATTACK || t.attack == ATTACK_BLOCK;
    if (t.todo == DONE) {
      ok(shouldAttack && (blockMode == shouldBlock), t.method + ": " + t.description +
	 " (" + t.attack + ")");
    } else {
      todo(shouldAttack && (blockMode == shouldBlock), t.method + ": " + t.description +
	   " (" + t.attack + ")");
    }
    setTimeout("Tester.next();", 10);
  }
};

var Tester = {

  current: 0,
  tests: TESTS,
  tID: 0,

  test: function() {
    netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
    var t = this.getCase();
    // setup listeners for postmessage and notification topic
    window.addEventListener("message", this.receiveMessage, false);
    getObserverService().addObserver(Observer, "xss-on-violate-policy", false);
    this.tID = window.setTimeout("Tester.timeout()", 5000);
    if (t.method == GET) {
      sayLink('file_XSS.sjs?hdr=' + escape(t.header) + '&type=' + t.context +
	      '&content=' + escape(t.content), t.description, t.attack);
      jQuery('body').append(jQuery("<iframe id='iframe_" + t.id +
				   "' src='file_XSS.sjs?" +
				   "hdr=" + escape(t.header) +
				   "&type=" + escape(t.context) +
				   "&content=" + escape(t.content) +
				   "'></iframe>"));
    } else {
      say("Test: " + t.description);
      jQuery('body').append(
	jQuery("<iframe id='iframe_" + t.id + "' name='iframe_" + t.id +
	       "' src='about:blank'></iframe>"));
      jQuery('body').append(jQuery(
	"<form method='POST' action='file_XSS.sjs' target='iframe_" + t.id +
	  "' id='form_" + t.id + "' enctype='" + t.encoding + "'>" +
	  "<input type='text' name='type' value='" + t.context + "' />" +
	  "<input type='text' name='hdr' value='" + t.header + "' />" +
	  "<input type='text' name='content' id='content_" + t.id + "' />" +

	  "<input type='submit' name='boh' value='submit test' />" +
	  "</form>"));
      jQuery('#content_' + t.id).val(t.content);
      jQuery("#form_" + t.id).submit();
    }
  },

  next: function () {
    netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
    var t = this.getCase();
    // remove listeners (and forms!)
    if (t.method == POST) {
      jQuery("#form_" + t.id).remove();
    }
    window.clearTimeout(this.tID);
    window.removeEventListener("message", this.receiveMessage);
    getObserverService().removeObserver(Observer, "xss-on-violate-policy");
    jQuery("#iframe_" + t.id).remove();
    this.current += 1;
    if (this.current >= this.tests.length) {
      say("Tests done.");
      SimpleTest.finish();
      return;
    }
    setTimeout("Tester.test()", 10);
  },

  receiveMessage: function(event)
  {
    var t = Tester.getCase();
    if (t.todo == DONE) {
      ok(t.attack == SAFE, t.method + ": " + t.description +
	 " (" + t.attack + ")");
    } else {
      todo(t.attack == SAFE, t.method + ": "+ t.description +
	   " (" + t.attack + ")");
    }
    setTimeout("Tester.next();", 10);
  },

  timeout: function() {
    var t = this.getCase();
    say("Timeout for " + t.description);
    if (t.todo == DONE) {
      ok(false, "Timeout for " + t.description + " (" + t.attack + ")");
    } else {
      todo(false, "Timeout for " + t.description + " (" + t.attack + ")");
    }
    setTimeout("Tester.next();", 10);
  },

  getCase: function() {
    var t = this.tests[this.current];
    return {method: t[0], description: t[1], context: t[2], content: t[3],
	    header: t[4], encoding: t[5], attack: t[6], todo: t[7],
	    id: this.current};
  },

  printLinks: function() {
    say('Only GET tests are printed.');
    for (var i = 0; i < this.tests.length; i++) {
      this.current = i;
      var t = this.getCase();
      if (t.method == GET) {
	sayLink('file_XSS.sjs?hdr=' + escape(t.header) + '&type=' + t.context +
		'&content=' + escape(t.content), t.description, t.attack);
      }
    }
  },

};

jQuery(document).ready(
    function () {
      netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
      if (env.exists("MANUALTEST") == false) {
	setTimeout("Tester.test()", 10);
	SimpleTest.waitForExplicitFinish();
      } else {
	// just print the links to open manually (only GET)
	Tester.printLinks();
	SimpleTest.finish();
      }
    }
);


// TODO: SimpleTest can't figure out the URL for the page.