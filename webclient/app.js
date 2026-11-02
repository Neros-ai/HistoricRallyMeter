(function () {
  'use strict';

  let ws = null;
  let reconnectTimer = null;
  let units = 'kph';
  let distanceKm = false;   // Settings distance units: 00.00 km, or 0,000 m
  let lastArrival = null;
  function renderArrival() {
    $('arr-unit').textContent = units === 'mph' ? 'mph' : 'kph';
    $('arr-kph').textContent = lastArrival
      ? (units === 'mph' ? Math.round(lastArrival.kph * 0.621371) : lastArrival.kph) : '--';
  }
  // The last state message, kept so a units change arriving on telemetry can
  // re-title the stage panel without waiting for the next state broadcast.
  let lastState = null;
  let lastTelemetry = null;
  let activeTab = 'navi';
  let driverGauge = null;
  let frozen = false;       // Navi freeze: the box and every phone hold together (set from the state message)
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

  // Same figures as the box's segments page. Kilometres are two decimals
  // (rounded). Metres stay whole, with thousands separators.
  function formatDist(m) {
    m = Number(m) || 0;
    if (distanceKm) return (Math.floor(m / 10 + 0.5) / 100).toFixed(2);
    return Math.round(m).toLocaleString();
  }

  // What an entry shows, and the metres to send back. Entries stay plain
  // numbers; the labels carry the thousands separators.
  function stageInput(m) {
    m = Number(m) || 0;
    if (distanceKm) return (Math.floor(m / 10 + 0.5) / 100).toFixed(2);
    return String(Math.round(m));
  }

  function stageMetres(text) {
    let t = String(text == null ? '' : text).trim().replace(/km/ig, '').replace(/m/ig, '').replace(/\s/g, '');
    if (!t) return null;
    if (distanceKm) {
      t = t.replace(',', '.');
      const v = Number(t);
      if (!Number.isFinite(v) || v < 0) return null;
      return Math.round(v * 1000);
    }
    t = t.replace(/,/g, '');
    const v = Number(t);
    if (!Number.isFinite(v) || v < 0) return null;
    return Math.round(v);
  }

  function showDist(el, m) {
    if (!el) return;
    el.textContent = formatDist(m);
    el.dataset.unit = distanceKm ? 'km' : 'm';
  }

  // True when the unit changed, so the caller can redraw figures that
  // were painted under the other unit.
  function useDistanceUnit(km) {
    const next = !!km;
    const changed = next !== distanceKm;
    distanceKm = next;
    const active = document.activeElement;
    document.querySelectorAll('input[name="distance-unit"]').forEach((r) => {
      if (r !== active) r.checked = (r.value === 'km') === distanceKm;
    });
    const add = $('seg-add-text');
    if (add && add !== active) add.placeholder = distanceKm ? '1.00;1.50;2.30' : '1,000;1,500;2,300';
    if (changed && lastState) {
      renderSegments(lastState);
      renderStagePanel(lastState);
    }
    return changed;
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

  // As the box: seconds alone under a minute, then m:ss, then h:mm:ss, with tenths.
  function offsetText(ms) {
    const tenths = Math.round(Math.abs(ms) / 100);
    const total = Math.floor(tenths / 10), t = tenths % 10;
    const p = (n) => String(n).padStart(2, '0');
    const sign = ms < 0 ? '-' : '+';
    if (total < 60) return sign + total + '.' + t;
    if (total < 3600) return sign + Math.floor(total / 60) + ':' + p(total % 60) + '.' + t;
    return sign + Math.floor(total / 3600) + ':' + p(Math.floor(total / 60) % 60) + ':' + p(total % 60) + '.' + t;
  }

  function applyTelemetry(msg) {
    lastTelemetry = msg;
    lastTelemetryAt = Date.now();
    tzMin = Number(msg.tz_min) || 0;
    $('rally-clock').textContent = msg.rally_clock || '--:--:--';
    const previousUnits = units;
    units = msg.units || 'kph';
    if (typeof msg.distance_km === 'boolean') useDistanceUnit(msg.distance_km);
    if (units !== previousUnits && lastState) renderStagePanel(lastState);
    if (units !== previousUnits) renderArrival();

    // Freeze stops the box counting distance, so these figures already hold.
    showDist($('trip-m'), msg.trip_m);
    showDist($('total-m'), msg.total_m);
    // Surrey: navi total in its own row, and the last time taken under Trip history.
    const sur = !!msg.surrey;
    const italianNow = Number(msg.rally_type) === 2;
    $('row-navi').style.display = (sur || italianNow) ? '' : 'none';
    $('time-taken-row').style.display = (sur && msg.time_taken) ? '' : 'none';
    if (sur || italianNow) showDist($('navi-m'), msg.navi_m);
    if (sur) {
      $('time-taken-label').textContent = 'time taken ss' + (Number(msg.time_taken_stage) >= 1 ? msg.time_taken_stage : '-');
      $('time-taken').textContent = msg.time_taken || '';
    }
    renderSector(msg);
    // Italian: the box's count down text, red past the line (RB-WEB-21).
    const ic = msg.it_count;
    if (ic) {
      $('next-m').textContent = ic.km;
      delete $('next-m').dataset.unit;
    } else if (msg.next_m === null || msg.next_m === undefined) {
      $('next-m').textContent = '---';
      delete $('next-m').dataset.unit;
    } else {
      showDist($('next-m'), msg.next_m);
    }
    $('next-m').style.color = (ic && ic.past) ? '#ff2626' : '';
    $('next-s').textContent = (ic && ic.s) ? ic.s : '';
    $('next-s').style.display = (ic && ic.s) ? '' : 'none';
    $('next-s').style.color = (ic && ic.s_past) ? '#ff2626' : '';
    $('total-el').textContent = mmss(msg.total_s);
    $('trip-el').textContent = mmss(msg.trip_s);
    $('speed-change').textContent = msg.speed_change || '';
    if (msg.next_key) $('btn-next').textContent = msg.next_key;
    if (msg.prev_key && $('btn-prev')) $('btn-prev').textContent = msg.prev_key;
    // Driver tab footer (HTML under the gauge — keeps dial clear).
    showDist($('drv-trip-m'), msg.trip_m);
    showDist($('drv-total-m'), msg.total_m);
    const avgF = units === 'mph' ? 0.621371 : 1;
    $('drv-trip-avg').textContent = (Number(msg.trip_avg_kph || 0) * avgF).toFixed(1);
    $('drv-total-avg').textContent = (Number(msg.total_avg_kph || 0) * avgF).toFixed(1);
    document.querySelectorAll('.avg-unit').forEach((e) => { e.textContent = units; });

    // Italian blanks the next-minute and autostart keys, and the stage panel's
    // auto/manual line. A-B, B-C zero is only the Greek measure-type key.
    // Seg hides Distance at start and Autostart on Italian, as the box does.
    {
      const italian = Number(msg.rally_type) === 2;
      const show = (el, on) => { if (el) el.style.display = on ? '' : 'none'; };
      show($('btn-flag-minute'), !italian);
      show($('btn-flag-auto'), !italian);
      show($('navi-auto'), !italian);
      show($('row-zero'), Number(msg.rally_type) === 0 && !!msg.measure_ab_bc);
      show($('seg-auto-row'), !italian);
      show($('seg-start-dist-label'), !italian);
      show($('seg-duration-label'), italian);
      show($('seg-duration-avg'), italian);
      syncSystemChoices(msg.rally_type, msg.measure_ab_bc, !!(lastState && lastState.stage_locked));
    }

    $('btn-next').disabled = !msg.next_enabled;
    $('btn-prev').disabled = !msg.prev_enabled;
    $('btn-reverse').classList.toggle('lit', !!msg.reverse);
    setWhoop(!!msg.reverse || frozen);
    $('btn-flag-late').innerHTML = (lastState && (lastState.total_reset_asks || lastState.stage_abortable))
      ? 'stage<br>abort' : '<svg class="flag" viewBox="0 0 40 34"><use href="#flag"/></svg> now';
    $('btn-freeze').classList.toggle('lit', frozen);
    setWhoop(!!(lastTelemetry && lastTelemetry.reverse) || frozen);

    // Stage arrival
    lastArrival = msg.arrival || null;
    renderArrival();
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
  // Rally type, measure type, and the Italian beep lock. Skips the control
  // under the finger so a tap is not overwritten by the next frame.
  function syncSystemChoices(rallyType, measureAb, stageLocked) {
    const active = document.activeElement;
    const rallyName = Number(rallyType) === 2 ? 'italian' : Number(rallyType) === 1 ? 'surrey' : 'greek';
    const pick = (name, val) => document.querySelectorAll('input[name="' + name + '"]').forEach((r) => {
      if (r !== active) r.checked = String(r.value) === String(val);
    });
    if (rallyType !== undefined && rallyType !== null) pick('rally-type', rallyName);
    if (typeof measureAb === 'boolean') pick('measure-type', measureAb ? 'abbc' : 'abac');
    document.querySelectorAll('input[name="rally-type"]').forEach((r) => { r.disabled = !!stageLocked; });
    const beepLocked = Number(rallyType) === 2;
    ['beep-nav', 'beep-timing', 'beep-advance-m', 'beep-advance-s'].forEach((id) => {
      const el = $(id);
      if (el) el.disabled = beepLocked;
    });
  }

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
    radios('gauge-range', Number(state.gauge_range) || 3);
    radios('units', units);
    syncSystemChoices(state.rally_type, state.measure_ab_bc, !!state.stage_locked);
    if (typeof state.distance_km === 'boolean' && useDistanceUnit(state.distance_km) && lastTelemetry) {
      lastTelemetry.distance_km = distanceKm;
      applyTelemetry(lastTelemetry);
    }
    if ($('reaction-offset')) {   // read-only: set on the box
      const ro = Number(state.reaction_offset_ms) || 0;
      $('reaction-offset').textContent = (ro < 0 ? '\u2212' : ro > 0 ? '+' : '') + (Math.abs(ro) / 1000).toFixed(2) + ' s';
    }
    setIf($('set-stage-log'), (el) => { el.checked = !!state.stage_logging; });
    setIf($('set-sensor-log'), (el) => { el.checked = !!state.sensor_logging; });
    setIf($('set-force-single'), (el) => { el.checked = !!state.force_single; });
    $('set-url').textContent = state.web_url || '-';
    if ($('bt-name')) $('bt-name').textContent = state.bt_name || 'no speaker remembered';
    if ($('setup-status')) $('setup-status').textContent = state.setup_status || '';
    renderWifi(state.wifi);
    // Calibration
    $('cal-pulses').textContent = state.calibration_pulses;
    $('cal-s1').classList.toggle('lit', !state.sensors_both);
    $('cal-s12').classList.toggle('lit', !!state.sensors_both);
    $('cal-s1').textContent = 'sensor 1';
    $('cal-s12').textContent = 'sensor 1&2';
    setIf($('cal-prop-n'), (el) => { el.value = state.prop_pulses; });
    // Navi
    frozen = !!state.frozen;
    $('btn-freeze').classList.toggle('lit', frozen);
    setWhoop(!!(lastTelemetry && lastTelemetry.reverse) || frozen);
    const hist = (state.trip_history || []);
    $('trip-history').textContent = hist.length ? hist.map((m) => formatDist(m) + (distanceKm ? ' km' : ' m')).join(' \u00b7 ') : '-';
    const sa = state.autostart || 'none';
    const armed = sa !== 'none';
    $('auto-word').textContent = armed ? 'auto' : 'manual';
    $('auto-word').classList.toggle('orange', !armed);
    $('auto-word').classList.toggle('cyan', armed);
    $('auto-time').textContent = armed ? sa : (state.start_time || '');
    $('flag-minute-time').textContent = nextMinuteText();
    $('btn-flag-auto').classList.toggle('lit', !!state.autostart_on);
  }

  function hms(seconds) {
    const t = Math.max(0, Math.round(Number(seconds) || 0));
    const h = Math.floor(t / 3600), m = Math.floor(t / 60) % 60, s = t % 60;
    return h + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
  }

  function renderSector(msg) {
    const card = $('sector-card');
    const greek = $('greek-cards');
    const italian = Number(msg.rally_type) === 2;
    if (greek) greek.style.display = italian ? 'none' : '';
    if (!card) return;
    card.style.display = italian ? '' : 'none';
    if (!italian) return;
    const s = msg.sector;
    if (!s || !s.loaded) { card.textContent = 'no sector loaded'; return; }
    const tag = (name, type) => '<b class="tag t' + Number(type) + '">' + (name || '') + '</b>';
    let html = '<table><tr><th></th><th>km</th><th>progr</th><th>mm:ss</th><th>km/h</th></tr>';
    html += '<tr><td>' + tag(s.in_tag, s.in_type) + '</td><td>' + (s.in_km || '') + '</td><td class="cyan">'
      + (s.in_progr || '') + '</td><td class="cyan">' + (s.in_mmss || '') + '</td><td>' + (s.in_kph || '') + '</td></tr>';
    html += '<tr><td>count<br>down</td><td class="' + (s.count_past ? 'late' : '') + '">' + (s.count_km || '')
      + '</td><td></td><td class="' + (s.count_s_past ? 'late' : '') + '">' + (s.count_s || '')
      + '</td><td>' + (s.avg || '') + '</td></tr>';
    if (s.next_tag) {
      html += '<tr class="dim"><td>' + tag(s.next_tag, s.next_type) + '</td><td>' + (s.next_km || '')
        + '</td><td>' + (s.next_progr || '') + '</td><td>' + (s.next_mmss || '') + '</td><td>' + (s.next_kph || '') + '</td></tr>';
    }
    html += '</table>';
    if (s.pc_ref) html += '<p>error pc' + s.pc_ref + ' ' + s.pc_s + ' s &nbsp; ' + s.pc_m + ' m</p>';
    if (s.co_hhmm) html += '<p>co' + (s.co_ref || '') + ' arrival ' + s.co_hhmm + ' &nbsp; ' + s.co_kph + ' kph &nbsp; ' + s.co_min + ' min</p>';
    if (s.ahead_min !== undefined && s.ahead_min !== null) html += '<p>+/&minus; ' + s.ahead_min + '</p>';
    card.innerHTML = html;
  }

  function renderWifi(list) {
    const box = $('wifi-nets');
    if (!box || !list) return;
    box.textContent = '';
    list.forEach((n) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'key' + (n.active ? ' lit' : '');
      b.dataset.net = n.name;
      b.textContent = (n.hotspot ? 'hotspot ' : 'join ') + n.name;
      box.appendChild(b);
    });
  }

  function nextMinuteText() {
    const ms = lastTelemetry ? Number(lastTelemetry.computer_ms) + Number(lastTelemetry.rally_offset_ms || 0) : Date.now();
    const d = new Date((Math.floor(ms / 60000) + 1) * 60000 + tzMin * 60000);
    return String(d.getUTCHours()).padStart(2, '0') + ':' + String(d.getUTCMinutes()).padStart(2, '0');
  }

  // The loaded stage's first rows (the running one's current segment lit), as on Home.
  function renderStagePanel(state) {
    $('stage-speed-head').textContent = units === 'mph' ? 'MPH' : 'KPH';
    if ($('stage-at-head')) $('stage-at-head').textContent = distanceKm ? 'at km' : 'at m';
    const tbody = $('stage-table').querySelector('tbody');
    tbody.innerHTML = '';
    // Speed changes only, as on the box: row 1 and every row that starts a new speed.
    const rows = [];
    (state.rows || []).forEach((r, i) => { if (i === 0 || r.chg) rows.push({ r: r, idx: i }); });
    const cur = Number(state.segment_current_number);
    let curChange = 0;
    rows.forEach((x, c) => { if (x.idx <= cur) curChange = c; });
    const first = curChange > 0 ? Math.min(curChange - 1, Math.max(0, rows.length - 3)) : 0;
    rows.slice(first, first + 3).forEach((x, i) => {
      const r = x.r;
      const tr = document.createElement('tr');
      if (cur >= 0 && first + i === curChange) tr.className = 'now';
      const speed = Number(r.kph || 0) * (units === 'mph' ? 0.621371 : 1);
      tr.innerHTML = '<td>' + speed.toFixed(1) + '</td><td>' + formatDist(r.from) + '</td>';
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
    const tbody = $('seg-table').querySelector('tbody');
    // A field is being typed in: do not rebuild under it, but bring every other figure up to
    // date in place (a changed "to m" moves the next row's "from m", the distances and times).
    const unit = distanceKm ? 'km' : 'm';
    if ($('seg-h-from')) $('seg-h-from').textContent = 'from ' + unit;
    if ($('seg-h-to')) $('seg-h-to').textContent = 'to ' + unit;
    if ($('seg-h-dist')) $('seg-h-dist').textContent = 'dist ' + unit;
    if (active && active.closest && active.closest('#seg-table')) {
      const rows = state.rows || [];
      const trs = tbody.children;
      if (trs.length !== rows.length) return;
      const setText = (td, text) => { if (td && !td.querySelector('input') && td.textContent !== text) td.textContent = text; };
      const setVal = (el, v) => { if (el && el !== active) el.value = v; };
      for (let i = 0; i < rows.length; i++) {
        const r = rows[i], tds = trs[i].children;
        if (!!trs[i].querySelector('.spd') !== !!r.chg) return;   // structure changed: wait for the field to be left
        if (i === 0) setVal(trs[i].querySelector('.first'), r.n); else setText(tds[0], String(r.n));
        if (i === 0) setVal(trs[i].querySelector('.first-from'), stageInput(r.from)); else setText(tds[1], formatDist(r.from));
        setVal(trs[i].querySelector('.to'), stageInput(r.to));
        setText(tds[3], formatDist(r.dist));
        setVal(trs[i].querySelector('.spd'), r.kph || '');
        setText(tds[6], r.chg && r.kph > 0 ? String(Math.round(r.kph / 3.6)) : '');
        setText(tds[8], mmss(r.time_s));
        setText(tds[9], mmss(r.total_s));
      }
      return;
    }
    tbody.innerHTML = '';
    // A committed edit lets go of its field, so the next state redraws the table.
    const done = (el) => { setTimeout(() => el.blur(), 0); };
    (state.rows || []).forEach((r, i) => {
      const tr = document.createElement('tr');
      tr.innerHTML =
        (i === 0 ? '<td><input type="number" step="1" min="0" inputmode="numeric" pattern="[0-9]*" class="first" value="' + r.n + '"></td>'
                 : '<td class="dim">' + r.n + '</td>') +
        (i === 0 ? '<td><input type="number" step="' + (distanceKm ? '0.01' : '1') + '" min="0" inputmode="' + (distanceKm ? 'decimal' : 'numeric') + '" class="first-from" value="' + stageInput(r.from) + '"></td>'
                 : '<td class="dim">' + formatDist(r.from) + '</td>') +
        '<td><input type="number" step="' + (distanceKm ? '0.01' : '1') + '" min="0" inputmode="' + (distanceKm ? 'decimal' : 'numeric') + '" class="to" value="' + stageInput(r.to) + '"></td>' +
        '<td class="dim">' + formatDist(r.dist) + '</td>' +
        '<td><input type="checkbox" class="chg"' + (r.chg ? ' checked' : '') + (i === 0 ? ' disabled' : '') + '></td>' +
        (r.chg ? '<td><input type="number" step="0.1" inputmode="decimal" class="spd" value="' + (r.kph || '') + '"></td>'
               : '<td class="dim">&quot;</td>') +
        '<td class="dim">' + (r.chg && r.kph > 0 ? Math.round(r.kph / 3.6) : '') + '</td>' +
        '<td>' + (i >= 1 ? '<input type="checkbox" class="auto"' + (r.auto_on ? ' checked' : '') +
                  (r.auto_av ? '' : ' disabled') + '>' : '') + '</td>' +
        '<td class="dim">' + mmss(r.time_s) + '</td><td class="dim">' + mmss(r.total_s) + '</td>' +
        '<td><button type="button" class="del">&times;</button></td>';
      const chg = tr.querySelector('.chg');
      if (chg) chg.addEventListener('change', () => send({ type: 'row_chg', row: i, on: chg.checked }));
      const firstEl = tr.querySelector('.first');
      if (firstEl) firstEl.addEventListener('change', () => {
        send({ type: 'stage_first', number: Number(firstEl.value) || 0, from_m: Number($('seg-first-from').value) || 0 });
        done(firstEl);
      });
      const fromEl = tr.querySelector('.first-from');
      if (fromEl) fromEl.addEventListener('change', () => {
        const metres = stageMetres(fromEl.value);
        if (metres == null) return;
        send({ type: 'stage_first', number: Number($('seg-first-n').value) || 0, from_m: metres });
        done(fromEl);
      });
      const toEl = tr.querySelector('.to');
      toEl.addEventListener('change', () => {
        const metres = stageMetres(toEl.value);
        if (metres == null) return;
        send({ type: 'row_to', row: i, to_m: metres });
        done(toEl);
      });
      const spd = tr.querySelector('.spd');
      if (spd) spd.addEventListener('change', () => {
        send({ type: 'row_speed', row: i, kph: Number(spd.value) || 0 });
        done(spd);
      });
      const au = tr.querySelector('.auto');
      if (au) au.addEventListener('change', () => send({ type: 'row_auto', row: i, on: au.checked }));
      tr.querySelector('.del').addEventListener('click', () => send({ type: 'row_delete', row: i }));
      tbody.appendChild(tr);
    });
    const setIf = (el, v) => { if (el !== active) el.value = v; };
    setIf($('seg-pick'), String(state.edit_slot || 1));
    setIf($('seg-start-time'), state.start_time || '');
    setIf($('seg-start-dist'), state.start_distance_m || '');
    const durS = Number(state.sector_duration_s);
    setIf($('seg-duration'), durS > 0 ? hms(durS) : '');
    if ($('seg-duration-avg')) {
      const rows = state.rows || [];
      const endM = rows.length ? Number(rows[rows.length - 1].to) : 0;
      $('seg-duration-avg').textContent = (durS > 0 && endM > 0)
        ? 'Average ' + (endM / 1000 / (durS / 3600)).toFixed(2) : '';
    }
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
      renderUpload(msg);
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
  // "set" pins Total to a roadbook figure: it asks for the figure. Cancel or an
  // empty answer is not a request to pin Total to zero -- press total to reset.
  $('btn-dist-set').addEventListener('click', () => {
    const answer = prompt(distanceKm ? 'Set Total to (km):' : 'Set Total to (metres):', '');
    if (answer === null || answer.trim() === '') return;
    const meters = stageMetres(answer);
    if (meters == null || meters > 999999) return;
    send({ type: 'distance_set', meters: meters });
  });
  $('btn-next').addEventListener('click', () => send({ type: 'next' }));
  $('btn-prev').addEventListener('click', () => send({ type: 'prev' }));
  $('btn-freeze').addEventListener('click', () => send({ type: 'freeze_toggle' }));
  $('btn-reverse').addEventListener('click', () => send({ type: 'reverse_set', on: !(lastTelemetry && lastTelemetry.reverse) }));
  $('btn-flag-minute').addEventListener('click', () => {
    // A stage already running: ask first, as the box does.
    const running = !!(lastState && lastState.total_reset_asks);
    if (running && !confirm('Abort the current stage?')) return;
    send({ type: 'flag_next_minute', abort: running });
  });
  // Stage go now -- or, with a stage armed or running, stage abort (as on the box).
  const stageActive = () => !!(lastState && (lastState.total_reset_asks || lastState.stage_abortable));
  $('btn-flag-late').addEventListener('click', () => {
    if (stageActive()) {
      if (confirm('ABORT the stage?')) send({ type: 'abort_stage' });
      return;
    }
    send({ type: 'flag_late_arrival' });
  });
  // Scheduled start: arm or cancel the autostart at the loaded stage's start time.
  $('btn-flag-auto').addEventListener('click', () => send({ type: 'autostart_set', on: !(lastState && lastState.autostart_on) }));
  $('btn-zero').addEventListener('click', () => {
    if (confirm('Zero the needle here?\nOK = zero now, Cancel = leave it')) send({ type: 'driver_zero', reset: false });
  });
  // No new stage can be loaded while one is live, as on the box.
  const okToChangeStage = () => {
    if (!(lastState && lastState.stage_locked)) return true;
    alert('New stage cannot be loaded while stage is running');
    return false;
  };
  // Stage numbers are the organiser's own, 1-99.
  ['stage-pick', 'seg-pick'].forEach((id) => {
    const sel = $(id);
    sel.innerHTML = '';
    for (let n = 1; n <= 99; n++) sel.appendChild(new Option(String(n), String(n)));
  });
  function stageStep(dir) {
    if (!okToChangeStage()) return;
    const cur = Number($('stage-pick').value) || 1;
    const pop = (lastState && lastState.memory_populated) || [];
    for (let s = cur + dir; s >= 1 && s <= 99; s += dir) {
      if (pop[s - 1]) { send({ type: 'memory_recall', slot: s }); return; }
    }
  }
  $('stage-minus').addEventListener('click', () => stageStep(-1));
  $('stage-plus').addEventListener('click', () => stageStep(1));
  $('stage-pick').addEventListener('change', (e) => {
    if (!okToChangeStage()) {   // put the picker back on the stage that is loaded
      const t = lastTelemetry && Number(lastTelemetry.stage_no);
      if (t >= 1) e.target.value = String(t);
      return;
    }
    send({ type: 'memory_recall', slot: Number(e.target.value) });
  });

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
  // Drv tab: tapping Total or Trip resets it, as on Navi (same questions).
  $('drv-box-total').addEventListener('click', resetTotal);
  $('drv-box-trip').addEventListener('click', resetTrip);

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
  // No stage can be edited while one is live, as on the box: any touch of an entry, key or
  // tick on this page is stopped and explained, and the page is drawn again from the box.
  const segLocked = () => !!(lastState && lastState.stage_locked);
  const segGuard = (e) => {
    if (!segLocked()) return;
    const t = e.target;
    if (!t.closest || !t.closest('input, select, button')) return;
    e.preventDefault();
    e.stopPropagation();
    if (t.blur) t.blur();
    const now = Date.now();
    if (now - (segGuard.last || 0) > 1000) { segGuard.last = now; alert('Stages cannot be edited while a stage is running'); }
    if (lastState) renderSegments(lastState);
  };
  ['focusin', 'click'].forEach((ev) => $('view-seg').addEventListener(ev, segGuard, true));
  // - / + step to the next programmed stage, skipping gaps (as the box); the
  // list itself takes any number 1-99, so a new stage can be started on an empty one.
  const segStep = (dir) => {
    const cur = Number($('seg-pick').value) || 1;
    const pop = (lastState && lastState.memory_populated) || [];
    for (let n = cur + dir; n >= 1 && n <= 99; n += dir) {
      if (pop[n - 1]) { send({ type: 'edit_select', slot: n }); return; }
    }
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
  $('seg-duration').addEventListener('change', (e) => send({ type: 'sector_duration', text: e.target.value.trim() }));
  $('btn-clear-stage').addEventListener('click', () => { if (confirm('Clear this stage?')) send({ type: 'clear_stage' }); });
  $('btn-clear-all').addEventListener('click', () => { if (confirm('Clear every stage?')) send({ type: 'clear_all' }); });

  // Upload stage: the phone checks the file with the box, then loads it.
  // Nothing is written on check. The box key opens the same file window.
  let uploadId = '';
  let uploadShown = 0;
  function markUploadFormat() {
    const name = lastTelemetry && Number(lastTelemetry.rally_type) === 2 ? 'italian'
      : lastTelemetry && Number(lastTelemetry.rally_type) === 1 ? 'surrey' : 'greek';
    document.querySelectorAll('#upload-window .fmt').forEach((el) => {
      el.classList.toggle('fit', el.dataset.rally === name);
    });
  }
  function openUpload(on) {
    $('upload-window').classList.toggle('hidden', !on);
    if (on) markUploadFormat();
    if (!on) {
      uploadId = '';
      $('upload-refusal').textContent = '';
      $('upload-file-name').textContent = '';
      $('upload-ask').classList.add('hidden');
      $('upload-stages').textContent = '';
      $('upload-table').querySelector('tbody').textContent = '';
    }
  }
  function renderUpload(state) {
    const up = state.upload;
    if (!up || $('upload-window').classList.contains('hidden')) return;
    if (up.loaded && up.loaded.length && (!up.id || up.id === uploadId)) {
      openUpload(false);
      showTab('seg');
      return;
    }
    if (up.id && up.id !== uploadId) return;
    $('upload-refusal').textContent = up.refusal || '';
    const stages = up.stages || [];
    const loadable = stages.some((s) => !(s.errors && s.errors.length));
    $('upload-load').disabled = !loadable || !!up.refusal;
    $('upload-ask').classList.add('hidden');
    const box = $('upload-stages');
    box.textContent = '';
    stages.forEach((s) => {
      const b = document.createElement('button');
      b.type = 'button';
      const problem = (s.errors && s.errors[0]) || (s.warnings && s.warnings[0]) || '';
      b.textContent = 'stage ' + s.stage + ' ' + (s.mode || '') + '  ' + s.rows + ' rows'
        + (s.replaces ? '  replaces' : '') + (problem ? '  ' + problem : '');
      if (s.replaces) b.classList.add('upload-amb');
      if (s.errors && s.errors.length) b.classList.add('upload-bad');
      if (s.stage === uploadShown) b.classList.add('on');
      b.addEventListener('click', () => { uploadShown = s.stage; renderUploadTable(s); box.querySelectorAll('button').forEach((x) => x.classList.remove('on')); b.classList.add('on'); });
      box.appendChild(b);
    });
    const shown = stages.find((s) => s.stage === uploadShown) || stages[0];
    if (shown) { uploadShown = shown.stage; renderUploadTable(shown); }
  }
  function renderUploadTable(stage) {
    const body = $('upload-table').querySelector('tbody');
    body.textContent = '';
    (stage.table || []).forEach((r) => {
      const tr = document.createElement('tr');
      const name = r.type ? (r.type + (r.ref ? r.ref : '')) : String(r.box);
      [name, r.from_m, r.to_m, r.kph, r.dur_s].forEach((v) => {
        const td = document.createElement('td');
        td.textContent = v;
        tr.appendChild(td);
      });
      body.appendChild(tr);
    });
  }
  $('btn-upload-stage').addEventListener('click', () => { showTab('seg'); openUpload(true); });
  $('upload-file').addEventListener('change', () => {
    const f = $('upload-file').files && $('upload-file').files[0];
    if (!f) return;
    $('upload-file-name').textContent = f.name;
    const reader = new FileReader();
    reader.onload = () => { $('upload-text').value = String(reader.result || ''); };
    reader.readAsText(f);
  });
  $('upload-check').addEventListener('click', () => {
    uploadId = Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
    uploadShown = 0;
    send({ type: 'stage_upload_check', id: uploadId, text: $('upload-text').value });
  });
  const sendLoad = (replace) => {
    if (!uploadId) return;
    send({ type: 'stage_upload_load', id: uploadId, replace: replace });
  };
  $('upload-load').addEventListener('click', () => {
    const q = lastState && lastState.upload && lastState.upload.question;
    if (q) {
      $('upload-ask-q').textContent = q;
      $('upload-ask').classList.remove('hidden');
      return;
    }
    sendLoad(true);
  });
  $('upload-replace').addEventListener('click', () => sendLoad(true));
  $('upload-keep').addEventListener('click', () => { $('upload-ask').classList.add('hidden'); sendLoad(false); });
  $('upload-cancel').addEventListener('click', () => {
    if (uploadId) send({ type: 'stage_upload_cancel', id: uploadId });
    openUpload(false);
  });

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
  document.querySelectorAll('input[name="gauge-range"]').forEach((r) =>
    r.addEventListener('change', () => { if (r.checked) send({ type: 'gauge_range', seconds: Number(r.value) }); }));
  document.querySelectorAll('input[name="units"]').forEach((r) =>
    r.addEventListener('change', () => { if (r.checked) send({ type: 'setting', name: 'units_mph', on: r.value === 'mph' }); }));
  document.querySelectorAll('input[name="distance-unit"]').forEach((r) =>
    r.addEventListener('change', () => { if (r.checked) send({ type: 'setting', name: 'distance_km', on: r.value === 'km' }); }));
  document.querySelectorAll('input[name="rally-type"]').forEach((r) =>
    r.addEventListener('change', () => {
      if (!r.checked) return;
      const n = r.value === 'italian' ? 2 : r.value === 'surrey' ? 1 : 0;
      send({ type: 'rally_type', n: n });
    }));
  document.querySelectorAll('input[name="measure-type"]').forEach((r) =>
    r.addEventListener('change', () => { if (r.checked) send({ type: 'setting', name: 'measure_ab_bc', on: r.value === 'abbc' }); }));
  $('reaction-minus').addEventListener('click', () => send({ type: 'reaction_step', ms: -10 }));
  $('reaction-plus').addEventListener('click', () => send({ type: 'reaction_step', ms: 10 }));
  $('set-stage-log').addEventListener('change', (e) => send({ type: 'setting', name: 'stage_logging', on: e.target.checked }));
  $('set-sensor-log').addEventListener('change', (e) => send({ type: 'setting', name: 'sensor_logging', on: e.target.checked }));
  $('set-force-single').addEventListener('change', (e) => send({ type: 'setting', name: 'force_single', on: e.target.checked }));
  $('bt-connect').addEventListener('click', () => send({ type: 'box_setup', name: 'bt_connect' }));
  $('bt-disconnect').addEventListener('click', () => send({ type: 'box_setup', name: 'bt_disconnect' }));
  $('wifi-list').addEventListener('click', () => send({ type: 'box_setup', name: 'wifi_list' }));
  $('reset-layout').addEventListener('click', () => {
    if (confirm('Reset the box screen layout?')) send({ type: 'box_setup', name: 'reset_layout' });
  });
  $('wifi-nets').addEventListener('click', (e) => {
    const b = e.target.closest('button');
    if (!b || !b.dataset.net) return;
    if (!confirm('Switch the box to ' + b.dataset.net + '? Phones using the hotspot can lose the box.')) return;
    send({ type: 'box_setup', name: 'wifi_use', net: b.dataset.net });
  });
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

  // Reverse: the box's repeating whoop (392 Hz triangle, 250 ms, 0.40, every 700 ms),
  // played here too while the car is reversing or the figures are frozen. Stops if the link goes quiet.
  let whoopTimer = null;
  function whoopOnce() {
    if (!soundOn || Date.now() - lastTelemetryAt > STALE_LINK_MS) return;
    wakeAudio();
    const ctx = audio();
    if (ctx) playBeepAt({ ms: 250, amp: 0.4, wave: 'triangle', freq_hz: 392 }, ctx.currentTime + 0.02);
  }
  function setWhoop(on) {
    if (on && whoopTimer === null) {
      whoopOnce();
      whoopTimer = setInterval(whoopOnce, 700);
    } else if (!on && whoopTimer !== null) {
      clearInterval(whoopTimer);
      whoopTimer = null;
    }
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
    g.linearRampToValueAtTime(t.level, at + fade);
    g.setValueAtTime(t.level, at + Math.max(fade, on - fade));
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
    osc.type = (c.wave === 'triangle' || c.wave === 'square') ? c.wave : 'sine';
    osc.frequency.value = c.freq_hz;
    gain.gain.value = 0;
    osc.connect(gain).connect(ctx.destination);
    osc.start();
    // A square wave of the same peak is much louder; the box trims it to 0.6.
    const t = { level: c.wave === 'square' ? TONE_LEVEL * 0.6 : TONE_LEVEL, osc: osc, gain: gain, key: key, freq: c.freq_hz, toneMs: c.tone_ms,
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
