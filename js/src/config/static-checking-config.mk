# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

ifdef ENABLE_CLANG_PLUGIN
# Load the clang plugin from the mozilla topsrcdir. This implies that the clang
# plugin is only usable if we're building js/src under mozilla/, though.
CLANG_PLUGIN := $(DEPTH)/../../build/clang-plugin/$(DLL_PREFIX)clang-plugin$(DLL_SUFFIX)
OS_CXXFLAGS += -Xclang -load -Xclang $(CLANG_PLUGIN) -Xclang -add-plugin -Xclang moz-check
OS_CFLAGS += -Xclang -load -Xclang $(CLANG_PLUGIN) -Xclang -add-plugin -Xclang moz-check
endif
