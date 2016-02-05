/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80 filetype=javascript: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Main entry point to get references to all the back-end objects.
 */

"use strict";

this.EXPORTED_SYMBOLS = [
  "Downloads",
];

////////////////////////////////////////////////////////////////////////////////
//// Globals

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;
const Cr = Components.results;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/DownloadCore.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "DownloadIntegration",
                                  "resource://gre/modules/DownloadIntegration.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "DownloadList",
                                  "resource://gre/modules/DownloadList.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "DownloadStore",
                                  "resource://gre/modules/DownloadStore.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "DownloadUIHelper",
                                  "resource://gre/modules/DownloadUIHelper.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "FileUtils",
                                  "resource://gre/modules/FileUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "NetUtil",
                                  "resource://gre/modules/NetUtil.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Promise",
                                  "resource://gre/modules/commonjs/sdk/core/promise.js");
XPCOMUtils.defineLazyModuleGetter(this, "Services",
                                  "resource://gre/modules/Services.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Task",
                                  "resource://gre/modules/Task.jsm");

////////////////////////////////////////////////////////////////////////////////
//// Downloads

/**
 * This object is exposed directly to the consumers of this JavaScript module,
 * and provides the only entry point to get references to back-end objects.
 */
this.Downloads = {
  /**
   * Identifier indicating the public download list.
   */
  get PUBLIC() {
    return "{Downloads.PUBLIC}";
  },

  /**
   * Identifier indicating the private download list.
   */
  get PRIVATE() {
    return "{Downloads.PRIVATE}";
  },

  /**
   * Identifier which is supposed to indicate all downloads.
   * This will act identically to Downloads.PUBLIC.
   */
  get ALL() {
    return "{Downloads.ALL}";
  },

  /**
   * Performs the decoding functionality for createDownload and
   * simpleDownload.
   *
   * Semantics are loosened because of how newer Firefox
   * decodes the objects involved. Source can be a string, for
   * example, not a URI object.
   *
   * This means some inputs here are accepted which
   * wouldn't have before with FF 25-style API, but it still
   * works as before with the usual old-style input.
   */
  _backendDownload: function (aSource, aTarget, aSaver, aProperties)
  {
    return Task.spawn(function task_D_createDownload() {
      let download = new Download();

      // Initialize the DownloadSource properties.
      download.source = new DownloadSource();

      if (aSource instanceof Ci.nsIURI) {
        source.uri = aSource;
      } else if (typeof aSource == "string" ||
                 (typeof aSource == "object" && "charAt" in aSource)) {
        download.source.uri = NetUtil.newURI(aSource);
      } else {
        download.source.uri = aSource.uri;
        if ("isPrivate" in aSource || "isPrivate" in aOptions)
          download.source.isPrivate = aOptions.isPrivate || aSource.isPrivate;
        if ("referrer" in aSource || "referrer" in aOptions)
          download.source.referrer = aOptions.referrer || aSource.referrer;
      }

      // And the DownloadTarget.
      download.target = new DownloadTarget();

      if ((typeof aTarget == "object" && "charAt" in aTarget) ||
          typeof aTarget == "string")
        download.target.file = new FileUtils.File(aTarget);
      else
        download.target.file = aTarget;

      // Support for different aProperties.saver values isn't implemented yet.
      if (aSaver && "type" in aSaver) {
        download.saver = aSaver.type == "legacy"
                  ? new DownloadLegacySaver()
                  : new DownloadCopySaver();
      } else {
        download.saver = new DownloadCopySaver();
      }
      download.saver.download = download;

      // This explicitly makes this function a generator for Task.jsm, so that
      // exceptions in the above calls can be reported asynchronously.
      yield;
      throw new Task.Result(download);
    });
  },

  /**
   * Creates a new Download object.
   *
   * @param aProperties
   *        Provides the initial properties for the newly created download.
   *        {
   *          source: {
   *            uri: The nsIURI for the download source, or a string indicating the URI.
   *            isPrivate: Indicates whether the download originated from a
   *                       private window.
   *          },
   *          target: {
   *            file: The nsIFile for the download target, or a string of the path.
   *          },
   *          saver: {
   *            type: String representing the class of download operation
   *                  handled by this saver object, for example "copy".
   *          },
   *        }
   *
   * @return {Promise}
   * @resolves The newly created Download object.
   * @rejects JavaScript exception.
   */
  createDownload: function D_createDownload(aProperties)
  {
    return this._backendDownload(aProperties.source, aProperties.target, 
                                 aProperties.saver, null);
  },

  /**
   * Downloads data from a remote network location to a local file.
   *
   * This download method does not provide user interface or the ability to
   * cancel the download programmatically.  For that, you should obtain a
   * reference to a Download object using the createDownload function.
   *
   * @param aSource
   *        The nsIURI or string containing the URI spec for the download
   *        source, or alternative DownloadSource.
   * @param aTarget
   *        The nsIFile or string containing the file path, or alternative
   *        DownloadTarget.
   * @param aOptions
   *        The object contains different additional options or null.
   *        {  isPrivate: Indicates whether the download originated from a
   *                      private window.
   *        }
   *
   * @return {Promise}
   * @resolves When the download has finished successfully.
   * @rejects JavaScript exception if the download failed.
   */
  simpleDownload: function D_simpleDownload(aSource, aTarget, aOptions) {
    return this._createDownload(aSource, aTarget, null, aOptions).
      then(function D_SD_onSuccess(aDownload) {
        return aDownload.start();
      });
  },

  /**
   * Wrapper function that is otherwise identical to simpleDownload.
   * Mozilla has since renamed simpleDownload to fetch. This is for
   * API compatibility.
   *
   * See simpleDownload for more information on usage.
   *
   * @param aSource
   * @param aTarget
   * @param aOptions
   *
   * @return {Promise}
   * @resolves When the download has finished successfully.
   * @rejects JavaScript exception if the download failed.
   */
  fetch: function(aSource, aTarget, aOptions) {
    return this.simpleDownload(aSource, aTarget, aOptions);
  },

  /**
   * Retrieves the DownloadList object for downloads that were not started from
   * a private browsing window.
   *
   * Calling this function may cause the download list to be reloaded from the
   * previous session, if it wasn't loaded already.
   *
   * This method always retrieves a reference to the same download list.
   *
   * @return {Promise}
   * @resolves The DownloadList object for public downloads.
   * @rejects JavaScript exception.
   */
  getPublicDownloadList: function D_getPublicDownloadList()
  {
    if (!this._publicDownloadList) {
      this._publicDownloadList = new DownloadList(true);
    }
    return Promise.resolve(this._publicDownloadList);
  },
  _publicDownloadList: null,

  /**
   * Retrieves the DownloadList object for downloads that were started from
   * a private browsing window.
   *
   * This method always retrieves a reference to the same download list.
   *
   * @return {Promise}
   * @resolves The DownloadList object for private downloads.
   * @rejects JavaScript exception.
   */
  getPrivateDownloadList: function D_getPrivateDownloadList()
  {
    if (!this._privateDownloadList) {
      this._privateDownloadList = new DownloadList(false);
    }
    return Promise.resolve(this._privateDownloadList);
  },
  _privateDownloadList: null,

  /**
   * Retrieves the proper DownloadList passed in. Compatibility shim.
   *
   * @param aType
   *        The type of list requested. One of Downloads.PUBLIC,
   *        Downloads.PRIVATE or Downloads.ALL, though Downloads.ALL
   *        will be resolved the same as Downloads.PUBLIC.
   *
   * @return {Promise}
   * @resolves The DownloadList specified by aType.
   * @rejects JavaScript exception.
   */
  getList: function D_getList(aType)
  {
    switch(aType) {
      case Downloads.PUBLIC:
      case Downloads.ALL:
        // I'm aware this isn't technically correct behavior.
        // The reasoning behind it - once we drag in DownloadCombinedList
        // the porting complexity goes through the roof and we would
        // require newer mozilla core jsm. Which isn't happening.
        // Not to mention, I'm not entirely sure mixing public and private mode is
        // even a good idea.
        return this.getPublicDownloadList();
        break;
      case Downloads.PRIVATE:
        return this.getPrivateDownloadList();
        break;
    }
    return Promise.reject();
  },

  /**
   * Returns the system downloads directory asynchronously.
   *   Mac OSX:
   *     User downloads directory
   *   XP/2K:
   *     My Documents/Downloads
   *   Vista and others:
   *     User downloads directory
   *   Linux:
   *     XDG user dir spec, with a fallback to Home/Downloads
   *   Android:
   *     standard downloads directory i.e. /sdcard
   *
   * @return {Promise}
   * @resolves The nsIFile of downloads directory.
   */
  getSystemDownloadsDirectory: function D_getSystemDownloadsDirectory() {
    return DownloadIntegration.getSystemDownloadsDirectory();
  },

  /**
   * Returns the preferred downloads directory based on the user preferences
   * in the current profile asynchronously.
   *
   * @return {Promise}
   * @resolves The nsIFile of downloads directory.
   */
  getUserDownloadsDirectory: function D_getUserDownloadsDirectory() {
    return DownloadIntegration.getUserDownloadsDirectory();
  },

  /**
   * Returns the temporary directory where downloads are placed before the
   * final location is chosen, or while the document is opened temporarily
   * with an external application. This may or may not be the system temporary
   * directory, based on the platform asynchronously.
   *
   * @return {Promise}
   * @resolves The nsIFile of downloads directory.
   */
  getTemporaryDownloadsDirectory: function D_getTemporaryDownloadsDirectory() {
    return DownloadIntegration.getTemporaryDownloadsDirectory();
  },

  /**
   * Constructor for a DownloadError object.  When you catch an exception during
   * a download, you can use this to verify if "ex instanceof Downloads.Error",
   * before reading the exception properties with the error details.
   */
  Error: DownloadError,
};
