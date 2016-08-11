# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# BE CAREFUL!  This makefile handles a postflight_all rule for a
# multi-project build, so DON'T rely on anything that might differ between
# the two OBJDIRs.

ifndef OBJDIR
OBJDIR = $(MOZ_OBJDIR)/$(firstword $(MOZ_BUILD_PROJECTS))
endif

DIST_ARCH = $(OBJDIR)/dist
DIST_UNI = $(DIST_ARCH)/universal

topsrcdir = $(TOPSRCDIR)
DEPTH = $(OBJDIR)
include $(OBJDIR)/config/autoconf.mk

core_abspath = $(if $(filter /%,$(1)),$(1),$(CURDIR)/$(1))

DIST = $(OBJDIR)/dist

postflight_all:
	mkdir -p $(DIST_UNI)/$(MOZ_PKG_APPNAME)
# Stage a package for buildsymbols to be happy.
	$(MAKE) -C $(OBJDIR)/$(MOZ_BUILD_APP)/installer \
	   PKG_SKIP_STRIP=1 stage-package
