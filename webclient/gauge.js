/* Phone Driver tab — recognisable twin of the compact box gauge.
   Maths mirrored from calculations.cpp; draw is canvas, not pixel-Cairo. */
(function (global) {
  'use strict';

  const GAUGE_ZONE_DEAD_BAND_S = 0.5;
  const REF_RADIUS = 256.0;
  const NEEDLE_HALF_WIDTH = 3.0;

  function gaugeEffectiveMaxSeconds(seconds) {
    const abs = Math.abs(seconds);
    if (abs < 3) return 3;
    if (abs > 30) return 30;
    return abs;
  }

  function gaugeZone(seconds) {
    const abs = Math.abs(seconds);
    if (abs < 10) return 0;
    if (abs < 30) return 1;
    return 2;
  }

  function gaugeZoneHysteretic(seconds, previousZone) {
    const plain = gaugeZone(seconds);
    if (previousZone < 0 || previousZone > 2) return plain;
    const abs = Math.abs(seconds);
    if (previousZone === 2 && abs >= 30.0 - GAUGE_ZONE_DEAD_BAND_S) return 2;
    if (previousZone === 1 && abs >= 10.0 - GAUGE_ZONE_DEAD_BAND_S && abs < 30.0) return 1;
    return plain;
  }

  function gaugeArcColor(zone) {
    if (zone === 0) return { r: 0.0, g: 0.7, b: 0.0 };
    if (zone === 2) return { r: 0.8, g: 0.1, b: 0.1 };
    return { r: 0.85, g: 0.65, b: 0.0 };
  }

  function gaugeTickAngle(index, maxVal) {
    const frac = index / maxVal;
    return Math.PI + Math.PI / 2 + frac * (Math.PI / 2);
  }

  function gaugeTickLabel(index) {
    return index === 0 ? '' : String(index);
  }

  function computeNeedleGeometry(seconds, maxSeconds, radius) {
    let clamped = seconds;
    if (clamped > maxSeconds) clamped = maxSeconds;
    if (clamped < -maxSeconds) clamped = -maxSeconds;
    return {
      angle: Math.PI + Math.PI / 2 + (clamped / maxSeconds) * (Math.PI / 2),
      length: (radius - 10) * 0.95,
      halfWidth: NEEDLE_HALF_WIDTH
    };
  }

  function computeCompactGaugeLayout(width, height) {
    const L = {};
    L.radius = Math.min(width / 2 - 25, height - 95);
    L.centerX = width / 2;
    L.centerY = height - 75;
    L.fscale = Math.min(1.0, L.radius / REF_RADIUS);
    L.valSize = 44 * L.fscale;
    L.curTopSize = 50 * L.fscale;
    L.labelSize = Math.max(12, 16 * L.fscale);
    L.rowGap = 48 * L.fscale;
    L.rightAnchor = L.centerX + L.radius * 0.72;
    L.distanceAnchor = L.radius * 0.72 + 10.0;
    L.bandOuterX = L.centerX + (L.radius + 6.0);
    L.bandTargetX = L.centerX - (L.radius + 6.0);
    L.bandTopY = L.centerY - (L.radius + 6.0);
    L.boxHeight = 50.0;
    L.boxY = L.centerY - 10;
    const lineGap = L.rowGap + 10 * L.fscale;
    L.tripBaseline = L.boxY - (0.22 * L.curTopSize + 6 * L.fscale);
    L.totalBaseline = L.tripBaseline - lineGap;
    L.footSize = Math.max(11, 14 * L.fscale);
    L.footBaseline = L.boxY + L.boxHeight;
    return L;
  }

  function formatGaugeDigital(seconds, zone) {
    const abs = Math.abs(seconds);
    const sign = seconds < 0 ? '-' : '+';
    if (zone === 2) {
      const total = Math.round(abs);
      const h = Math.floor(total / 3600);
      const m = Math.floor((total % 3600) / 60);
      const s = total % 60;
      if (h > 0) {
        return sign + h + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
      }
      return sign + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
    }
    return sign + abs.toFixed(1);
  }

  function rgb(c) {
    return 'rgb(' + Math.round(c.r * 255) + ',' + Math.round(c.g * 255) + ',' + Math.round(c.b * 255) + ')';
  }

  function DriverGauge(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.zoneShown = 0;
    this.last = {
      ahead_behind_s: 0,
      cur_kph: 0,
      target_kph: 0,
      total_avg_kph: 0,
      trip_avg_kph: 0,
      total_m: 0,
      trip_m: 0,
      rally_clock: '--:--:--',
      units: 'kph'
    };
  }

  DriverGauge.prototype.resize = function () {
    const parent = this.canvas.parentElement;
    const cssW = Math.max(280, parent.clientWidth || window.innerWidth);
    // Dial only — Total/Trip sit in the HTML footer below, so leave them room.
    const cssH = Math.max(280, Math.min(window.innerHeight * 0.55, cssW * 0.85));
    const dpr = window.devicePixelRatio || 1;
    this.canvas.style.width = cssW + 'px';
    this.canvas.style.height = cssH + 'px';
    this.canvas.width = Math.round(cssW * dpr);
    this.canvas.height = Math.round(cssH * dpr);
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.cssW = cssW;
    this.cssH = cssH;
    this.draw();
  };

  DriverGauge.prototype.update = function (msg) {
    this.last = Object.assign({}, this.last, msg);
    this.draw();
  };

  DriverGauge.prototype.draw = function () {
    const ctx = this.ctx;
    const width = this.cssW || this.canvas.clientWidth;
    const height = this.cssH || this.canvas.clientHeight;
    if (!width || !height) return;

    const msg = this.last;
    const seconds = Number(msg.ahead_behind_s) || 0;
    const maxVal = gaugeEffectiveMaxSeconds(seconds);
    this.zoneShown = gaugeZoneHysteretic(seconds, this.zoneShown);
    const zone = this.zoneShown;
    const arc = gaugeArcColor(zone);
    const L = computeCompactGaugeLayout(width, height);
    const centerX = L.centerX;
    const centerY = L.centerY;
    const radius = L.radius;
    const fscale = L.fscale;

    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, width, height);

    // Bezel
    ctx.strokeStyle = 'rgb(64,64,64)';
    ctx.lineWidth = 4;
    ctx.beginPath();
    ctx.arc(centerX, centerY, radius + 18, Math.PI, 2 * Math.PI);
    ctx.stroke();

    // Arc background
    ctx.strokeStyle = 'rgb(31,31,31)';
    ctx.lineWidth = 28;
    ctx.beginPath();
    ctx.arc(centerX, centerY, radius, Math.PI, 2 * Math.PI);
    ctx.stroke();

    // Coloured arc
    ctx.strokeStyle = rgb(arc);
    ctx.lineWidth = 12;
    ctx.beginPath();
    ctx.arc(centerX, centerY, radius, Math.PI, 2 * Math.PI);
    ctx.stroke();

    // Ticks
    const labelsVisible = zone === 0;
    const tickCount = Math.floor(maxVal);
    ctx.strokeStyle = 'rgb(230,230,230)';
    ctx.fillStyle = 'rgb(230,230,230)';
    ctx.lineWidth = NEEDLE_HALF_WIDTH * 2;
    ctx.font = 'bold ' + (24 * fscale) + 'px monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    for (let i = -tickCount; i <= tickCount; i++) {
      const angle = gaugeTickAngle(i, maxVal);
      const x1 = centerX + (radius - 20) * Math.cos(angle);
      const y1 = centerY + (radius - 20) * Math.sin(angle);
      const x2 = centerX + (radius + 8) * Math.cos(angle);
      const y2 = centerY + (radius + 8) * Math.sin(angle);
      ctx.beginPath();
      ctx.moveTo(x1, y1);
      ctx.lineTo(x2, y2);
      ctx.stroke();
      if (!labelsVisible) continue;
      const label = gaugeTickLabel(i);
      if (!label) continue;
      const labelR = radius - 38;
      const lx = centerX + labelR * Math.cos(angle);
      const ly = centerY + labelR * Math.sin(angle);
      ctx.save();
      ctx.translate(lx, ly);
      ctx.rotate(angle + Math.PI / 2);
      ctx.fillText(label, 0, 0);
      ctx.restore();
    }

    // Top triangle at zero
    ctx.beginPath();
    const triY = centerY - radius - 12;
    ctx.moveTo(centerX, triY + 10);
    ctx.lineTo(centerX - 6, triY);
    ctx.lineTo(centerX + 6, triY);
    ctx.closePath();
    ctx.fill();

    // Needle
    const needle = computeNeedleGeometry(seconds, maxVal, radius);
    const dirX = Math.cos(needle.angle);
    const dirY = Math.sin(needle.angle);
    const perpX = -Math.sin(needle.angle);
    const perpY = Math.cos(needle.angle);
    const tipX = centerX + needle.length * dirX;
    const tipY = centerY + needle.length * dirY;
    ctx.fillStyle = 'rgb(230,230,230)';
    ctx.beginPath();
    ctx.moveTo(tipX + needle.halfWidth * perpX, tipY + needle.halfWidth * perpY);
    ctx.lineTo(tipX - needle.halfWidth * perpX, tipY - needle.halfWidth * perpY);
    ctx.lineTo(centerX - needle.halfWidth * perpX, centerY - needle.halfWidth * perpY);
    ctx.lineTo(centerX + needle.halfWidth * perpX, centerY + needle.halfWidth * perpY);
    ctx.closePath();
    ctx.fill();
    ctx.beginPath();
    ctx.arc(centerX, centerY, 8, 0, 2 * Math.PI);
    ctx.fill();

    // Digital ahead/behind box
    const digital = formatGaugeDigital(seconds, zone);
    ctx.font = 'bold ' + L.valSize + 'px monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const textW = ctx.measureText(digital).width;
    const boxW = Math.max(180, textW + 24) * 1.1;
    const boxH = L.valSize + 20;
    const boxCenterY = L.boxY + L.boxHeight / 2;
    const boxY = boxCenterY - boxH / 2;
    const boxX = centerX - boxW / 2;
    ctx.fillStyle = '#000';
    ctx.fillRect(boxX, boxY, boxW, boxH);
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 3;
    ctx.strokeRect(boxX, boxY, boxW, boxH);
    ctx.fillStyle = '#fff';
    ctx.fillText(digital, centerX, boxCenterY);

    // Current (right) / Target (left)
    const cur = Number(msg.cur_kph || 0).toFixed(1);
    const tgt = Number(msg.target_kph || 0).toFixed(1);
    ctx.font = 'bold ' + L.curTopSize + 'px sans-serif';
    ctx.textBaseline = 'alphabetic';

    ctx.fillStyle = '#7ec8e3';
    ctx.textAlign = 'right';
    ctx.fillText(cur, L.bandOuterX, L.bandTopY + L.curTopSize * 0.8);
    ctx.font = L.labelSize + 'px sans-serif';
    ctx.fillText('Current', L.bandOuterX, L.bandTopY + L.curTopSize * 0.8 + L.labelSize + 4);

    ctx.fillStyle = '#FFDD00';
    ctx.textAlign = 'left';
    ctx.font = 'bold ' + L.curTopSize + 'px sans-serif';
    ctx.fillText(tgt, L.bandTargetX, L.bandTopY + L.curTopSize * 0.8);
    ctx.font = L.labelSize + 'px sans-serif';
    ctx.fillText('Target', L.bandTargetX, L.bandTopY + L.curTopSize * 0.8 + L.labelSize + 4);

    // Total / Trip live in the HTML footer under the canvas (RB-WEB-11).

    // Rally clock top-right (driver overlay) — larger for glanceability.
    ctx.fillStyle = '#fff';
    ctx.font = 'bold ' + Math.max(26, 34 * fscale) + 'px monospace';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'top';
    ctx.fillText(msg.rally_clock || '--:--:--', width - 8, 6);
  };

  global.RallyDriverGauge = DriverGauge;
})(window);
