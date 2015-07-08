"use strict";
function getValue(value, min, max) {
	if (value >= min && value <= max) {
		return value/100 + 'em';
	}
}
function onPaneLoad() {
	var prefs = Components.classes["@mozilla.org/preferences-service;1"]
	              .getService(Components.interfaces.nsIPrefService)
	              .getBranch("browser.ruby."),
		rubyTextSize = prefs.getCharPref('rubyTextSize'),
		value = rubyTextSize.match(/([.0-9]+)/)[0];
	p.rubyTextSize = rubyTextSize;
	if (value) {
		value = Math.floor(Math.abs(Number(value))*100);
	}
	if (!value || value > 100 || value < 30) {
		value = 55;
	}
	document.getElementById('rubyTextSizeScale').value = value;
	
	var rubyLineHeight = prefs.getCharPref('rubyLineHeight'),
		lineHeight = rubyLineHeight.match(/[.0-9]+/)[0];
	p.rubyLineHeight = rubyLineHeight;
	if (lineHeight) {
		lineHeight = Math.floor(Math.abs(Number(lineHeight))*100);
	}
	if (!lineHeight || lineHeight > 180 || lineHeight < 30) {
		lineHeight = 100;
	}
	document.getElementById('rubyLineHeightScale').value = lineHeight;
	
	p.adjustLineHeight = document.getElementById('adjustLineHeightControl').checked;
	
	p.setUserPreferenceStyleSheet();
}
function onSyncToPreference(node) {
	return getValue(node.value, node.min, node.max);
}
function onChange(scale) {
	var v = getValue(scale.value, scale.min, scale.max);
	document.getElementById(scale.id + 'Label').value = v;
	switch (scale.id) {
		case 'rubyTextSizeScale':
			p.rubyTextSize = v;
			break;
		case 'rubyLineHeightScale':
			p.rubyLineHeight = v;
			break;
	}
	p.setUserPreferenceStyleSheet();
}
function onChangeCheckBox(checkbox) {
	p.adjustLineHeight = checkbox.checked;
	p.setUserPreferenceStyleSheet();
}
function onUnload() {
	p.unload();
	return true;
}
function Preferences() {
	this.register();
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
	register: function() {
		var prefs = Components.classes["@mozilla.org/preferences-service;1"].getService(Components.interfaces.nsIPrefService);
		this._branch = prefs.getBranch("browser.ruby.");
		this._SSService = Components.classes['@mozilla.org/content/style-sheet-service;1'].getService(Components.interfaces.nsIStyleSheetService);
		this._IOService = Components.classes['@mozilla.org/network/io-service;1'].getService(Components.interfaces.nsIIOService);
	},
	setUserPreferenceStyleSheet: function() {
		var sss = this._SSService,
			css = 'rtc,ruby>rt{;;;font-size:' + this.rubyTextSize + '!important;-moz-transform:translateY(-' + this.rubyLineHeight + ')!important}';
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
	},
	unload: function() {
		var sss = this._SSService,
			currentSheet = this._currentSheet;
		if (currentSheet && sss.sheetRegistered(currentSheet, sss.USER_SHEET)) {
			sss.unregisterSheet(currentSheet, sss.USER_SHEET);
		}
	}
};
var p = new Preferences();