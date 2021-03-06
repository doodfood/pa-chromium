// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

/** @suppress {duplicate} */
var remoting = remoting || {};

/** @constructor */
remoting.HostController = function() {
  /** @return {remoting.HostPlugin} */
  var createNpapiPlugin = function() {
    var plugin = remoting.HostSession.createPlugin();
    /** @type {HTMLElement} @private */
    var container = document.getElementById('daemon-plugin-container');
    container.appendChild(plugin);
    return plugin;
  }

  /** @type {remoting.HostDispatcher} @private */
  this.hostDispatcher_ = new remoting.HostDispatcher(createNpapiPlugin);

  /** @param {string} version */
  var printVersion = function(version) {
    if (version == '') {
      console.log('Host not installed.');
    } else {
      console.log('Host version: ' + version);
    }
  };

  try {
    this.hostDispatcher_.getDaemonVersion(printVersion);
  } catch (err) {
    console.log('Host version not available.');
  }
}

// Note that the values in the enums below are copied from
// daemon_controller.h and must be kept in sync.
/** @enum {number} */
remoting.HostController.State = {
  NOT_IMPLEMENTED: -1,
  NOT_INSTALLED: 0,
  INSTALLING: 1,
  STOPPED: 2,
  STARTING: 3,
  STARTED: 4,
  STOPPING: 5,
  UNKNOWN: 6
};

/** @enum {number} */
remoting.HostController.AsyncResult = {
  OK: 0,
  FAILED: 1,
  CANCELLED: 2,
  FAILED_DIRECTORY: 3
};

/**
 * @param {function(boolean, boolean, boolean):void} callback Callback to be
 *     called when done.
 */
remoting.HostController.prototype.getConsent = function(callback) {
  this.hostDispatcher_.getUsageStatsConsent(callback);
};

/**
 * Registers and starts the host.
 *
 * @param {string} hostPin Host PIN.
 * @param {boolean} consent The user's consent to crash dump reporting.
 * @param {function(remoting.HostController.AsyncResult):void} callback
 * callback Callback to be called when done.
 * @return {void} Nothing.
 */
remoting.HostController.prototype.start = function(hostPin, consent, callback) {
  /** @type {remoting.HostController} */
  var that = this;

  /** @return {string} */
  function generateUuid() {
    var random = new Uint16Array(8);
    window.crypto.getRandomValues(random);
    /** @type {Array.<string>} */
    var e = new Array();
    for (var i = 0; i < 8; i++) {
      e[i] = (/** @type {number} */random[i] + 0x10000).
          toString(16).substring(1);
    }
    return e[0] + e[1] + '-' + e[2] + "-" + e[3] + '-' +
        e[4] + '-' + e[5] + e[6] + e[7];
  };

  var newHostId = generateUuid();

  /** @param {function(remoting.HostController.AsyncResult):void} callback
   *  @param {remoting.HostController.AsyncResult} result
   *  @param {string} hostName
   *  @param {string} publicKey */
  function onStarted(callback, result, hostName, publicKey) {
    if (result == remoting.HostController.AsyncResult.OK) {
      remoting.hostList.onLocalHostStarted(hostName, newHostId, publicKey);
    } else {
      // Unregister the host if we failed to start it.
      remoting.HostList.unregisterHostById(newHostId);
    }
    callback(result);
  };

  /**
   * @param {string} hostName
   * @param {string} publicKey
   * @param {string} privateKey
   * @param {XMLHttpRequest} xhr
   */
  function onRegistered(hostName, publicKey, privateKey, xhr) {
    var success = (xhr.status == 200);

    if (success) {
      that.hostDispatcher_.getPinHash(newHostId, hostPin,
          startHostWithHash.bind(null, hostName, publicKey, privateKey, xhr));
    } else {
      console.log('Failed to register the host. Status: ' + xhr.status +
                  ' response: ' + xhr.responseText);
      callback(remoting.HostController.AsyncResult.FAILED_DIRECTORY);
    }
  };

  /**
   * @param {string} hostName
   * @param {string} publicKey
   * @param {string} privateKey
   * @param {XMLHttpRequest} xhr
   * @param {string} hostSecretHash
   */
  function startHostWithHash(hostName, publicKey, privateKey, xhr,
                             hostSecretHash) {
    var hostConfig = {
        xmpp_login: remoting.identity.getCachedEmail(),
        oauth_refresh_token: remoting.oauth2.exportRefreshToken(),
        host_id: newHostId,
        host_name: hostName,
        host_secret_hash: hostSecretHash,
        private_key: privateKey
    };
    /** @param {remoting.HostController.AsyncResult} result */
    var onStartDaemon = function(result) {
      onStarted(callback, result, hostName, publicKey);
    };
    that.hostDispatcher_.startDaemon(hostConfig, consent, onStartDaemon);
  }

  /**
   * @param {string} hostName
   * @param {string} privateKey
   * @param {string} publicKey
   * @param {string} oauthToken
   */
  function doRegisterHost(hostName, privateKey, publicKey, oauthToken) {
    var headers = {
      'Authorization': 'OAuth ' + oauthToken,
      'Content-type' : 'application/json; charset=UTF-8'
    };

    var newHostDetails = { data: {
       hostId: newHostId,
       hostName: hostName,
       publicKey: publicKey
    } };
    remoting.xhr.post(
        remoting.settings.DIRECTORY_API_BASE_URL + '/@me/hosts/',
        /** @param {XMLHttpRequest} xhr */
        function (xhr) { onRegistered(hostName, publicKey, privateKey, xhr); },
        JSON.stringify(newHostDetails),
        headers);
  };

  /**
   * @param {string} hostName
   * @param {string} privateKey
   * @param {string} publicKey
   */
  function onKeyGenerated(hostName, privateKey, publicKey) {
    remoting.identity.callWithToken(
        /** @param {string} oauthToken */
        function(oauthToken) {
          doRegisterHost(hostName, privateKey, publicKey, oauthToken);
        },
        /** @param {remoting.Error} error */
        function(error) {
          // TODO(jamiewalch): Have a more specific error code here?
          callback(remoting.HostController.AsyncResult.FAILED);
        });
  };

  /**
   * @param {string} hostName
   * @return {void} Nothing.
   */
  function startWithHostname(hostName) {
    that.hostDispatcher_.generateKeyPair(onKeyGenerated.bind(null, hostName));
  }

  this.hostDispatcher_.getHostName(startWithHostname);
};

/**
 * Stop the daemon process.
 * @param {function(remoting.HostController.AsyncResult):void} callback
 *     Callback to be called when finished.
 * @return {void} Nothing.
 */
remoting.HostController.prototype.stop = function(callback) {
  /** @type {remoting.HostController} */
  var that = this;

  /**
   * @param {remoting.HostController.AsyncResult} result The result of the
   *     stopDaemon call, to be passed to the callback.
   * @param {string?} hostId The host id of the local host.
   */
  function unregisterHost(result, hostId) {
    if (hostId) {
      remoting.HostList.unregisterHostById(hostId);
    }
    callback(result);
  };

  /**
   * @param {remoting.HostController.AsyncResult} result The result of the
   *     stopDaemon call, to be passed to the callback.
   */
  function onStopped(result) {
    if (result != remoting.HostController.AsyncResult.OK) {
      callback(result);
      return;
    }
    that.getLocalHostId(unregisterHost.bind(null, result));
  };

  this.hostDispatcher_.stopDaemon(onStopped);
};

/**
 * Check the host configuration is valid (non-null, and contains both host_id
 * and xmpp_login keys).
 * @param {Object} config The host configuration.
 * @return {boolean} True if it is valid.
 */
function isHostConfigValid_(config) {
  return !!config && typeof config['host_id'] == 'string' &&
      typeof config['xmpp_login'] == 'string';
}

/**
 * @param {string} newPin The new PIN to set
 * @param {function(remoting.HostController.AsyncResult):void} callback
 *     Callback to be called when finished.
 * @return {void} Nothing.
 */
remoting.HostController.prototype.updatePin = function(newPin, callback) {
  /** @type {remoting.HostController} */
  var that = this;

  /** @param {Object} config */
  function onConfig(config) {
    if (!isHostConfigValid_(config)) {
      callback(remoting.HostController.AsyncResult.FAILED);
      return;
    }
    /** @type {string} */
    var hostId = config['host_id'];
    that.hostDispatcher_.getPinHash(hostId, newPin, updateDaemonConfigWithHash);
  }

  /** @param {string} pinHash */
  function updateDaemonConfigWithHash(pinHash) {
    var newConfig = {
      host_secret_hash: pinHash
    };
    that.hostDispatcher_.updateDaemonConfig(newConfig, callback);
  }

  // TODO(sergeyu): When crbug.com/121518 is fixed: replace this call
  // with an unprivileged version if that is necessary.
  this.hostDispatcher_.getDaemonConfig(onConfig);
};

/**
 * Get the state of the local host.
 *
 * @param {function(remoting.HostController.State):void} onDone
 *     Completion callback.
 */
remoting.HostController.prototype.getLocalHostState = function(onDone) {
  this.hostDispatcher_.getDaemonState(onDone);
};

/**
 * Get the id of the local host, or null if it is not registered.
 *
 * @param {function(string?):void} onDone Completion callback.
 */
remoting.HostController.prototype.getLocalHostId = function(onDone) {
  /** @type {remoting.HostController} */
  var that = this;
  /** @param {Object} config */
  function onConfig(config) {
    var hostId = null;
    if (isHostConfigValid_(config)) {
      hostId = /** @type {string} */ config['host_id'];
    }
    onDone(hostId);
  };
  try {
    this.hostDispatcher_.getDaemonConfig(onConfig);
  } catch (err) {
    onDone(null);
  }
};

/** @type {remoting.HostController} */
remoting.hostController = null;
