.. _localization:

===================
Localization (l10n)
===================

Single-locale language repacks
==============================

To save on build time, the build system and automation collaborate to allow
downloading a packaged en-US Firefox, performing some locale-specific
post-processing, and re-packaging a locale-specific Firefox.  Such artifacts
are termed "single-locale language repacks".  There is another concept of a
"multi-locale language build", which is more like a regular build and less
like a re-packaging post-processing step.

There are scripts in-tree in mozharness to orchestrate these re-packaging
steps for `Desktop
<https://dxr.mozilla.org/mozilla-central/source/testing/mozharness/scripts/desktop_l10n.py>`_
but they rely heavily on buildbot information so they are almost impossible to
run locally.
