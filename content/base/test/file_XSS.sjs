// SJS file for XSS mochitests

Components.utils.import("resource://gre/modules/NetUtil.jsm");

function handleRequest(request, response)
{
  var query = {};
  var requestBody;
  if (request.queryString) {
    request.queryString.split('&').forEach(function (val) {
      var [name, value] = val.split('=');
      query[name] = unescape(value);
    });
  }

  if (request.method == "POST") {
    requestBody = NetUtil.readInputStreamToString(
      request.bodyInputStream,
      request.bodyInputStream.available()
    );
  }

  if (request.method == "POST" &&
      request.hasHeader("Content-Type") &&
      request.getHeader("Content-Type") ==
      "application/x-www-form-urlencoded") {
    requestBody.split('&').forEach(function (val) {
      var [name, value] = val.split('=');
      query[name] = unescape(value.replace(/\+/g, ' '));
    });
  }

  if (request.method == "POST" &&
      request.hasHeader("Content-Type") &&
      request.getHeader("Content-Type")
      .indexOf("multipart/form-data") != -1) {

    var regex = /boundary=(-+.*)/;
    var boundary = regex.exec(request.getHeader("Content-Type"))[1];
    if (boundary == "") {
      response.write("Cannot parse MIME post params");
      return;
    }
    //dump("Boundary: " + boundary + "\n\n\n");
    //dump("Requestbody:\n" + requestBody + "\n\n\n\n");
    var fields = requestBody.split(boundary + "\r\n");
    for (var i = 1; i < fields.length; i++) {
      fields[i] = fields[i].replace(boundary + "--", "");
      //dump("Field: " + fields[i] + "\n\n\n\n");
      regex = /Content-Disposition:.+; name="(.*?)"(?:; filename="(.*?)")?/;
      var result = regex.exec(fields[i]);
      if (!result) {
	response.write("mime parsing error");
	return;
      }
      var name = result[1];
      var filename = result[2];
      if (filename == undefined) {
       	var content = fields[i].split("\r\n\r\n")[1].replace("\r\n--", "");
       	query[name] = content;
      } else {
       	query[name] = filename;
      }
    }
  }

  dump("Query: " + JSON.stringify(query) + "\n");
  if (!query.content) {
    response.write("No content");
    return;
  }

  if (query['hdr'] != 'none') {
    response.setHeader("x-xss-protection", query['hdr'], false);
  }

  //avoid confusing cache behaviors
  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader("Content-Type", "text/html", false);

  //response.write('type=' + query['type']);

  HTML = "<html><head><title>No</title>" +
    '<script type="text/javascript" ' +
    'src="/tests/dom/tests/mochitest/ajax/jquery/dist/jquery.js"></script>' +
    "</head><body>HERE</body></html>";

  HTML_NOBODY = "<html><head><title>No</title>" +
    '<script type="text/javascript" ' +
    'src="/tests/dom/tests/mochitest/ajax/jquery/dist/jquery.js"></script></head>' +
    "HERE</html>";

  HTML_HEAD = "<html><head><title>No</title>" +
    '<script type="text/javascript" ' +
    'src="/tests/dom/tests/mochitest/ajax/jquery/dist/jquery.js"></script>HERE' +
    "</head><body>Hello</body></html>";

  FRAMESET = "<html><head><title>No</title></head>HERE</html>";


  INLINE_SCRIPT = "<script>var pippo = 3456;\n unescape('llla<>');"
		+ "var query = 'term_HERE';"
		+ "var done = /aaa/.test('bbb');</script>";

  STATIC_SCRIPT = "<script>var abcd = 234+2;</script>";

  ATTR = "<a href=\'#\' HERE>Click</a>";
  HANDLER_ATTR = "onclick='HERE'";
  HANDLER_PIECE = "do_HERE()";

  // add these to execute inline event handlers
  FAKE_CLICK = "<script>"
	       + "function fakeClick(obj) {"
	       + "  var evt = document.createEvent('MouseEvents');"
	       + "  evt.initMouseEvent('click', true, true, window,"
	       + "  0, 0, 0, 0, 0, false, false, false, false, 0, null);"
	       + "  var allowDefault = obj.dispatchEvent(evt);"
	       + "}"
	       + "</script>";
  CLICK_TRIGGER = "<script>$(function () { fakeClick($('a')[0]) })</script>";

  EXTERNAL_DOMAIN = '<script src="http://HERE"></script>';

  EXTERNAL_PATH = 'example.org/tests/content/base/test/HERE';

  // add this to run JS urls
  AUTO_FORM = '<form action="HERE">' +
    '<input type="submit" value="Submit"></form>' +
    "<script>$(function () { $('form').submit() })</script>";

  // some quirks require another script tag after the attack
  ADD_SCRIPT = "<script>nop();</script>";

  // TODO: document.getElementById("frm").document is undefined
  ADD_SCRIPT_IFRAME = "<script>$(document).ready( function() { document.title = document.getElementById('frm').document.title; } )</script>";

  EVAL = "<script>eval('HERE');</script>";
  EVAL_READY = "<script>$(document).ready(function() { eval('HERE'); });</script>";
  FUNCTION = "<script>f = Function('HERE'); f();</script>";
  TIMEOUT = "<script>setTimeout('HERE', 1);</script>";

  var fix_param;

  switch(query['type']) {
  case "outside":
    response.write(HTML.replace("HERE", query['content']));
    break;
  case "outside_esc":
    response.write(HTML.replace("HERE", query['content'].replace(/\"/g, "'")));
    break;
  case "outside_script":
    response.write(HTML.replace("HERE", query['content'] + ADD_SCRIPT));
    break;
  case "outside_iframe":
    response.write(HTML.replace("HERE", query['content'] + ADD_SCRIPT_IFRAME));
    break;
  case "inside":
    var outside = INLINE_SCRIPT.replace("HERE", query['content']);
    response.write(HTML.replace("HERE", outside));
    break;
  case "attribute":
    var outside = ATTR.replace("HERE", query['content']);
    response.write(HTML.replace("HERE", outside));
    break;
  case "handler_plain":
    var handler = HANDLER_ATTR.replace("HERE", query['content']);
    var outside = ATTR.replace("HERE", handler);
    response.write(HTML.replace("HERE", FAKE_CLICK + outside + CLICK_TRIGGER));
    break;
  case "handler_inside":
    var piece = HANDLER_PIECE.replace("HERE", query['content']);
    var handler = HANDLER_ATTR.replace("HERE", piece);
    var outside = ATTR.replace("HERE", handler);
    response.write(HTML.replace("HERE", FAKE_CLICK + outside + CLICK_TRIGGER));
    break;
  case "external_domain":
    var outside = EXTERNAL_DOMAIN.replace("HERE", query['content']);
    response.write(HTML.replace("HERE", outside));
    break;
  case "external_path":
    var path = EXTERNAL_PATH.replace("HERE", query['content']);
    var outside = EXTERNAL_DOMAIN.replace("HERE", path);
    response.write(HTML.replace("HERE", outside));
    break;
  case "js_url":
    var outside = AUTO_FORM.replace("HERE", query['content']);
    response.write(HTML.replace("HERE", outside));
    break;
  case "html_nobody":
    response.write(HTML_NOBODY.replace("HERE", query['content']));
    break;
  case "frameset":
    response.write(FRAMESET.replace("HERE", query['content']));
    break;
  case "meta":
    response.write(HTML_HEAD.replace("HERE", query['content']));
    break;
  case "static":
    response.write(HTML.replace("HERE", STATIC_SCRIPT));
    break;
  case "data_url":
    break;
  case "full_inline_good":
    fix_param = '<script>document.title = "Yes";</script>';
    response.write(HTML.replace("HERE",	fix_param));
    break;
  case "part_inline_good":
    fix_param = 'abc\'; document.title = "Yes";//';
    var outside = INLINE_SCRIPT.replace("HERE", fix_param);
    response.write(HTML.replace("HERE", outside));
    break;
  case "handler_plain_good":
    fix_param = 'document.title = "Yes";';
    var handler = HANDLER_ATTR.replace("HERE", fix_param);
    var outside = ATTR.replace("HERE", handler);
    response.write(HTML.replace("HERE", FAKE_CLICK + outside + CLICK_TRIGGER));
    break;
  case "full_external_good":
    fix_param =
'<script src="http://example.org/tests/content/base/test/file_XSS_title.js">' +
'</script>';
    response.write(HTML.replace("HERE",	fix_param));
  case "eval":
    var outside = EVAL.replace("HERE", query['content']);
    response.write(HTML.replace("HERE", outside));
    break;
  case "eval_ready":
    var outside = EVAL_READY.replace("HERE", query['content']);
    response.write(HTML.replace("HERE", outside));
    break;
  case "function":
    var outside = FUNCTION.replace("HERE", query['content']);
    response.write(HTML.replace("HERE", outside));
    break;
  case "timeout":
    var outside = TIMEOUT.replace("HERE", query['content']);
    response.write(HTML.replace("HERE", outside));
    break;
  default:
    response.write("ERROR");
  }

}
