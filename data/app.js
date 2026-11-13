(function () {
  'use strict';

  const $ = (id) => document.getElementById(id);
  const logEl = $('log');
  function log(msg) {
    const line = document.createElement('div');
    line.textContent = new Date().toLocaleTimeString() + ' ' + msg;
    logEl.prepend(line);
    while (logEl.childElementCount > 40) logEl.removeChild(logEl.lastChild);
  }

  let ws = null;
  let connected = false;
  const statusEl = $('status');
  function setStatus(c) {
    connected = c;
    statusEl.textContent = c ? 'Connected' : 'Disconnected';
    statusEl.className = 'status ' + (c ? 'connected' : 'disconnected');
  }
  let controlMode = 'joystick';
  const ctrlModeBtn = $('ctrl-mode');
  function applyControlMode() {
    const joySec = $('joystick-section');
    const gyroSec = document.querySelector('.gyro-panel');
    if (controlMode === 'joystick') {
      joySec.classList.remove('hidden');
      gyroSec.classList.add('hidden');
    } else {
      joySec.classList.add('hidden');
      gyroSec.classList.remove('hidden');
    }
    ctrlModeBtn.textContent = 'Mode: ' + (controlMode === 'joystick' ? 'Joystick' : 'Air Mouse');
  }
  ctrlModeBtn.addEventListener('click', () => {
    controlMode = controlMode === 'joystick' ? 'air' : 'joystick';
    applyControlMode();
    log('control mode: ' + controlMode);
  });
  applyControlMode();

  function connect() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const url = proto + '//' + location.host + '/ws';
    ws = new WebSocket(url);
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => { setStatus(true); log('WebSocket connected'); send({ type: 'hello', name: 'WebController' }); };
    ws.onclose = () => { setStatus(false); log('WebSocket closed'); setTimeout(connect, 2000); };
    ws.onerror = () => { log('WebSocket error'); };
    ws.onmessage = (e) => {
      try { const m = JSON.parse(e.data); if (m.type === 'pong') return; log('rx: ' + e.data); } catch (_) {}
    };
  }
  function send(obj) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(obj)); }
  function sendBin(buf) { if (ws && ws.readyState === 1) ws.send(buf); }

  // Joystick
  const joy = $('joystick');
  const stick = $('stick');
  let joyActive = false;
  let joyX = 0, joyY = 0;
  let joyTimer = null;
  function updateStick() {
    stick.style.transform = 'translate(' + (joyX * 60) + 'px,' + (joyY * 60) + 'px)';
  }
  function handleJoy(clientX, clientY) {
    const r = joy.getBoundingClientRect();
    const cx = r.left + r.width / 2;
    const cy = r.top + r.height / 2;
    let dx = (clientX - cx) / (r.width / 2);
    let dy = (clientY - cy) / (r.height / 2);
    const mag = Math.hypot(dx, dy);
    if (mag > 1) { dx /= mag; dy /= mag; }
    joyX = dx; joyY = dy;
    updateStick();
  }
  function endJoy() {
    joyActive = false;
    joyX = 0; joyY = 0;
    updateStick();
    if (joyTimer) { clearInterval(joyTimer); joyTimer = null; }
    send({ type: 'joystick', x: 0, y: 0 });
  }
  joy.addEventListener('touchstart', (e) => { joyActive = true; handleJoy(e.touches[0].clientX, e.touches[0].clientY); if (!joyTimer) joyTimer = setInterval(() => { if (joyActive && controlMode === 'joystick') send({ type: 'joystick', x: joyX, y: joyY }); }, 16); e.preventDefault(); }, { passive: false });
  joy.addEventListener('touchmove', (e) => { if (joyActive) handleJoy(e.touches[0].clientX, e.touches[0].clientY); e.preventDefault(); }, { passive: false });
  joy.addEventListener('touchend', (e) => { endJoy(); e.preventDefault(); }, { passive: false });
  joy.addEventListener('mousedown', (e) => { joyActive = true; handleJoy(e.clientX, e.clientY); if (!joyTimer) joyTimer = setInterval(() => { if (joyActive && controlMode === 'joystick') send({ type: 'joystick', x: joyX, y: joyY }); }, 16); });
  window.addEventListener('mousemove', (e) => { if (joyActive) handleJoy(e.clientX, e.clientY); });
  window.addEventListener('mouseup', () => { if (joyActive) endJoy(); });

  // Buttons
  $('btn-left').addEventListener('click', () => send({ type: 'button', id: 'left', state: true }));
  $('btn-right').addEventListener('click', () => send({ type: 'button', id: 'right', state: true }));
  $('btn-scroll-up').addEventListener('click', () => send({ type: 'scroll', delta: 1 }));
  $('btn-scroll-down').addEventListener('click', () => send({ type: 'scroll', delta: -1 }));

  // Gyro/accel
  let gyroOn = false;
  const gyroToggle = $('gyro-toggle');
  const gyroValues = $('gyro-values');
  const accelValues = $('accel-values');
  const sens = $('sens');
  const flipX = $('flipx');
  const flipY = $('flipy');
  const canvas = $('orient');
  const ctx = canvas.getContext('2d');
  $('calib').addEventListener('click', () => { baseBeta = curBeta; baseGamma = curGamma; calibrated = true; log('calibrated'); drawOrient(); });
  let lastGyro = 0;
  let baseBeta = 0, baseGamma = 0;
  let curBeta = 0, curGamma = 0;
  let calibrated = false;
  function calibrate() {
    baseBeta = curBeta; baseGamma = curGamma; calibrated = true;
    log('gyro calibrated');
    drawOrient();
  }

  function drawOrient() {
    const w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = '#334155';
    ctx.beginPath();
    ctx.moveTo(w / 2, 0); ctx.lineTo(w / 2, h);
    ctx.moveTo(0, h / 2); ctx.lineTo(w, h / 2);
    ctx.stroke();
    // neutral zone
    ctx.strokeStyle = '#1e3a8a';
    ctx.strokeRect(w / 2 - 30, h / 2 - 30, 60, 60);
    const dx = calibrated ? (curGamma - baseGamma) : 0;
    const dy = calibrated ? (curBeta - baseBeta) : 0;
    const x = Math.max(-80, Math.min(80, (flipX.checked ? -dx : dx) * 2));
    const y = Math.max(-80, Math.min(80, (flipY.checked ? -dy : dy) * 2));
    ctx.fillStyle = '#3b82f6';
    ctx.beginPath();
    ctx.arc(w / 2 + x, h / 2 + y, 8, 0, Math.PI * 2);
    ctx.fill();
  }
  function handleOrientation(e) {
    const now = Date.now();
    if (now - lastGyro < 16) return;
    lastGyro = now;
    curBeta = e.beta || 0; curGamma = e.gamma || 0;
    if (!calibrated) { baseBeta = curBeta; baseGamma = curGamma; calibrated = true; }
    gyroValues.textContent = 'tilt L/R:' + curGamma.toFixed(1) + ' tilt F/B:' + curBeta.toFixed(1);
    drawOrient();
  }
  let lastMotion = 0;
  function handleMotion(e) {
    const now = Date.now();
    if (now - lastMotion < 16) return;
    lastMotion = now;
    const a = e.accelerationIncludingGravity || e.acceleration;
    if (a) accelValues.textContent = 'ax:' + (a.x || 0).toFixed(1) + ' ay:' + (a.y || 0).toFixed(1) + ' az:' + (a.z || 0).toFixed(1);
    const rr = e.rotationRate;
    if (rr) {
      // rotationRate is deg/s; map to pointer velocity
      const axisx = $('axisx').value;
      const axisy = $('axisy').value;
      function getAxis(rr, axis, orientDelta) {
        if (rr) {
          if (axis === 'alpha' && rr.alpha != null) return rr.alpha;
          if (axis === 'beta' && rr.beta != null) return rr.beta;
          if (axis === 'gamma' && rr.gamma != null) return rr.gamma;
        }
        // fallback to orientation deltas
        if (axis === 'alpha') return 0;
        if (axis === 'beta') return (curBeta - baseBeta) * 4;
        if (axis === 'gamma') return (curGamma - baseGamma) * 4;
        return 0;
      }
      let rx = getAxis(rr, axisx, 0);
      let ry = getAxis(rr, axisy, 0);
      if (flipX.checked) rx = -rx;
      if (flipY.checked) ry = -ry;
      const dz = 2;
      const mx = Math.abs(rx) < dz ? 0 : rx;
      const my = Math.abs(ry) < dz ? 0 : ry;
      const s = +sens.value;
      if (controlMode === 'air') send({ type: 'gyro', gx: mx * s * 0.6, gy: -my * s * 1.2, gz: 0, sens: s });
    }
    if (a) send({ type: 'accel', ax: a.x || 0, ay: a.y || 0, az: a.z || 0 });
  }
  gyroToggle.addEventListener('click', async () => {
    if (!gyroOn) {
      if (typeof DeviceOrientationEvent !== 'undefined' && typeof DeviceOrientationEvent.requestPermission === 'function') {
        try {
          const p = await DeviceOrientationEvent.requestPermission();
          if (p !== 'granted') { log('orientation permission denied'); return; }
        } catch (e) { log('perm error: ' + e.message); }
      }
      window.addEventListener('deviceorientation', handleOrientation);
      window.addEventListener('devicemotion', handleMotion);
      baseBeta = 0; baseGamma = 0;
      gyroOn = true;
      gyroToggle.textContent = 'Disable';
      log('gyro enabled');
    } else {
      window.removeEventListener('deviceorientation', handleOrientation);
      window.removeEventListener('devicemotion', handleMotion);
      gyroOn = false;
      gyroToggle.textContent = 'Enable';
      log('gyro disabled');
    }
  });
  flipX.addEventListener('change', drawOrient);
  flipY.addEventListener('change', drawOrient);
  sens.addEventListener('input', () => {});
  drawOrient();

  // Keys
  document.querySelectorAll('.key-btn').forEach((b) => {
    b.addEventListener('click', () => {
      if (b.dataset.hotkey) send({ type: 'hotkey', key: b.dataset.hotkey });
      else send({ type: 'key', key: b.dataset.key });
    });
  });

  // Mic with buffering
  let audioCtx = null;
  let micStream = null;
  let processor = null;
  let micLive = false;
  let micBuf = [];
  let micTimer = null;
  const micStatus = $('mic-status');
  const micLevel = $('mic-level');
  async function ensureMic() {
    if (micStream) return micStream;
    micStream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true } });
    return micStream;
  }
  function floatToPcm16(float32) {
    const out = new Int16Array(float32.length);
    for (let i = 0; i < float32.length; i++) {
      let s = Math.max(-1, Math.min(1, float32[i]));
      out[i] = s < 0 ? s * 0x8000 : s * 0x7fff;
    }
    return out;
  }
  function flushMic() {
    if (!micLive) return;
    while (micBuf.length >= 320) {
      const chunk = new Int16Array(micBuf.splice(0, 320));
      sendBin(chunk.buffer);
    }
  }
  async function startLive() {
    if (micLive) return;
    await ensureMic();
    audioCtx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 16000 });
    const src = audioCtx.createMediaStreamSource(micStream);
    processor = audioCtx.createScriptProcessor(512, 1, 1);
    src.connect(processor);
    processor.connect(audioCtx.destination);
    processor.onaudioprocess = (e) => {
      if (!micLive) return;
      const pcm = floatToPcm16(e.inputBuffer.getChannelData(0));
      for (let i = 0; i < pcm.length; i++) micBuf.push(pcm[i]);
      let sum = 0;
      for (let i = 0; i < pcm.length; i++) sum += Math.abs(pcm[i]);
      const lvl = Math.min(100, (sum / pcm.length / 32768) * 400);
      micLevel.style.width = lvl + '%';
    };
    micLive = true;
    micStatus.textContent = 'Live streaming';
    send({ type: 'mic', action: 'live_start' });
    micTimer = setInterval(flushMic, 20);
    log('mic live start');
  }
  function stopLive() {
    if (!micLive) return;
    micLive = false;
    if (micTimer) { clearInterval(micTimer); micTimer = null; }
    flushMic();
    micStatus.textContent = 'Mic idle';
    send({ type: 'mic', action: 'live_stop' });
    log('mic live stop');
  }
  $('mic-live').addEventListener('mousedown', startLive);
  $('mic-live').addEventListener('touchstart', (e) => { e.preventDefault(); startLive(); }, { passive: false });
  $('mic-live').addEventListener('mouseup', stopLive);
  $('mic-live').addEventListener('touchend', (e) => { e.preventDefault(); stopLive(); }, { passive: false });
  $('mic-live').addEventListener('mouseleave', stopLive);

  // Clip recording
  let clipChunks = [];
  let clipRecording = false;
  let clipRecorder = null;
  $('mic-clip').addEventListener('click', async () => {
    if (!clipRecording) {
      await ensureMic();
      clipChunks = [];
      clipRecording = true;
      micStatus.textContent = 'Recording clip...';
      send({ type: 'mic', action: 'clip_start' });
      try {
        clipRecorder = new MediaRecorder(micStream);
        clipRecorder.ondataavailable = (e) => { if (e.data.size > 0) clipChunks.push(e.data); };
        clipRecorder.onstop = async () => {
          micStatus.textContent = 'Sending clip...';
          send({ type: 'mic', action: 'clip_finish' });
          const blob = new Blob(clipChunks, { type: 'audio/webm' });
          const arr = await blob.arrayBuffer();
          const ctx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 16000 });
          const audioBuf = await ctx.decodeAudioData(arr);
          const pcm = floatToPcm16(audioBuf.getChannelData(0));
          const chunkSize = 320;
          for (let i = 0; i < pcm.length; i += chunkSize) {
            const chunk = pcm.slice(i, i + chunkSize);
            if (chunk.length === chunkSize) { sendBin(chunk.buffer); await new Promise((r) => setTimeout(r, 20)); }
          }
          send({ type: 'mic', action: 'clip_end' });
          micStatus.textContent = 'Mic idle';
          clipRecording = false;
        };
        clipRecorder.start();
        log('clip recording started');
      } catch (e) {
        log('MediaRecorder error: ' + e.message);
        clipRecording = false;
      }
    } else {
      if (clipRecorder && clipRecorder.state !== 'inactive') clipRecorder.stop();
    }
  });

  connect();
})();
