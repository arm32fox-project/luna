# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

MAKE_INSTALLER_TARGET = $(MAKE) -C browser/installer

install:
	@$(MAKE_INSTALLER_TARGET) $@

installer:
	@$(MAKE_INSTALLER_TARGET) $@

package:
	@$(MAKE_INSTALLER_TARGET) archive

buildsymbols:
	@$(MAKE_INSTALLER_TARGET) symbols

mar-package:
	@$(MAKE_INSTALLER_TARGET) update

mar-package-bz2:
	@$(MAKE_INSTALLER_TARGET) update-bz2

l10n-package:
	@$(MAKE_INSTALLER_TARGET) locale

theme-package:
	@$(MAKE_INSTALLER_TARGET) skin

stage-package:
	@$(MAKE_INSTALLER_TARGET) $@
