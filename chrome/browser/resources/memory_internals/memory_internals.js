// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var g_main_view = null;

/**
 * This class is the root view object of the page.
 */
var MainView = (function() {
  'use strict';

  /**
   * @constructor
   */
  function MainView() {
    $('button-update').onclick = function() {
      chrome.send('update');
    };
  };

  MainView.prototype = {
    /**
     * Receiving notification to display memory snapshot.
     * @param {Object}  Information about memory in JSON format.
     */
    onSetSnapshot: function(browser) {
      this.updateSnapshot(browser['processes']);
      this.updateExtensions(browser['extensions']);

      $('os-value').textContent = browser['os'] + ' (' +
          browser['os_version'] + ')';
      $('uptime-value').textContent = Math.floor(browser['uptime'] / 1000) +
          ' sec';

      $('json').textContent = JSON.stringify(browser);
      $('json').style.display = 'block';
    },

    /**
     * Update process information table.
     * @param {Object} processes information about memory.
     */
    updateSnapshot: function(processes) {
      // Remove existing processes.
      var size = $('snapshot-view').getElementsByClassName('process').length;
      for (var i = 0; i < size; ++i) {
        $('snapshot-view').deleteRow(-1);
      }

      var template = $('process-template').childNodes;
      // Add processes.
      for (var p in processes) {
        var process = processes[p];

        var row = $('snapshot-view').insertRow(-1);
        // We skip |template[0]|, because it is a (invalid) Text object.
        for (var i = 1; i < template.length; ++i) {
          var value = '---';
          switch (template[i].className) {
          case 'process-id':
            value = process['pid'];
            break;
          case 'process-info':
            value = process['type'] + '<br>' + process['titles'].join('<br>');
            break;
          case 'process-memory':
            value = process['memory_private'];
            break;
          }
          var col = row.insertCell(-1);
          col.innerHTML = value;
          col.className = template[i].className;
        }
        row.setAttribute('class', 'process');
      }
    },

    /**
     * Update extension information table.
     * @param {Object} extensions information about memory.
     */
    updateExtensions: function(extensions) {
      // Remove existing information.
      var size = $('extension-view').getElementsByClassName('extension').length;
      for (var i = 0; i < size; ++i) {
        $('extension-view').deleteRow(-1);
      }

      var template = $('extension-template').childNodes;
      // Add information of each extension.
      for (var id in extensions) {
        var extension = extensions[id];

        var row = $('extension-view').insertRow(-1);
        // We skip |template[0]|, because it is a (invalid) Text object.
        for (var i = 1; i < template.length; ++i) {
          var value = '---';
          switch (template[i].className) {
          case 'extension-id':
            value = extension['pid'];
            break;
          case 'extension-info':
            value = extension['titles'].join('<br>');
            break;
          case 'extension-memory':
            value = extension['memory_private'];
            break;
          }
          var col = row.insertCell(-1);
          col.innerHTML = value;
          col.className = template[i].className;
        }
        row.setAttribute('class', 'extension');
      }
    }
  };

  return MainView;
})();

/**
 * Initialize everything once we have access to chrome://memory-internals.
 */
document.addEventListener('DOMContentLoaded', function() {
  g_main_view = new MainView();
});
