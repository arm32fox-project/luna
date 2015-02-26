/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 
function run_test() {
  var secMan = Components.classes["@mozilla.org/scriptsecuritymanager;1"].getService(
    Components.interfaces.nsIScriptSecurityManager);

  do_check_true(secMan.isSystemPrincipal(secMan.getObjectPrincipal({})));
}
