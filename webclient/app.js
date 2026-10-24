(function () {
  'use strict';

  let ws = null;
  let reconnectTimer = null;
  let units = 'kph';
  // The last state message, kept so a units change arriving on telemetry can
  // re-title the stage panel without waiting for the next state broadcast.
  let lastState = null;
  let lastTelemetry = null;
  let activeTab = 'navi';
  let driverGauge = null;
  let frozen = false;       // Navi freeze: this phone's figures hold (the box's driver display never does)
  let tzMin = 0;            // the box's zone, so its clock reads the same here

  const $ = (id) => document.getElementById(id);
  const TABS = ['navi', 'seg', 'time', 'cal', 'set', 'drv'];

  function ensureDriverGauge() {
    if (driverGauge) return driverGauge;
    const canvas = $('driver-gauge');
    if (!canvas || typeof RallyDriverGauge !== 'function') return null;
    driverGauge = new RallyDriverGauge(canvas);
    driverGauge.resize();
    return driverGauge;
  }

  function showTab(name) {
    activeTab = name;
    TABS.forEach((t) => {
      $('tab-' + t).classList.toggle('active', t === name);
      $('view-' + t).classList.toggle('hidden', t !== name);
    });
    if (name === 'drv') {
      const g = ensureDriverGauge();
      if (g) g.resize();
    }
  }

  // ---- Log downloads (foot of Set) ----
  // Plain HTTP, not the WebSocket: GET /api/logs for the list, GET /logs/<name>
  // to download. Stage logs first, then sensor logs, newest first.
  function formatSize(bytes) {
    bytes = Number(bytes) || 0;
    if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
    if (bytes >= 1024) return Math.round(bytes / 1024) + ' KB';
    return bytes + ' B';
  }

  function fillLogList(ul, files) {
    ul.textContent = '';
    files.forEach((f) => {
      const li = document.createElement('li');
      const a = document.createElement('a');
      a.href = '/logs/' + encodeURIComponent(f.name);
      a.setAttribute('download', f.name);
      const name = document.createElement('span');
      name.className = 'log-name';
      name.textContent = f.name;
      const size = document.createElement('span');
      size.className = 'log-size';
      size.textContent = formatSize(f.size);
      a.appendChild(name);
      a.appendChild(size);
      li.appendChild(a);
      ul.appendChild(li);
    });
  }

  function refreshLogs() {
    const empty = $('logs-empty');
    fetch('/api/logs', { cache: 'no-store' })
      .then((r) => r.json())
      .then((files) => {
        fillLogList($('logs-stage'), files.filter((f) => f.kind === 'stage'));
        fillLogList($('logs-sensor'), files.filter((f) => f.kind !== 'stage'));
        empty.textContent = 'No log files yet.';
        empty.classList.toggle('hidden', files.length > 0);
      })
      .catch(() => {
        empty.textContent = 'Could not reach the meter.';
        empty.classList.remove('hidden');
      });
  }

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
    // From 100,000 m the box shows km with two decimals and no unit.
    if (Math.abs(m) >= 100000) return (m / 1000).toFixed(2);
    return Math.round(m).toLocaleString();
  }

  function mmss(s) {
    s = Math.max(0, Math.round(Number(s) || 0));
    return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
  }

  // "hh:mm:ss.t" of an epoch time in the box's zone.
  function clockText(ms) {
    const d = new Date(ms + tzMin * 60000);
    const p = (n, w) => String(n).padStart(w || 2, '0');
    return p(d.getUTCHours()) + ':' + p(d.getUTCMinutes()) + ':' + p(d.getUTCSeconds()) + '.' +
           Math.floor(d.getUTCMilliseconds() / 100);
  }

  function offsetText(ms) {
    const tenths = Math.round(Math.abs(ms) / 100);
    return (ms < 0 ? '-' : '+') + Math.floor(tenths / 10) + '.' + (tenths % 10);
  }

  function applyTelemetry(msg) {
    lastTelemetry = msg;
    lastTelemetryAt = Date.now();
    tzMin = Number(msg.tz_min) || 0;
    $('rally-clock').textContent = msg.rally_clock || '--:--:--';
    const previousUnits = units;
    units = msg.units || 'kph';
    if (units !== previousUnits && lastState) renderStagePanel(lastState);

    if (!frozen) {
      const ab = Number(msg.ahead_behind_s) || 0;
      $('ahead-value').textContent = (ab >= 0 ? '+' : '') + ab.toFixed(1) + ' s';
      const abEl = $('ahead-behind');
      abEl.classList.toggle('ahead', ab > 0.05);
      abEl.classList.toggle('behind', ab < -0.05);
      $('unit-label').textContent = units;
      $('cur-kph').textContent = Number(msg.cur_kph || 0).toFixed(1);
      $('target-kph').textContent = Number(msg.target_kph || 0).toFixed(1);
      $('trip-m').textContent = formatDist(msg.trip_m);
      $('total-m').textContent = formatDist(msg.total_m);
      $('total-el').textContent = mmss(msg.total_s);
      $('trip-el').textContent = mmss(msg.trip_s);
      $('trip-avg').textContent = Number(msg.trip_avg_kph || 0).toFixed(1);
      $('total-avg').textContent = Number(msg.total_avg_kph || 0).toFixed(1);
      $('next-m').textContent = (msg.next_m === null || msg.next_m === undefined) ? '---' : formatDist(msg.next_m);
      $('speed-change').textContent = msg.speed_change || '';
    }
    // Driver tab footer (HTML under the gauge — keeps dial clear).
    $('drv-trip-m').textContent = formatDist(msg.trip_m) + ' m';
    $('drv-total-m').textContent = formatDist(msg.total_m) + ' m';
    $('drv-trip-avg').textContent = Number(msg.trip_avg_kph || 0).toFixed(1);
    $('drv-total-avg').textContent = Number(msg.total_avg_kph || 0).toFixed(1);

    $('btn-next').disabled = !msg.next_enabled;
    $('btn-prev').disabled = !msg.prev_enabled;
    $('btn-reverse').classList.toggle('lit', !!msg.reverse);
    $('btn-freeze').classList.toggle('lit', frozen);

    // Stage arrival
    $('arr-kph').textContent = msg.arrival ? msg.arrival.kph : '--';
    $('arr-min').textContent = msg.arrival ? msg.arrival.min : '--';
    const sp = $('stage-pick');
    if (document.activeElement !== sp && msg.stage_no >= 1) sp.value = String(msg.stage_no);

    // Time tab
    const rallyMs = Number(msg.computer_ms) + Number(msg.rally_offset_ms || 0);
    $('t-rally').textContent = clockText(rallyMs);
    $('t-computer').textContent = clockText(Number(msg.computer_ms));
    $('t-offset').textContent = offsetText(Number(msg.rally_offset_ms) || 0);
    $('t-date').textContent = new Date(rallyMs + tzMin * 60000).toUTCString().slice(0, 16);
    const phoneEl = $('t-phone');
    phoneEl.textContent = clockText(Date.now() - 0);   // this phone's clock, in the box's zone
    $('btn-computer-phone').disabled = !msg.phone_known;

    // Cal tab
    if (msg.cal) {
      $('cal-dist').textContent = Number(msg.cal.dist_m).toFixed(1);
      $('cal-c1').textContent = msg.cal.c1;
      $('cal-c2').textContent = msg.cal.c2;
      $('cal-avg').textContent = msg.cal.avg;
      $('cal-rpm').textContent = msg.cal.rpm >= 0 ? msg.cal.rpm : '---';
    }

    // Set tab
    $('set-fps').textContent = (msg.fps || '').replace(/^fps:\s*/, '') + ' fps';
    $('set-cpu').textContent = msg.cpu || '';

    toneHeard(msg.tone);
    beepHeard(msg.beep);

    // Driver canvas: only paint while that tab is open (keeps the others cheap).
    if (activeTab === 'drv') {
      const g = ensureDriverGauge();
      if (g) g.update(msg);
    } else if (driverGauge) {
      driverGauge.last = Object.assign({}, driverGauge.last, msg);
    }
  }

  // Reflects the box's own settings back into the controls. Skips whichever
  // field has focus so an incoming update cannot yank a value out from under
  // someone mid-edit on the phone.
  function applySetState(state) {
    const active = document.activeElement;
    const setIf = (el, apply) => { if (el && el !== active) apply(el); };
    setIf($('beep-nav'), (el) => { el.checked = !!state.beep_navigation_mode; });
    setIf($('beep-timing'), (el) => { el.checked = !!state.beep_timing_mode; });
    setIf($('beep-advance-m'), (el) => { el.value = Number(state.beep_advance_m) || 0; });
    setIf($('beep-advance-s'), (el) => { el.value = Number(state.beep_advance_s) || 0; });
    setIf($('tone-enabled'), (el) => { el.checked = !!state.tone_enabled; });
    const radios = (name, val) => document.querySelectorAll('input[name="' + name + '"]').forEach((r) => {
      if (r !== active) r.checked = String(r.value) === String(val);
    });
    radios('tone-behind', Number(state.tone_type_behind) || 1);
    radios('tone-ahead', Number(state.tone_type_ahead) || 1);
    radios('units', units);
    setIf($('set-stage-log'), (el) => { el.checked = !!state.stage_logging; });
    setIf($('set-sensor-log'), (el) => { el.checked = !!state.sensor_logging; });
    setIf($('set-force-single'), (el) => { el.checked = !!state.force_single; });
    $('set-url').textContent = state.web_url || '-';
    // Calibration
    $('cal-pulses').textContent = state.calibration_pulses;
    $('cal-s1').classList.toggle('lit', !state.sensors_both);
    $('cal-s12').classList.toggle('lit', !!state.sensors_both);
    $('cal-s1').textContent = state.sensors_both ? 'select sensor 1' : 'using sensor 1';
    $('cal-s12').textContent = state.sensors_both ? 'using sensor 1&2' : 'select sensor 1&2';
    setIf($('cal-prop-n'), (el) => { el.value = state.prop_pulses; });
    // Navi
    const hist = (state.trip_history || []);
    $('trip-history').textContent = hist.length ? hist.map((m) => Number(m).toLocaleString()).join('  ') : '-';
    const sa = state.autostart || 'none';
    const armed = sa !== 'none';
    $('auto-word').textContent = armed ? 'auto' : 'manual';
    $('auto-word').classList.toggle('orange', !armed);
    $('auto-word').classList.toggle('cyan', armed);
    $('auto-time').textContent = armed ? sa : (state.start_time || '');
    $('flag-minute-time').textContent = nextMinuteText();
  }

  function nextMinuteText() {
    const ms = lastTelemetry ? Number(lastTelemetry.computer_ms) + Number(lastTelemetry.rally_offset_ms || 0) : Date.now();
    const d = new Date((Math.floor(ms / 60000) + 1) * 60000 + tzMin * 60000);
    return String(d.getUTCHours()).padStart(2, '0') + ':' + String(d.getUTCMinutes()).padStart(2, '0');
  }

  // The loaded stage's first rows (the running one's current segment lit), as on Home.
  function renderStagePanel(state) {
    $('stage-speed-head').textContent = units === 'mph' ? 'MPH' : 'KPH';
    const tbody = $('stage-table').querySelector('tbody');
    tbody.innerHTML = '';
    const rows = state.rows || [];
    const cur = Math.max(0, (Number(state.segment_current_number) || 0));
    const first = cur > 0 ? Math.min(cur - 1, Math.max(0, rows.length - 3)) : 0;
    rows.slice(first, first + 3).forEach((r, i) => {
      const tr = document.createElement('tr');
      if (first + i === state.segment_current_number) tr.className = 'now';
      const speed = Number(r.kph || 0) * (units === 'mph' ? 0.621371 : 1);
      tr.innerHTML = '<td>' + speed.toFixed(1) + '</td><td>' + Number(r.from).toLocaleString() + '</td>';
      tbody.appendChild(tr);
    });
    if (!rows.length) {
      const tr = document.createElement('tr');
      tr.innerHTML = '<td colspan="2" class="stage-empty">(no segments set)</td>';
      tbody.appendChild(tr);
    }
  }

  // ---- Seg tab ----
  function renderSegments(state) {
    const active = document.activeElement;
    // Do not rebuild under a field being typed in.
    if (active && active.closest && active.closest('#seg-table')) return;
    const tbody = $('seg-table').querySelector('tbody');
    tbody.innerHTML = '';
    (state.rows || []).forEach((r, i) => {
      const tr = document.createElement('tr');
      tr.innerHTML =
        '<td>' + r.n + '</td>' +
        '<td>' + Number(r.to).toLocaleString() + '</td>' +
        '<td>' + Number(r.dist).toLocaleString() + '</td>' +
        '<td><input type="checkbox" class="chg"' + (r.chg ? ' checked' : '') + (i === 0 ? ' disabled' : '') + '></td>' +
        (r.chg ? '<td><input type="number" step="0.1" class="spd" value="' + (r.kph || '') + '"></td>'
               : '<td class="dim">&quot;</td>') +
        '<td>' + (i >= 1 ? '<input type="checkbox" class="auto"' + (r.auto_on ? ' checked' : '') +
                  (r.auto_av ? '' : ' disabled') + '>' : '') + '</td>' +
        '<td class="dim">' + mmss(r.time_s) + '</td><td class="dim">' + mmss(r.total_s) + '</td>' +
        '<td><button type="button" class="del">&times;</button></td>';
      const chg = tr.querySelector('.chg');
      if (chg) chg.addEventListener('change', () => send({ type: 'row_chg', row: i, on: chg.checked }));
      const spd = tr.querySelector('.spd');
      if (spd) spd.addEventListener('change', () => send({ type: 'row_speed', row: i, kph: Number(spd.value) || 0 }));
      const au = tr.querySelector('.auto');
      if (au) au.addEventListener('change', () => send({ type: 'row_auto', row: i, on: au.checked }));
      tr.querySelector('.del').addEventListener('click', () => send({ type: 'row_delete', row: i }));
      tbody.appendChild(tr);
    });
    const setIf = (el, v) => { if (el !== active) el.value = v; };
    setIf($('seg-pick'), String(state.edit_slot || 1));
    setIf($('seg-start-time'), state.start_time || '');
    setIf($('seg-start-dist'), state.start_distance_m || '');
    setIf($('seg-first-n'), state.first_number);
    setIf($('seg-first-from'), state.first_from_m);
    if ($('seg-autostart') !== active) $('seg-autostart').checked = !!state.autostart_on;
  }

  function onMessage(ev) {
    let msg;
    try { msg = JSON.parse(ev.data); } catch { return; }
    if (msg.type === 'telemetry') applyTelemetry(msg);
    if (msg.type === 'state') {
      lastState = msg;
      renderSegments(msg);
      applySetState(msg);
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

  TABS.forEach((t) => $('tab-' + t).addEventListener('click', () => showTab(t)));
  window.addEventListener('resize', () => {
    if (activeTab === 'drv' && driverGauge) driverGauge.resize();
  });

  // Heartbeat: this phone's clock, for the Time tab and "set computer to phone time".
  setInterval(() => send({ type: 'phone_time', ms: Date.now() }), 2000);

  // ---- Navi ----
  $('btn-dist-minus').addEventListener('click', () => send({ type: 'distance_adjust', delta_m: -10 }));
  $('btn-dist-plus').addEventListener('click', () => send({ type: 'distance_adjust', delta_m: 10 }));
  $('btn-dist-set').addEventListener('click', () => {
    const field = $('dist-set-m');
    // An empty box is someone who has not typed a figure yet, not a request
    // to pin Total to zero -- press total to reset.
    if (field.value.trim() === '') return;
    const meters = Number(field.value);
    if (!Number.isFinite(meters) || meters < 0) return;
    send({ type: 'distance_set', meters: meters });
    field.value = '';
    field.blur();
  });
  $('btn-next').addEventListener('click', () => send({ type: 'next' }));
  $('btn-prev').addEventListener('click', () => send({ type: 'prev' }));
  $('btn-freeze').addEventListener('click', () => { frozen = !frozen; $('btn-freeze').classList.toggle('lit', frozen); });
  $('btn-reverse').addEventListener('click', () => send({ type: 'reverse_set', on: !(lastTelemetry && lastTelemetry.reverse) }));
  $('btn-flag-minute').addEventListener('click', () => send({ type: 'flag_next_minute' }));
  $('btn-flag-late').addEventListener('click', () => send({ type: 'flag_late_arrival' }));
  $('btn-zero').addEventListener('click', () => {
    if (confirm('Zero the needle here?\nOK = zero now, Cancel = leave it')) send({ type: 'driver_zero', reset: false });
  });
  function stageStep(dir) {
    const cur = Number($('stage-pick').value) || 1;
    const pop = (lastState && lastState.memory_populated) || [];
    for (let s = cur + dir; s >= 1 && s <= 5; s += dir) {
      if (pop[s - 1]) { send({ type: 'memory_recall', slot: s }); return; }
    }
  }
  $('stage-minus').addEventListener('click', () => stageStep(-1));
  $('stage-plus').addEventListener('click', () => stageStep(1));
  $('stage-pick').addEventListener('change', (e) => send({ type: 'memory_recall', slot: Number(e.target.value) }));

  // With a stage running, the box's own question (a missed start): the
  // distance zero is the press, so the box is told at once and the reset
  // applied only if the crew confirm.
  function resetTotal() {
    if (lastState && lastState.total_reset_asks) {
      send({ type: 'reset_total_capture' });
      $('stage-menu').classList.remove('hidden');
      return;
    }
    if (confirm('Reset total distance?')) send({ type: 'reset_total' });
  }
  // The box's trip key never asks, so neither does the phone.
  function resetTrip() { send({ type: 'reset_trip' }); }
  $('btn-total').addEventListener('click', resetTotal);
  $('btn-trip').addEventListener('click', resetTrip);

  const closeStageMenu = () => $('stage-menu').classList.add('hidden');
  $('menu-abort').addEventListener('click', () => {
    closeStageMenu();
    send({ type: 'reset_total', choice: 'confirm' });
  });
  // Back: the box is holding the position of the press, so it has to be told
  // the press is abandoned (its own message: see the box's cancelTotalResetPress).
  $('menu-cancel').addEventListener('click', () => {
    closeStageMenu();
    send({ type: 'reset_total_cancel' });
  });

  // ---- Seg ----
  const segStep = (dir) => {
    const cur = Number($('seg-pick').value) || 1;
    const n = cur + dir;
    if (n >= 1 && n <= 5) send({ type: 'edit_select', slot: n });
  };
  $('seg-minus').addEventListener('click', () => segStep(-1));
  $('seg-plus').addEventListener('click', () => segStep(1));
  $('seg-pick').addEventListener('change', (e) => send({ type: 'edit_select', slot: Number(e.target.value) }));
  $('btn-add-seg').addEventListener('click', () => {
    const text = $('seg-add-text').value.trim();
    if (!text) return;
    send({ type: 'rows_add', text: text });
    $('seg-add-text').value = '';
  });
  const sendStart = () => send({ type: 'stage_start', time: $('seg-start-time').value.trim(),
                                 distance_m: Number($('seg-start-dist').value) || 0 });
  $('seg-start-time').addEventListener('change', sendStart);
  $('seg-start-dist').addEventListener('change', sendStart);
  const sendFirst = () => send({ type: 'stage_first', number: Number($('seg-first-n').value) || 0,
                                 from_m: Number($('seg-first-from').value) || 0 });
  $('seg-first-n').addEventListener('change', sendFirst);
  $('seg-first-from').addEventListener('change', sendFirst);
  $('seg-autostart').addEventListener('change', (e) => send({ type: 'autostart_set', on: e.target.checked }));
  $('btn-clear-stage').addEventListener('click', () => { if (confirm('Clear this stage?')) send({ type: 'clear_stage' }); });
  $('btn-clear-all').addEventListener('click', () => { if (confirm('Clear every stage?')) send({ type: 'clear_all' }); });

  // ---- Time ----
  $('trim-minus').addEventListener('click', () => send({ type: 'rally_trim', steps: -1 }));
  $('trim-plus').addEventListener('click', () => send({ type: 'rally_trim', steps: 1 }));
  $('btn-rally-computer').addEventListener('click', () => send({ type: 'rally_to_computer' }));
  $('btn-computer-phone').addEventListener('click', () => send({ type: 'computer_to_phone' }));
  $('btn-rally-entered').addEventListener('click', () => {
    const t = $('t-enter').value.trim();
    if (t) send({ type: 'rally_to_entered', time: t });
  });

  // ---- Cal ----
  $('cal-zero').addEventListener('click', () => send({ type: 'cal_zero' }));
  $('cal-save').addEventListener('click', () => {
    const m = Number($('cal-actual').value);
    if (Number.isFinite(m) && m >= 500 && m <= 100000) { send({ type: 'cal_save', meters: m }); $('cal-actual').value = ''; }
  });
  $('cal-s1').addEventListener('click', () => send({ type: 'cal_sensor', both: false }));
  $('cal-s12').addEventListener('click', () => send({ type: 'cal_sensor', both: true }));
  $('cal-reset').addEventListener('click', () => {
    const n = Number($('cal-reset-n').value);
    if (Number.isFinite(n) && n > 0) send({ type: 'cal_reset', pulses: n });
  });
  $('cal-prop-n').addEventListener('change', (e) => send({ type: 'prop_pulses', n: Number(e.target.value) || 0 }));

  // ---- Set ----
  function sendBeep(patch) { send(Object.assign({ type: 'beep_set' }, patch)); }
  $('beep-nav').addEventListener('change', (e) => sendBeep({ navigation: e.target.checked }));
  $('beep-timing').addEventListener('change', (e) => sendBeep({ timing: e.target.checked }));
  // Committed on blur, not per keystroke: mid-typing a number is a real value.
  $('beep-advance-m').addEventListener('change', (e) => sendBeep({ advance_m: Number(e.target.value) || 0 }));
  $('beep-advance-s').addEventListener('change', (e) => sendBeep({ advance_s: Number(e.target.value) || 0 }));
  $('tone-enabled').addEventListener('change', (e) => send({ type: 'tone_set', enabled: e.target.checked }));
  document.querySelectorAll('input[name="tone-behind"]').forEach((r) =>
    r.addEventListener('change', () => { if (r.checked) send({ type: 'tone_set', tone_type_behind: Number(r.value) }); }));
  document.querySelectorAll('input[name="tone-ahead"]').forEach((r) =>
    r.addEventListener('change', () => { if (r.checked) send({ type: 'tone_set', tone_type_ahead: Number(r.value) }); }));
  document.querySelectorAll('input[name="units"]').forEach((r) =>
    r.addEventListener('change', () => { if (r.checked) send({ type: 'setting', name: 'units_mph', on: r.value === 'mph' }); }));
  $('set-stage-log').addEventListener('change', (e) => send({ type: 'setting', name: 'stage_logging', on: e.target.checked }));
  $('set-sensor-log').addEventListener('change', (e) => send({ type: 'setting', name: 'sensor_logging', on: e.target.checked }));
  $('set-force-single').addEventListener('change', (e) => send({ type: 'setting', name: 'force_single', on: e.target.checked }));
  $('btn-download-logs').addEventListener('click', () => {
    $('logs-block').classList.toggle('hidden');
    if (!$('logs-block').classList.contains('hidden')) refreshLogs();
  });
  $('btn-logs-refresh').addEventListener('click', refreshLogs);

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
