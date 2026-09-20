(function () {
  'use strict';

  let ws = null;
  let reconnectTimer = null;
  let suppressSegEdit = false;
  let units = 'kph';
  // The last state message, kept so a units change arriving on telemetry can
  // re-title the stage panel without waiting for the next state broadcast.
  let lastState = null;

  const $ = (id) => document.getElementById(id);

  function wsUrl() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    return proto + '//' + location.host + '/ws';
  }

  function send(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(obj));
    }
  }

  function formatDist(m) {
    m = Number(m) || 0;
    if (Math.abs(m) > 999999) return Math.round(m / 1000).toLocaleString() + ' km';
    return Math.round(m).toLocaleString() + ' m';
  }

  function applyTelemetry(msg) {
    $('rally-clock').textContent = msg.rally_clock || '--:--:--';
    const ab = Number(msg.ahead_behind_s) || 0;
    $('ahead-value').textContent = (ab >= 0 ? '+' : '') + ab.toFixed(1) + ' s';
    const abEl = $('ahead-behind');
    abEl.classList.toggle('ahead', ab > 0.05);
    abEl.classList.toggle('behind', ab < -0.05);

    const previousUnits = units;
    units = msg.units || 'kph';
    if (units !== previousUnits && lastState) renderStagePanel(lastState);
    $('unit-label').textContent = units;
    $('cur-kph').textContent = Number(msg.cur_kph || 0).toFixed(1);
    $('target-kph').textContent = Number(msg.target_kph || 0).toFixed(1);
    $('trip-m').textContent = formatDist(msg.trip_m);
    $('total-m').textContent = formatDist(msg.total_m);
    $('trip-avg').textContent = Number(msg.trip_avg_kph || 0).toFixed(1);
    $('total-avg').textContent = Number(msg.total_avg_kph || 0).toFixed(1);
    $('seg-num').textContent = msg.segment_number || 0;
    $('seg-count').textContent = msg.segment_count || 0;

    $('btn-next').disabled = !msg.next_enabled;
    $('btn-prev').disabled = !msg.prev_enabled;

    toneHeard(msg.tone);
    beepHeard(msg.beep);
  }

  // Reflects the box's own settings back into the controls. Skips whichever
  // field has focus so an incoming update cannot yank a value out from under
  // someone mid-edit on the phone.
  function applySetupState(state) {
    const active = document.activeElement;
    const setIf = (el, apply) => { if (el && el !== active) apply(el); };

    setIf($('beep-enabled'), (el) => { el.checked = !!state.beep_assist_enabled; });
    setIf($('beep-nav'), (el) => { el.checked = !!state.beep_navigation_mode; });
    setIf($('beep-timing'), (el) => { el.checked = !!state.beep_timing_mode; });
    setIf($('beep-advance-m'), (el) => { el.value = Number(state.beep_advance_m) || 0; });
    setIf($('beep-advance-s'), (el) => { el.value = Number(state.beep_advance_s) || 0; });
    setIf($('beep-waypoints'), (el) => { el.value = state.beep_waypoints_km || ''; });
    setIf($('tone-enabled'), (el) => { el.checked = !!state.tone_enabled; });

    const type = Number(state.tone_type) || 1;
    document.querySelectorAll('input[name="tone-type"]').forEach((radio) => {
      if (radio !== active) radio.checked = Number(radio.value) === type;
    });

    const populated = state.memory_populated || [];
    document.querySelectorAll('.mem-slot').forEach((btn) => {
      btn.classList.toggle('populated', !!populated[Number(btn.dataset.slot) - 1]);
    });
  }

  // The co-pilot's stage panel, built from the same `segments` list the box
  // builds it from -- the roadbook as set, not the running stage's frozen
  // snapshot. The cumulative column is computed here for the same reason the
  // box computes it: the roadbook gives each segment's length, but the
  // odometer counts from the stage start.
  function renderStagePanel(state) {
    $('stage-autostart').textContent = state.autostart || 'none';
    $('stage-speed-head').textContent = units === 'mph' ? 'MPH' : 'KPH';

    const tbody = $('stage-table').querySelector('tbody');
    tbody.innerHTML = '';
    let cumulative = 0;
    (state.segments || []).forEach((seg) => {
      const dist = Number(seg.distance_m) || 0;
      cumulative += dist;
      const tr = document.createElement('tr');
      // Converted, not just relabelled: the state message always carries KPH,
      // and the box's own panel converts before printing. Leaving the number
      // raw under an MPH heading showed 30.00 where the box showed 18.64 --
      // the same segment, wrong by a margin the crew would drive to.
      const speed = Number(seg.target_speed_kph || 0) * (units === 'mph' ? 0.621371 : 1);
      tr.innerHTML =
        '<td>' + speed.toFixed(2) + '</td>' +
        '<td>' + Math.round(dist).toLocaleString() + '</td>' +
        '<td>' + Math.round(cumulative).toLocaleString() + '</td>';
      tbody.appendChild(tr);
    });
    if (!(state.segments || []).length) {
      const tr = document.createElement('tr');
      tr.innerHTML = '<td colspan="3" class="stage-empty">(no segments set)</td>';
      tbody.appendChild(tr);
    }
  }

  function renderSegments(state) {
    const tbody = $('seg-table').querySelector('tbody');
    tbody.innerHTML = '';
    suppressSegEdit = true;
    (state.segments || []).forEach((seg, i) => {
      const tr = document.createElement('tr');
      tr.innerHTML =
        '<td>' + (i + 1) + '</td>' +
        '<td><input type="number" step="0.1" class="spd" value="' + seg.target_speed_kph + '"></td>' +
        '<td><input type="number" step="1" class="dist" value="' + seg.distance_m + '"></td>' +
        '<td><input type="checkbox" class="auto"' + (seg.autoNext ? ' checked' : '') + '></td>' +
        '<td><button type="button" class="del">del</button></td>';
      tr.querySelector('.spd').addEventListener('change', () => pushSegSet(i, tr));
      tr.querySelector('.dist').addEventListener('change', () => pushSegSet(i, tr));
      tr.querySelector('.auto').addEventListener('change', () => pushSegSet(i, tr));
      tr.querySelector('.del').addEventListener('click', () => send({ type: 'segment_delete', index: i }));
      tbody.appendChild(tr);
    });
    suppressSegEdit = false;
  }

  function pushSegSet(index, tr) {
    if (suppressSegEdit) return;
    send({
      type: 'segment_set',
      index: index,
      target_speed_kph: Number(tr.querySelector('.spd').value),
      distance_m: Number(tr.querySelector('.dist').value),
      autoNext: tr.querySelector('.auto').checked
    });
  }

  function onMessage(ev) {
    let msg;
    try { msg = JSON.parse(ev.data); } catch { return; }
    if (msg.type === 'telemetry') applyTelemetry(msg);
    if (msg.type === 'state') {
      lastState = msg;
      renderSegments(msg);
      applySetupState(msg);
      renderStagePanel(msg);
    }
  }

  function connect() {
    if (ws) { ws.onclose = null; ws.close(); }
    ws = new WebSocket(wsUrl());
    $('conn').textContent = 'connecting…';
    $('conn').classList.remove('ok');
    ws.onopen = () => {
      $('conn').textContent = 'connected';
      $('conn').classList.add('ok');
    };
    ws.onclose = () => {
      stopTone();
      $('conn').textContent = 'disconnected';
      $('conn').classList.remove('ok');
      clearTimeout(reconnectTimer);
      reconnectTimer = setTimeout(connect, 2000);
    };
    ws.onmessage = onMessage;
  }

  $('tab-live').addEventListener('click', () => {
    $('tab-live').classList.add('active');
    $('tab-setup').classList.remove('active');
    $('view-live').classList.remove('hidden');
    $('view-setup').classList.add('hidden');
  });
  $('tab-setup').addEventListener('click', () => {
    $('tab-setup').classList.add('active');
    $('tab-live').classList.remove('active');
    $('view-setup').classList.remove('hidden');
    $('view-live').classList.add('hidden');
  });

  // ---- Distance correction ----
  // The co-pilot's Total row, verbatim: -10 and +10 nudge both Total and Trip
  // (one wheel measurement, so a slip correction belongs to both), and "set"
  // pins Total alone to a roadbook figure.
  $('btn-dist-minus').addEventListener('click', () => send({ type: 'distance_adjust', delta_m: -10 }));
  $('btn-dist-plus').addEventListener('click', () => send({ type: 'distance_adjust', delta_m: 10 }));
  $('btn-dist-set').addEventListener('click', () => {
    const field = $('dist-set-m');
    // An empty box is someone who has not typed a figure yet, not a request
    // to pin Total to zero -- Reset Total is the button for that.
    if (field.value.trim() === '') return;
    const meters = Number(field.value);
    if (!Number.isFinite(meters) || meters < 0) return;
    send({ type: 'distance_set', meters: meters });
    field.value = '';
    field.blur();
  });

  $('btn-next').addEventListener('click', () => send({ type: 'next' }));
  $('btn-prev').addEventListener('click', () => send({ type: 'prev' }));
  $('btn-reset-trip').addEventListener('click', resetTrip);
  $('btn-reset-trip2').addEventListener('click', resetTrip);
  // With a stage running, the box's own question (a missed start): the
  // distance zero is the press, so the box is told at once and the reset
  // applied only if the crew confirm.
  $('btn-reset-total').addEventListener('click', resetTotal);
  $('btn-reset-total-live').addEventListener('click', resetTotal);
  function resetTotal() {
    if (lastState && lastState.total_reset_asks) {
      send({ type: 'reset_total_capture' });
      $('stage-menu').classList.remove('hidden');
      return;
    }
    if (confirm('Reset total distance?')) send({ type: 'reset_total' });
  }
  const closeStageMenu = () => $('stage-menu').classList.add('hidden');
  $('menu-abort').addEventListener('click', () => {
    closeStageMenu();
    send({ type: 'reset_total', choice: 'confirm' });
  });
  // Back: the box is holding the position of the press, so it has to be told
  // the press is abandoned. Its own message, not a "reset_total" carrying a
  // cancel: with no stage running that message means "just do it" above, so a
  // Back arriving after the stage ended would zero Total.
  $('menu-cancel').addEventListener('click', () => {
    closeStageMenu();
    send({ type: 'reset_total_cancel' });
  });

  // The box's Reset Trip never asks, so neither does the phone: a confirm()
  // costs a second the crew do not have, mid-stage or not.
  function resetTrip() {
    send({ type: 'reset_trip' });
  }

  $('btn-add-seg').addEventListener('click', () => {
    const kph = prompt('Target speed (kph):', '75');
    const dist = prompt('Distance (m):', '1000');
    if (!kph || !dist) return;
    send({
      type: 'segment_add',
      target_speed_kph: Number(kph),
      distance_m: Number(dist),
      autoNext: true
    });
  });

  // A slot button now acts on its own row's verb, so there is no separate
  // select-then-press step and no selection state to get out of sync.
  document.querySelectorAll('.mem-slot').forEach((btn) => {
    btn.addEventListener('click', () => {
      const slot = Number(btn.dataset.slot);
      const store = btn.dataset.action === 'store';
      const populated = btn.classList.contains('populated');
      // Confirm only when something would be lost: overwriting a saved stage,
      // or replacing the segments on screen with a recalled one.
      const question = store
        ? (populated ? 'Overwrite memory ' + slot + '?' : null)
        : 'Recall memory ' + slot + '?';
      if (!store && !populated) return;  // nothing saved there, so nothing to ask
      if (question && !confirm(question)) return;
      send({ type: store ? 'memory_store' : 'memory_recall', slot: slot });
    });
  });

  // ---- Beep Assist ----
  // Each control sends only its own field; the box merges it into state.
  function sendBeep(patch) { send(Object.assign({ type: 'beep_set' }, patch)); }

  $('beep-enabled').addEventListener('change', (e) => sendBeep({ enabled: e.target.checked }));
  $('beep-nav').addEventListener('change', (e) => sendBeep({ navigation: e.target.checked }));
  $('beep-timing').addEventListener('change', (e) => sendBeep({ timing: e.target.checked }));

  // Committed on blur, not per keystroke: mid-typing a number is a real value
  // ("3" on the way to "30"), and sending it would arm a waypoint the operator
  // never meant.
  $('beep-advance-m').addEventListener('change', (e) => sendBeep({ advance_m: Number(e.target.value) || 0 }));
  $('beep-advance-s').addEventListener('change', (e) => sendBeep({ advance_s: Number(e.target.value) || 0 }));
  $('beep-waypoints').addEventListener('change', (e) => sendBeep({ waypoints_km: e.target.value }));

  // ---- Tone ----
  $('tone-enabled').addEventListener('change', (e) => send({ type: 'tone_set', enabled: e.target.checked }));
  document.querySelectorAll('input[name="tone-type"]').forEach((radio) => {
    radio.addEventListener('change', () => {
      if (radio.checked) send({ type: 'tone_set', tone_type: Number(radio.value) });
    });
  });

  // ---- Sound (RB-WEB-02) ----
  // The box's ahead/behind tone, played exactly as the box is playing it --
  // telemetry carries its cadence -- and the box's click on every button
  // pressed here. Browsers allow sound only once the page has been touched,
  // so the audio starts on the first tap; "Sound" in the header mutes this
  // phone only.
  let audioCtx = null;
  let soundOn = true;
  try { soundOn = localStorage.getItem('rallySound') !== 'off'; } catch (e) { /* private mode */ }
  let tone = null;
  let toneWatchdog = null;
  const TONE_LEVEL = 0.25;   // ToneGenerator's AMPLITUDE

  function audio() {
    if (!audioCtx) {
      const Ctx = window.AudioContext || window.webkitAudioContext;
      if (!Ctx) return null;
      audioCtx = new Ctx();
      // The engine changes state on its own -- suspended by a screen lock,
      // running again after a resume settles -- so let it drive the label
      // rather than waiting for the next telemetry frame.
      //
      // Every departure from 'running' also retires the current audio graph.
      // A phone that suspends the engine for minutes can hand back a context
      // that reports 'running' while the oscillator inside it is finished, and
      // applyTone's key check would keep that corpse forever: the cadence has
      // not changed, so it took the early return and rebuilt nothing. That was
      // the "says Sound is on but there is no sound until I refresh" fault.
      audioCtx.onstatechange = () => {
        if (audioCtx && audioCtx.state !== 'running') audioGen++;
        clockSample = null;   // the clock stops legitimately while suspended
        showSound();
      };
    }
    if (audioCtx.state === 'suspended') audioCtx.resume();
    return audioCtx;
  }

  // A phone suspends the audio engine whenever it locks the screen or the page
  // goes to the background, and nothing brought it back by itself: telemetry
  // kept arriving, the page kept "playing" into a suspended context, and the
  // tone was silently absent -- no warning, nothing on screen -- until the
  // screen happened to be touched again. Resuming is only permitted once the
  // page has been touched at all (the browser's autoplay rule, handled by the
  // pointerdown/click handlers at the end of this file); after that first
  // touch, this puts the engine back whenever it has drifted out.
  //
  // Called on every telemetry frame, so it is throttled -- but on a clock, not
  // on an "in flight" flag. A resume() promise that never settles (iOS does
  // this when it decides the page has no user activation) would leave such a
  // flag stuck on and this function a permanent no-op.
  // Bumped whenever the engine leaves 'running'; a tone stamped with an older
  // generation is stale and must be rebuilt rather than retuned.
  let audioGen = 0;
  let lastResumeAt = 0;
  function wakeAudio() {
    if (!audioCtx || audioCtx.state !== 'suspended') return;
    const now = Date.now();
    if (now - lastResumeAt < 500) return;
    lastResumeAt = now;
    try {
      const r = audioCtx.resume();
      if (r && r.then) r.then(showSound, showSound);
    } catch (e) { /* state stays suspended; the label says so */ }
  }
  // Returning after a long absence. A phone freezes a backgrounded page
  // outright, and the socket can come back half-open: no close event fires, so
  // the reconnect below never runs and the header still reads "connected" while
  // nothing arrives. Silence with a live-looking page was the result. If no
  // telemetry has landed for a few seconds, rebuild the connection from here.
  const STALE_LINK_MS = 4000;
  let lastTelemetryAt = 0;
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) { showSound(); return; }
    clockSample = null;   // the page was frozen; do not read that as a stall
    if (soundOn) wakeAudio();
    showSound();
    if (Date.now() - lastTelemetryAt > STALE_LINK_MS) connect();
  });

  // The box's button click: 1200 Hz sine, 50 ms, 0.20 -- ToneGenerator::playBeep's defaults.
  function playClick() {
    if (!soundOn) return;
    const ctx = audio();
    if (!ctx) return;
    const t0 = ctx.currentTime;
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.frequency.value = 1200;
    gain.gain.setValueAtTime(0, t0);
    gain.gain.linearRampToValueAtTime(0.2, t0 + 0.005);
    gain.gain.setValueAtTime(0.2, t0 + 0.045);
    gain.gain.linearRampToValueAtTime(0, t0 + 0.05);
    osc.connect(gain).connect(ctx.destination);
    osc.start(t0);
    osc.stop(t0 + 0.06);
  }

  // One Beep Assist beep, at the box's own frequency, waveform, length and
  // volume. Scheduled at an absolute time so navigation mode's pair keeps its
  // gap exactly, rather than relying on a page timer the phone may throttle.
  function playBeepAt(b, at) {
    const ctx = audioCtx;
    if (!ctx) return;
    const ms = Number(b.ms) || 0;
    const on = ms / 1000;
    if (on <= 0) return;
    const amp = Number(b.amp) || 0.2;
    const fade = Math.min(0.005, on / 2);
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.type = b.wave === 'triangle' ? 'triangle' : 'sine';
    osc.frequency.value = Number(b.freq_hz) || 0;
    if (!osc.frequency.value) return;
    gain.gain.setValueAtTime(0, at);
    gain.gain.linearRampToValueAtTime(amp, at + fade);
    gain.gain.setValueAtTime(amp, at + Math.max(fade, on - fade));
    gain.gain.linearRampToValueAtTime(0, at + on);
    osc.connect(gain).connect(ctx.destination);
    osc.start(at);
    osc.stop(at + on + 0.02);
  }

  // Beep Assist. The box plays these to its own speaker, which the phone never
  // sees, so it is told each one by sequence number (see BeepEvent in
  // tone_cadence.h) and plays it when the number advances. The first sequence
  // number of a session is recorded and NOT played: a phone connecting
  // mid-rally must not announce a waypoint the car has already passed.
  let lastBeepSeq = null;
  function beepHeard(b) {
    if (!b) return;
    const seq = Number(b.seq) || 0;
    if (lastBeepSeq === null) { lastBeepSeq = seq; return; }
    if (seq <= lastBeepSeq) return;
    lastBeepSeq = seq;
    if (!soundOn) return;
    wakeAudio();
    const ctx = audio();
    if (!ctx) return;
    // A hair ahead of now, so the envelope's first point is never already past.
    const at = ctx.currentTime + 0.02;
    playBeepAt(b, at);
    if (b.twice) playBeepAt(b, at + (Number(b.gap_ms) || 150) / 1000);
  }

  function stopTone() {
    if (!tone) return;
    const t = tone;
    tone = null;
    clearInterval(t.timer);
    const now = audioCtx.currentTime;
    t.gain.gain.cancelScheduledValues(now);
    t.gain.gain.setValueAtTime(t.gain.gain.value, now);
    t.gain.gain.linearRampToValueAtTime(0, now + 0.01);
    t.osc.stop(now + 0.02);
  }

  // One beep of the cadence at an absolute time on the audio clock, with short
  // fades so its edges don't click -- the box's generator fades its edges for
  // the same reason.
  function envelopeAt(t, at) {
    const g = t.gain.gain;
    const on = t.toneMs / 1000;
    const fade = Math.min(0.005, on / 2);
    g.setValueAtTime(0, at);
    g.linearRampToValueAtTime(TONE_LEVEL, at + fade);
    g.setValueAtTime(TONE_LEVEL, at + Math.max(fade, on - fade));
    g.linearRampToValueAtTime(0, at + on);
  }

  // The beeps are queued on the AudioContext's own clock, up to
  // SCHEDULE_AHEAD_S in advance, instead of being drawn one at a time by a page
  // timer. A backgrounded page has its timers throttled to roughly once a
  // second, which broke up the rhythm of every cadence quicker than that -- the
  // 100/100 gentle tick worst of all. The audio clock is not throttled, so the
  // rhythm stays exact and the page timer only has to wake often enough to top
  // the queue up. While the engine is suspended its clock does not advance, so
  // on resume the queue is behind: clamping to currentTime picks the cadence
  // straight back up rather than firing a burst of overdue beeps.
  const SCHEDULE_AHEAD_S = 1.5;
  const SCHEDULE_TICK_MS = 250;

  function topUpSchedule(t) {
    if (!audioCtx || tone !== t) return;
    if (t.nextAt < audioCtx.currentTime) t.nextAt = audioCtx.currentTime;
    while (t.nextAt < audioCtx.currentTime + SCHEDULE_AHEAD_S) {
      envelopeAt(t, t.nextAt);
      t.nextAt += t.periodS;
    }
  }

  // Restarted only when the cadence changes; telemetry repeats it 10 times a
  // second.
  //
  // The pitch is deliberately NOT part of the key. The box retunes without
  // resetting its waveform -- ToneGenerator changes only the phase increment,
  // precisely so a retune makes no click -- and the phone has to match, or the
  // same driving sounds smooth on the box and chopped on the phone. Rebuilding
  // the oscillator for a new frequency cost a 10ms fade-out and a 10ms fade-in
  // every time, which in the simple tone's continuous pitch ramp is a hole in
  // the sound at every change. setValueAtTime on a running oscillator is
  // phase-continuous, so it is the same retune the box does. A change of
  // WAVEFORM still rebuilds: it is rare, and it only happens across the silent
  // quiet band, where there is nothing playing to interrupt.
  function applyTone(c) {
    // audioLive(), not just "a context exists": there is no point building an
    // oscillator on an engine measured to be dead, and treating it as silent
    // keeps the button honest instead of reporting a tone nobody can hear.
    const sounding = soundOn && audioLive() && !!c && c.tone_ms > 0 && c.freq_hz > 0;
    const key = sounding ? [c.tone_ms, c.silence_ms, c.wave].join('|') : '';
    if (tone && tone.gen !== audioGen) {
      // Same cadence, but the graph predates a suspend: rebuild, don't retune.
      stopTone();
    }
    if ((tone ? tone.key : '') === key) {
      if (tone && c && tone.freq !== c.freq_hz) {
        tone.osc.frequency.setValueAtTime(c.freq_hz, audioCtx.currentTime);
        tone.freq = c.freq_hz;
      }
      return;
    }
    stopTone();
    if (!sounding) return;
    const ctx = audio();
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.type = c.wave === 'triangle' ? 'triangle' : 'sine';
    osc.frequency.value = c.freq_hz;
    gain.gain.value = 0;
    osc.connect(gain).connect(ctx.destination);
    osc.start();
    const t = { osc: osc, gain: gain, key: key, freq: c.freq_hz, toneMs: c.tone_ms,
                periodS: (c.tone_ms + c.silence_ms) / 1000, nextAt: ctx.currentTime,
                gen: audioGen, timer: null };
    tone = t;
    if (c.silence_ms > 0) {
      topUpSchedule(t);
      t.timer = setInterval(() => topUpSchedule(t), SCHEDULE_TICK_MS);
    } else {
      // Continuous (the simple tone): fade in and hold.
      gain.gain.setValueAtTime(0, ctx.currentTime);
      gain.gain.linearRampToValueAtTime(TONE_LEVEL, ctx.currentTime + 0.01);
    }
  }

  // Silence rather than a stuck tone if the box stops talking. Telemetry
  // arrives ten times a second, so this only fires on a real gap -- but at one
  // second it was firing on ordinary ones. A phone in a moving car, on the
  // box's own wifi, drops a second of traffic often enough that the tone cut
  // out and came back while the box's speaker played straight through. Three
  // seconds still silences a genuinely dead link within a few beats of the
  // cadence, which is all this is for.
  const TONE_WATCHDOG_MS = 3000;
  function toneHeard(c) {
    lastTelemetryAt = Date.now();
    if (soundOn) wakeAudio();
    checkAudioAlive();
    showSound();
    applyTone(c);
    clearTimeout(toneWatchdog);
    toneWatchdog = setTimeout(stopTone, TONE_WATCHDOG_MS);
  }

  // True only when the audio engine is actually able to make a sound. A
  // browser will not start one until the page has been touched, and the phone
  // suspends it again on every screen lock, so "Sound on" was a lie for the
  // commonest reason the phone is silent.
  // state === 'running' is the browser's INTENT, not proof of output: a phone
  // can hand back a context that says 'running' while its render thread is
  // dead, which is silence with no warning. The engine's own clock is the
  // proof -- a live context advances currentTime in step with wall time, a
  // stalled one freezes it -- so audioStalled below is measured, not assumed.
  function audioLive() {
    return !!audioCtx && audioCtx.state === 'running' && !audioStalled;
  }

  // Sampled on every telemetry frame; judged once a second, because a shorter
  // window cannot tell a stall from ordinary scheduling jitter.
  const STALL_WINDOW_MS = 1000;
  const STALL_RATIO = 0.5;   // less than half of wall time elapsed => stalled
  let clockSample = null;    // { ctx, wall } or null = start a fresh window
  let audioStalled = false;
  function checkAudioAlive() {
    if (!audioCtx || audioCtx.state !== 'running') {
      clockSample = null;
      return;
    }
    const wallNow = Date.now();
    const ctxNow = audioCtx.currentTime;
    if (!clockSample) { clockSample = { ctx: ctxNow, wall: wallNow }; return; }
    const wallElapsed = wallNow - clockSample.wall;
    if (wallElapsed < STALL_WINDOW_MS) return;
    const ctxElapsed = (ctxNow - clockSample.ctx) * 1000;
    clockSample = { ctx: ctxNow, wall: wallNow };
    const stalled = ctxElapsed < wallElapsed * STALL_RATIO;
    if (stalled === audioStalled) return;
    audioStalled = stalled;
    if (stalled) {
      // Retire the graph and ask for a resume. If the engine will not come
      // back by itself the button now says so, and a tap rebuilds it.
      audioGen++;
      stopTone();
      wakeAudio();
    }
    showSound();
  }

  // Called from the telemetry path as well, so it must not touch the DOM unless
  // something actually changed.
  let shownSound = null;
  function showSound() {
    const blocked = soundOn && !audioLive();
    // "Sound is on/off" rather than "Sound on/off": the shorter form reads as a
    // button that will DO that when pressed, which had the crew pressing it to
    // turn sound on when it was already on. "Tap for sound" stays an
    // instruction, because in that state pressing it is exactly what is needed.
    const label = !soundOn ? 'Sound is off' : (blocked ? 'Tap for sound' : 'Sound is on');
    // The engine's own word for what it is doing, shown only while it is not
    // playing: "Tap for sound" says what to do about it, this says why. Without
    // it a phone that refuses to start audio is undiagnosable from here.
    const why = blocked
      ? (!audioCtx ? 'no engine' : (audioStalled ? 'engine stalled' : audioCtx.state))
      : '';
    if (shownSound === label + '|' + why) return;
    shownSound = label + '|' + why;
    const btn = $('sound-toggle');
    btn.textContent = label;
    btn.title = why ? 'audio engine: ' + why : '';
    btn.classList.toggle('off', !soundOn);
    btn.classList.toggle('blocked', blocked);
    const note = $('audio-state');
    if (note) note.textContent = why ? 'audio: ' + why : '';
  }
  $('sound-toggle').addEventListener('click', () => {
    // While it reads "Tap for sound" the button is an instruction, NOT the
    // mute: a press there means "make the sound work". Toggling as well muted
    // the phone on the very press meant to un-silence it, because the
    // page-wide unlock handler and this one both fired on the same tap.
    if (soundOn && !audioLive()) {
      // A context that has gone suspended and will not come back is a known
      // phone trap: the cure is a fresh one built inside the gesture that asked
      // for it, which is this tap. Cheap, and only on an explicit press.
      if (audioCtx && !audioLive()) {
        stopTone();
        try { audioCtx.close(); } catch (e) { /* already closing */ }
        audioCtx = null;
        audioStalled = false;
        clockSample = null;
        audioGen++;
      }
      audio();
      showSound();
      return;
    }
    soundOn = !soundOn;
    try { localStorage.setItem('rallySound', soundOn ? 'on' : 'off'); } catch (e) { /* private mode */ }
    if (soundOn) audio(); else stopTone();
    showSound();
  });
  showSound();

  // Any touch unlocks the audio; every button pressed here clicks, as every
  // button on the box does. Capture phase, so the click sounds before any
  // confirm() the button opens.
  document.addEventListener('pointerdown', () => { if (soundOn) audio(); showSound(); }, true);
  document.addEventListener('click', (e) => {
    if (soundOn) audio();
    if (e.target.closest && e.target.closest('button')) playClick();
    showSound();
  }, true);

  connect();
})();
