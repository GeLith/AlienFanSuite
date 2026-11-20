/*
 * bridge.js — 与原生 AFBridge 通信
 * 调用：AF.call(cmd, args) -> Promise<{ok, data?, error?}>
 * 推送：window.__afPush(state) / window.__afStatus(text, warn)
 */
(function () {
  'use strict';

  var seq = 0;
  var pending = Object.create(null);
  var readyWaiters = [];
  var ready = false;

  function call(cmd, args) {
    return new Promise(function (resolve, reject) {
      var id = ++seq;
      pending[id] = { resolve: resolve, reject: reject };
      try {
        window.webkit.messageHandlers.af.postMessage({
          id: id, cmd: cmd, args: args || {}
        });
      } catch (e) {
        delete pending[id];
        reject(e);
      }
    });
  }

  /* 原生回复入口 */
  window.__afReply = function (id, reply) {
    var p = pending[id];
    if (!p) return;
    delete pending[id];
    if (reply && reply.ok) p.resolve(reply);
    else {
      var err = new Error((reply && reply.error) || '调用失败');
      err.reply = reply;
      p.reject(err);
    }
  };

  var lastState = null;
  var stateListeners = [];

  window.__afPush = function (state) {
    lastState = state;
    if (!ready) { ready = true; readyWaiters.forEach(function (f) { f(); }); readyWaiters = []; }
    stateListeners.forEach(function (f) {
      try { f(state); } catch (e) { console.error(e); }
    });
  };

  window.__afStatus = function (text, warn) {
    document.dispatchEvent(new CustomEvent('afstatus', { detail: { text: text, warn: !!warn } }));
  };

  window.AF = {
    call: call,
    get lastState() { return lastState; },
    onState: function (fn) { stateListeners.push(fn); if (lastState) fn(lastState); },
    whenReady: function (fn) { if (ready) fn(); else readyWaiters.push(fn); }
  };
})();
