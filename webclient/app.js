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
    $('unit-label2').textContent = units;
    $('cur-kph').textContent = Number(msg.cur_kph || 0).toFixed(1);
    $('target-kph').textContent = Number(msg.target_kph || 0).toFixed(1);
    $('trip-m').textContent = formatDist(msg.trip_m);
    $('total-m').textContent = formatDist(msg.total_m);
    $('trip-avg').textContent = 'avg ' + Number(msg.trip_avg_kph || 0).toFixed(1);
    $('total-avg').textContent = 'avg ' + Number(msg.total_avg_kph || 0).toFixed(1);
    $('seg-num').textContent = msg.segment_number || 0;
    $('seg-count').textContent = msg.segment_count || 0;

    const btn = $('btn-next-prev');
    btn.textContent = msg.next_prev_label || '--->';
    btn.disabled = !msg.next_prev_enabled;
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

  $('btn-next-prev').addEventListener('click', () => send({ type: 'next_prev' }));
  $('btn-reset-trip').addEventListener('click', resetTrip);
  $('btn-reset-trip2').addEventListener('click', resetTrip);
  $('btn-reset-total').addEventListener('click', () => {
    if (confirm('Reset total distance?')) send({ type: 'reset_total' });
  });

  function resetTrip() {
    if (confirm('Reset trip distance?')) send({ type: 'reset_trip' });
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

  connect();
})();
