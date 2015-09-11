"use strict";
(function() {

var PMprefs = Components.classes["@mozilla.org/preferences-service;1"]
                .getService(Components.interfaces.nsIPrefBranch);
if (!PMprefs.getBoolPref('browser.ruby.enabled'))
  return;


function Preferences() {
	this.register();
	this.load();
}
Preferences.prototype = {
	rubyTextSize: '0.55em',
	spaceRubyText: true,
	processInsertedContent: true,
	rubyLineHeight: '0.92em',
	adjustLineHeight: true,
	_branch: null,
	_SSService: null,
	_IOService: null,
	_currentSheet: null,
	load: function() {
		var branch = this._branch;
		try {
			this.rubyTextSize = branch.getCharPref('rubyTextSize');
			this.spaceRubyText = branch.getBoolPref('spaceRubyText');
			this.processInsertedContent = branch.getBoolPref('processInsertedContent');
			this.rubyLineHeight = branch.getCharPref('rubyLineHeight');
			this.adjustLineHeight = branch.getBoolPref('adjustLineHeight');
		} catch(e) {
			branch.setCharPref('rubyTextSize', this.rubyTextSize);
			branch.setBoolPref('spaceRubyText', this.spaceRubyText);
			branch.setBoolPref('processInsertedContent', this.processInsertedContent);
			branch.setCharPref('rubyLineHeight', this.rubyLineHeight);
			branch.setBoolPref('adjustLineHeight', this.adjustLineHeight);
		}
		this.setUserPreferenceStyleSheet();
	},
	register: function() {
	   var prefs = Components.classes["@mozilla.org/preferences-service;1"].getService(Components.interfaces.nsIPrefService);
		this._branch = prefs.getBranch("browser.ruby.");
		this._branch.QueryInterface(Components.interfaces.nsIPrefBranch2);
		this._branch.addObserver("", this, false);
		this._SSService = Components.classes['@mozilla.org/content/style-sheet-service;1'].getService(Components.interfaces.nsIStyleSheetService);
		this._IOService = Components.classes['@mozilla.org/network/io-service;1'].getService(Components.interfaces.nsIIOService);
	},
	observe: function(subject, topic, data) {
		if (topic != "nsPref:changed") {
			return null;
		}
		switch (data) {
			case 'rubyTextSize':
				this.rubyTextSize = this._branch.getCharPref('rubyTextSize');
				this.setUserPreferenceStyleSheet();
				break;
			case 'spaceRubyText':
				this.spaceRubyText = this._branch.getBoolPref('spaceRubyText');
				break;
			case 'processInsertedContent':
				this.processInsertedContent = this._branch.getBoolPref('processInsertedContent');
				break;
			case 'rubyLineHeight':
				this.rubyLineHeight = this._branch.getCharPref('rubyLineHeight');
				this.setUserPreferenceStyleSheet();
				break;
			case 'adjustLineHeight':
				this.adjustLineHeight = this._branch.getBoolPref('adjustLineHeight');
				this.setUserPreferenceStyleSheet();
				break;
		}
	},
	setUserPreferenceStyleSheet: function() {
		var sss = this._SSService,
			css = 'rtc,ruby>rt{font-size:' + this.rubyTextSize + '!important;-moz-transform:translateY(-' + this.rubyLineHeight + ')!important}';
		if (this.adjustLineHeight) {
			css += 'ruby{margin-top:' + this.rubyTextSize + '!important}';
		} else {
			css += 'ruby{margin-top:0px!important}';
		}
		var	uri = this._IOService.newURI('data:text/css;charset=utf-8,' + encodeURIComponent(css), null, null),
			currentSheet = this._currentSheet;
		if (currentSheet && sss.sheetRegistered(currentSheet, sss.USER_SHEET)) {
			sss.unregisterSheet(currentSheet, sss.USER_SHEET);
		}
		if (!sss.sheetRegistered(uri, sss.USER_SHEET)) {
			sss.loadAndRegisterSheet(uri, sss.USER_SHEET);
		}
		this._currentSheet = uri;
	}
};

function RubyData(ruby) {
	this.ruby = ruby;
}
RubyData.prototype = {
	ruby: null,
	rts: null,
	rbs: null,
	rtWidths: null,
	rbWidths: null,		
	calculateWidths: function() {
		var ruby = this.ruby,
			rts = ruby.querySelector('rtc').querySelectorAll('rt'),
			rbs = ruby.querySelectorAll('rb'),
			rbCount = rbs.length,
			rtWidths = new Array(rbCount),
			rbWidths = new Array(rbCount),
			i = 0;
		for (; i<rbCount; i++) {
			var rt = rts[i];
			if (rt) {
				rtWidths[i] = rt.clientWidth;
				if (rt.hasAttribute('rbspan')) {
					i += rt.getAttribute('rbspan') - 1;
				}
			}
		}
		for (i=rbCount; i--; ) {
			rbWidths[i] = rbs[i].clientWidth;
		}
		this.rtWidths = rtWidths;
		this.rbWidths = rbWidths;
		this.rts = rts;
		this.rbs = rbs;
	}
};	

function Processor(doc) {
	var me = this,
		arbitrator = new Worker("chrome://htmlruby/content/scripts/workerArbitrator.js");
	this.document = doc;
	this._arbitrator = arbitrator;
	this._queue = [];
	this._onstop = function() {
		me._queue = [];
		s.hideProgress();
		s.stop.removeEventListener('click', me._onstop, true);
	};
	this._oninsert = function(e) {
		me._arbitrator.postMessage('flush');
	};
	arbitrator.onmessage = function(e) {
		if (e.data == 'space') {
			me.space();
		} else {
			me.flush();
		}
	};
	doc.defaultView.addEventListener('pagehide', me._onstop, true);
	this.onDOMContentLoaded();
	if (p.processInsertedContent) {
		this.resume();
	}
}
Processor.prototype = {
	document: null,
	_onstop: null,
	_oninsert: null,
	_arbitrator: null,
	_queue: null,
	pause: function() {
		this.document.body.removeEventListener('DOMNodeInserted', this._oninsert, true);
	},
	resume: function() {
		this.document.body.addEventListener('DOMNodeInserted', this._oninsert, true);
	},
	onDOMContentLoaded: function() {
		var doc = this.document,
			body = doc.body;
		if (!body.querySelector('ruby')) {
			return null;
		}
		if (body.querySelector('ruby ruby') || body.querySelector('rt rp') || body.querySelector('rp rt')) {
			this.clean();
		}
		var rubies = body.querySelectorAll('ruby'),
			count = rubies.length;
		if (!count) {
			return null;
		}
		var queue, fragment;
		if (count > 50) {
			var range = doc.createRange();
			range.selectNodeContents(body);
			fragment = range.extractContents();
		}
		queue = this.process(rubies);
		if (fragment) {
			body.appendChild(fragment);
		}
		if (p.spaceRubyText) {
			this._queue = queue;
			s.stop.addEventListener('click', this._onstop, true);
			s.startProgress(count);
			this._arbitrator.postMessage('space');
		}
	},
	flush: function() {
		var doc = this.document,
			body = doc.body,
			rubies = body.querySelectorAll('ruby:not([htmlruby_processed])'),
			count = rubies.length,
			spaceRubyText = p.spaceRubyText,
			queue, range, parent, fragment;
		if (!count) {
			return null;
		}
		if (!spaceRubyText && rubies[0].querySelector('rb')) {
			return null;
		}
		this.pause();
		if (count > 50) {
			range = doc.createRange();
			range.setStartBefore(rubies[0]);
			range.setEndAfter(rubies[count-1]);
			parent = range.commonAncestorContainer;
			range.selectNodeContents(parent);
			fragment = range.extractContents();
		}
		queue = this.process(rubies);
		if (fragment) {
			parent.appendChild(fragment);
		}
		if (spaceRubyText) {
			queue = this._queue.concat(queue);
			s.stop.addEventListener('click', this._onstop, true);
			s.startProgress(queue.length);
			this._queue = queue;
			this._arbitrator.postMessage('space');
		}
		this.resume();
	},
	process: function(block) {
		var doc = this.document,
			count = block.length,
			queue = null,
			spaceRubyText = p.spaceRubyText;
		if (spaceRubyText) {
			queue = new Array(count);
		}
		for (var i=count; i--; ) {
			var ruby = block[i];
			if (!ruby) {
				continue;
			}
			if (ruby.hasAttribute('htmlruby_processed')) {
				continue;
			}
			ruby.setAttribute('htmlruby_processed', 'processed', null);
			if (ruby.querySelector('rbc')) {
				var rtcs = ruby.querySelectorAll('rtc'); 
				if (rtcs.length == 2) {
					ruby.setAttribute('title', rtcs[1].textContent.trim());
				}
				if (spaceRubyText) {
					queue[i] = new RubyData(ruby);
				}
			} else {
				var rb = ruby.querySelector('rb'),
					rtc = doc.createElement('rtc'),
					rbc = doc.createElement('rbc'),
					rt;
				if (rb) {
					if (!spaceRubyText) {
						continue;
					}
					rt = ruby.querySelector('rt');
					if (rt) {
						rtc.appendChild(rt);
					}
					rbc.appendChild(rb);
				} else {
					var rts = ruby.querySelectorAll('rt'),
						rtCount = rts.length;
					if (!rtCount) {
						continue;
					}
					ruby.normalize();
					for (var j=rtCount; j--; ) {
						rt = rts[j];
						rb = doc.createElement('rb');
						var	range = doc.createRange();
						if (j > 0) {
							range.setStartAfter(rts[j-1]);
						} else if (ruby.firstChild != rt) {
							range.setStart(ruby.firstChild, 0);
						} else {
							continue;
						}
						range.setEndBefore(rt);
						rb.appendChild(range.extractContents());
						rbc.insertBefore(rb, rbc.firstChild);
						rtc.insertBefore(rt, rtc.firstChild);
					}
				}
				ruby.appendChild(rbc);
				ruby.appendChild(rtc);
				if (spaceRubyText) {
					queue[i] = new RubyData(ruby);
				}
			}
		}
		return queue;
	},
	space: function() {
		function apply(elem, diff, maxWidth) {
			var text = elem.textContent.trim(),
				len = text.length,
				wordCount, perChar;
			if (!len) {
				return null;
			}
			if (text.charCodeAt(0) <= 128) {
				wordCount = text.split(' ').length;
				if (wordCount > 1) {
					elem.style.cssText += ';max-width:' + maxWidth + 'px;word-spacing:' + Math.round(diff/wordCount) + 'px;';
				}
			} else {
				perChar = diff / len;
				if (perChar) {
					elem.style.cssText += ';max-width:' + maxWidth + 'px;text-indent:' + Math.round(perChar/2) + 'px;letter-spacing:' + Math.round(perChar) + 'px;';
				}
			}
		}
		var block = this._queue.splice(0, 250),
			count = block.length;
		if (!count) {
			s.hideProgress();
			s.stop.removeEventListener('click', this._onstop, true);
			return null;
		}
		var i = count, data;
		for (; i--; ) {
			data = block[i];
			if (data) {
				data.calculateWidths();
			}
		}
		for (i=count; i--; ) {
			data = block[i];
			if (!data) {
				continue;
			}
			var rbWidths = data.rbWidths,
				rtWidths = data.rtWidths,
				rbs = data.rbs,
				rts = data.rts,
				rbCount = rbs.length;
			for (var j=rbCount; j--; ) {
				var rb = rbs[j],
					rt = rts[j],
					rbWidth = rbWidths[j],
					rtWidth = rtWidths[j],
					diff = rbWidth - rtWidth;
				if (rtWidth === undefined) {
					rbWidths[j-1] += rbWidth;
					continue;
				}
				if (rbWidth === 0) {
					rb.style.cssText = ';min-width:' + rtWidth + 'px;min-height:1px;';
					continue;
				}
				if (rtWidth === 0) {
					rt.style.cssText = ';min-width:' + rbWidth + 'px;min-height:1px;';
					continue;
				}
				if (diff > 0) {
					apply(rt, diff, rbWidth);
				} else {
					apply(rb, Math.abs(diff), rtWidth);
				}
			}
		}
		s.showProgress(count);
		this._arbitrator.postMessage('space');
	},
	clean: function() {
		var request = new XMLHttpRequest();
		request.overrideMimeType('text/html; charset=' + this.document.characterSet);
		request.open('GET', this.document.documentURI, false);
		request.send(null);
		var response = request.responseText.replace(/[\s\S]*<body[^<>]*>/im, ''),
			end = response.lastIndexOf('</body>');
		if (end < 0) {
			end = response.lastIndexOf('</BODY>');
		}
		if (end > 0) {
			response = response.substring(0, end);
		}
		response = response.replace(/<(r[btp])(?=([^<>]*?))\2>([^<>]*)/gim, '<$1$2>$3</$1>');
		response = response.replace(/<(?=(r[btp]))\1[^<>]*?><\/\1>/gim, '');
		response = response.replace(/<\/(?=(r[btp]))\1><\/\1>/gim, '</$1>');
		response = response.replace(/<ruby(?!.{1,50}<rt)/gim, '</ruby');
		response = response.replace(/<ruby/gim, '</ruby><ruby');
		this.document.body.innerHTML = response.replace(/<\/rt>/gim, '</rt></ruby>');
	}
};	

function Manager() {
	var sss = Components.classes['@mozilla.org/content/style-sheet-service;1'].getService(Components.interfaces.nsIStyleSheetService),
		uri = Components.classes['@mozilla.org/network/io-service;1'].getService(Components.interfaces.nsIIOService).newURI('chrome://htmlruby/content/overlay.css', null, null);
	if (!sss.sheetRegistered(uri, sss.USER_SHEET)) {
		sss.loadAndRegisterSheet(uri, sss.USER_SHEET);
	}
	window.addEventListener('load', function(e) {
		var appcontent = document.getElementById('appcontent');
		if (!appcontent) {
			return null;
		}
		appcontent.addEventListener('DOMContentLoaded', function(e) {
			var doc = e.originalTarget;
			if (doc.body === undefined) {
				return null;
			}
			new Processor(doc);
		}, true);
	}, false);
}

function Statusbar() {
	var me = this;
	window.addEventListener('load', function(e) {
	   me.icon = document.getElementById('htmlruby-icon');
	   me.icon.collapsed = false;
		me.meter = document.getElementById('htmlruby-meter');
		me.stop = document.getElementById('htmlruby-stop');
	}, false);
}
Statusbar.prototype = {
	meter: null,
	stop: null,
	total: 1,
	current: 0,
	startProgress: function(totalCount) {
		this.total = totalCount;
		this.current = 0;
		this.showProgress(0);
	},
	showProgress: function(increment) {
		this.current += increment;
		var meter = this.meter;
		meter.value = this.current/this.total*100;
		meter.collapsed = false;
		this.stop.collapsed = false;
	},
	hideProgress: function() {
		this.meter.collapsed = true;
		this.stop.collapsed = true;
	},
	reportProblem: function() {
		var elem = document.getElementById('htmlruby-reportProblem'),
			key = elem.getAttribute('key');
		gBrowser.selectedTab = gBrowser.addTab('https://spreadsheets.google.com/viewform?formkey=' + key + '&entry_0=' + encodeURIComponent(gBrowser.contentDocument.documentURI));
	}
};

var m = new Manager(),
	p = new Preferences(),
	s = new Statusbar();
	
}());