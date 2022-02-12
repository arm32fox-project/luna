#! /bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Application Basics
# Change these if you are a full fork
# Otherwise, you are likely a half-assed rebuild
# See: unofficial branding
MOZ_PHOENIX=1
MOZ_APP_VENDOR=MoonchildProductions
MOZ_APP_BASENAME=PaleMoon
MOZ_APP_DISPLAYNAME=$MOZ_APP_BASENAME
MOZ_APP_VERSION=`cat ${_topsrcdir}/$MOZ_BUILD_APP/config/version.txt`
MOZ_APP_ID={ec8030f7-c20a-464f-9b0e-13a3a9e97384}
MOZ_APP_STATIC_INI=1
MOZ_BRANDING_DIRECTORY=browser/branding/unofficial
MOZ_OFFICIAL_BRANDING_DIRECTORY=other-licenses/branding/palemoon/official
MOZ_PROFILE_MIGRATOR=
MOZ_SEPARATE_MANIFEST_FOR_THEME_OVERRIDES=1

# Application Features
MC_PALEMOON=1
MC_APP_ID={8de7fcbb-c55c-4fbe-bfc5-fc555c87dbc4}

if test "$OS_ARCH" = "WINNT"; then
  MOZ_CAN_DRAW_IN_TITLEBAR=1
fi

# Platform Features
MOZ_DEVTOOLS=1
MOZ_SERVICES_COMMON=1
MOZ_SERVICES_SYNC=1
MOZ_JSDOWNLOADS=1
MOZ_WEBGL_CONFORMANT=1

# Application Update Service
# MAR_CHANNEL_ID must not contained the follow 3 characters: ",\t"
# ACCEPTED_MAR_CHANNEL_IDS should usually be the same as MAR_CHANNEL_ID
# If more than one ID is needed, then you should use a comma separated list.
MOZ_UPDATER=
MAR_CHANNEL_ID=unofficial
ACCEPTED_MAR_CHANNEL_IDS=unofficial,unstable,beta,release
