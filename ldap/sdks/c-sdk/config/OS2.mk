# 
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
# 
# The contents of this file are subject to the Mozilla Public License Version 
# 1.1 (the "License"); you may not use this file except in compliance with 
# the License. You may obtain a copy of the License at 
# http://www.mozilla.org/MPL/
# 
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
# 
# The Original Code is the Netscape Portable Runtime (NSPR).
# 
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 1998-2000
# the Initial Developer. All Rights Reserved.
# 
# Contributor(s):
# 
# Alternatively, the contents of this file may be used under the terms of
# either of the GNU General Public License Version 2 or later (the "GPL"),
# or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
# 
# ***** END LICENSE BLOCK ***** 

#
# Configuration common to all (supported) versions of OS/2
#
# OS_CFLAGS is the command line options for the compiler when
#   building the .DLL object files.
# OS_EXE_CFLAGS is the command line options for the compiler
#   when building the .EXE object files; this is for the test
#   programs.
# the macro OS_CFLAGS is set to OS_EXE_CFLAGS inside of the
#   makefile for the pr/tests directory. ... Hack.

# Specify toolset.
XP_OS2   = 1

#
# On OS/2 we use ash...
#
SHELL = ASH.EXE

RANLIB 			= @echo RANLIB
BSDECHO 		= @echo BSDECHO
NSINSTALL 		= nsinstall
INSTALL			= $(NSINSTALL)
MAKE_OBJDIR 		= if test ! -d $(OBJDIR); then mkdir $(OBJDIR); fi

GARBAGE =

XP_DEFINE 		= -DXP_PC
DLL_SUFFIX 		= dll
OBJ_SUFFIX		= o


# Name of the binary code directories
ifdef MOZ_LITE
OBJDIR_NAME 		= $(subst OS2,NAV,$(OS_CONFIG))_$(MOZ_OS2_TOOLS)$(OBJDIR_TAG).OBJ
else
OBJDIR_NAME 		= $(OS_CONFIG)_$(MOZ_OS2_TOOLS)$(OBJDIR_TAG).OBJ
endif

OS_DLLFLAGS 		= -nologo -DLL -FREE -NOE

CC			= gcc
CCC			= gcc
LINK			= gcc
RC 			= rc.exe
FILTER  		= emxexp
IMPLIB  		= emximp -o

OMF_FLAG 		= -Zomf
AR			= emxomfar r $@
LIB_SUFFIX		= lib

# if we compile with GCC we can also use the high-memory flag if specified
ifeq ($(MOZ_OS2_HIGH_MEMORY),1)
HIGHMEM_LDFLAG		= -Zhigh-mem
endif

OS_LIBS     		= -lemxio

DEFINES += -DXP_OS2 -DOS2EMX_PLAIN_CHAR

OS_CFLAGS     		= $(OMF_FLAG) -Wall -Wno-unused -Zmtd
OS_EXE_CFLAGS 		= $(OMF_FLAG) -Wall -Wno-unused -Zmtd
OS_DLLFLAGS 		= $(OMF_FLAG) -Zmt -Zdll -Zcrtdll $(HIGHMEM_LDFLAG) -o $@
EXEFLAGS                += -Zlinker /DE

ifdef BUILD_OPT
OPTIMIZER		= -O3
DLLFLAGS		= $(HIGHMEM_LDFLAG)
EXEFLAGS    		= $(HIGHMEM_LDFLAG) -Zmtd -o $@
else
OPTIMIZER		= -g #-s
DLLFLAGS		= -g $(HIGHMEM_LDFLAG) #-s
EXEFLAGS		= -g $(HIGHMEM_LDFLAG) $(OMF_FLAG) -Zmtd -L$(DIST)/lib -o $@ # -s
EXEFLAGS                += -Zlinker /DE
endif

AR_EXTRA_ARGS 		=


