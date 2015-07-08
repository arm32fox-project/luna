"use strict";
var timeout;
onmessage = function(e) {
	if (timeout) {
		clearTimeout(timeout);
	}
	timeout = setTimeout(postMessage, 100, e.data);
};