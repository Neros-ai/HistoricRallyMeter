/* Phone Driver tab — recognisable twin of the compact box gauge.
   Needle geometry mirrored from calculations.cpp; every figure, the colour band and the scale
   are the box's own (telemetry drv.shown). Draw is canvas, not pixel-Cairo. */
(function (global) {
  'use strict';

  const REF_RADIUS = 256.0;
  const NEEDLE_HALF_WIDTH = 3.0;   // ticks
  const NEEDLE_BAR_HALF_WIDTH = 5.0;   // Nero2.0: the needle is 10 px thick

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
      halfWidth: NEEDLE_BAR_HALF_WIDTH
    };
  }

  function computeCompactGaugeLayout(width, height, boxMode) {
    const L = {};
    L.radius = Math.min(width / 2 - 25, height - 95);
    L.centerX = width / 2;
    L.fscale = Math.min(1.0, L.radius / REF_RADIUS);
    L.boxHeight = 64.0 * L.fscale;
    if (boxMode) {
      // The dedicated driver window (800×480). Fonts are the box's, and the
      // readout sits over the hub. No embedded-hub drop.
      L.centerY = height - 75;
      L.valSize = 60 * L.fscale;
      L.curTopSize = 62 * L.fscale;
      L.boxFontSize = 50 * L.fscale;
      L.labelGap = 8 * L.fscale;
      L.boxY = L.centerY - 8 * L.fscale;
    } else {
      // Distances are the footer. The readout row sits just under the hub.
      L.boxHeight = 78.0 * L.fscale;   // portrait: a taller row, so the figures do not brush the edges
      L.centerY = height - (L.boxHeight + 20);
      L.valSize = 50 * L.fscale;
      L.curTopSize = 60;   // fixed, not scaled with the dial
      L.boxFontSize = L.valSize;
      L.labelGap = 4;
      L.boxY = L.centerY + 12;
    }
    L.legendDrop = 20 * L.fscale;
    L.labelSize = Math.max(12, 16 * L.fscale);
    L.rowGap = 48 * L.fscale;
    L.bandTopY = L.centerY - (L.radius + 6.0);
    L.speedBase = L.bandTopY - 14;
    L.footSize = Math.max(11, 14 * L.fscale);
    L.footBaseline = L.boxY + L.boxHeight;
    return L;
  }

  function avgLegendRight(baseRight, numberLeft, gap) {
    const need = numberLeft - gap;
    return need < baseRight ? need : baseRight;
  }

  function captionLeftAfter(baseLeft, numberRight, gap) {
    const need = numberRight + gap;
    return need > baseLeft ? need : baseLeft;
  }

  function domText(id) {
    const el = document.getElementById(id);
    return el ? el.textContent : '';
  }

  // Landscape only. The same anchors as ui_driver.cpp: averages on the left of
  // the strip, distances on the right, captions beside them. Reads the footer
  // strings, which applyTelemetry has already formatted for this frame.
  function drawBoxReadouts(ctx, L, drv, msg, abBoxX, abBoxY, abBoxW, fs, italianFig) {
    const page = drv ? Number(drv.page) : 4;
    const lcx = abBoxX + abBoxW / 2;
    const avgAnchor = abBoxX + 8 * fs;
    const distAnchor = lcx + 260 * fs;
    const y1 = abBoxY - 93 * fs;
    const y2 = abBoxY - 27 * fs;
    const totalAvg = domText('drv-total-avg');
    const tripAvg = domText('drv-trip-avg');
    const totalM = domText('drv-total-m');
    const tripM = domText('drv-trip-m');
    const unitEl = document.getElementById('drv-total-m');
    const distUnit = (unitEl && unitEl.dataset.unit) || 'km';
    const showTrip = page !== 1;
    const unitLow = (!italianFig && msg.units === 'mph') ? 'mph' : 'kph';
    const drawAt = (text, anchor, baseline, color) => {
      ctx.fillStyle = color;
      ctx.font = 'bold ' + L.valSize + 'px monospace';
      ctx.textAlign = 'right';
      ctx.textBaseline = 'alphabetic';
      ctx.fillText(text || '', anchor, baseline);
    };
    drawAt(totalAvg, avgAnchor, y2, '#fff');
    drawAt(totalM, distAnchor, y2, '#fff');
    if (showTrip) {
      drawAt(tripAvg, avgAnchor, y1, '#0ff');
      drawAt(tripM, distAnchor, y1, '#0ff');
    }
    ctx.font = 'bold ' + L.valSize + 'px monospace';
    let numberLeft = avgAnchor - ctx.measureText(totalAvg || '').width;
    if (showTrip) numberLeft = Math.min(numberLeft, avgAnchor - ctx.measureText(tripAvg || '').width);
    const legendRight = avgLegendRight(lcx - 288 * fs, numberLeft, 6 * fs);
    ctx.font = (18 * fs) + 'px monospace';
    ctx.textAlign = 'right';
    ctx.fillStyle = '#fff';
    if (page === 1) {   // a pc / pm average is the sub's: "sub" above "avg", no unit
      ctx.fillText('sub', legendRight, y2 - 24 * fs);
      ctx.fillText('avg', legendRight, y2 - 4 * fs);
    } else {
      ctx.fillText('avg', legendRight, y2 - 24 * fs);
      ctx.fillText(unitLow, legendRight, y2 - 4 * fs);
    }
    if (page === 3) {
      ctx.fillStyle = '#0ff';
      ctx.fillText('sub', legendRight, y1 - 4 * fs);
    }
    const rx = lcx + 272 * fs;
    ctx.textAlign = 'left';
    ctx.fillStyle = '#fff';
    ctx.fillText('Total', rx, y2 - 4 * fs);
    if (!showTrip) {
      ctx.fillText(distUnit, rx, y2 - 24 * fs);
    } else {
      ctx.fillStyle = '#0ff';
      ctx.fillText(Number(msg.rally_type) === 2 ? 'sub' : 'Trip', rx, y1 - 4 * fs);
      ctx.fillStyle = '#ccc';
      ctx.fillText(distUnit, rx, (y1 + y2) / 2 + 2 * fs);
    }
  }

  function rgb(c) {
    return 'rgb(' + Math.round(c.r * 255) + ',' + Math.round(c.g * 255) + ',' + Math.round(c.b * 255) + ')';
  }

  function DriverGauge(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
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

  // Total and Trip are the HTML footer in portrait. Landscape draws them on the
  // canvas, in the box's places, inside an 800×480 stage.
  const STAGE_STRIP = 0;
  const DIAL_DROP = 48;
  // Extra canvas under the speed and target lines. The dial stays bottom-anchored,
  // so this lowers the arc and the three boxes. The speed and target are drawn
  // back up by the same amount.
  const PORTRAIT_DROP = 44;

  function wideLayout() {
    return window.matchMedia('(orientation: landscape) and (min-width: 700px)').matches;
  }

  DriverGauge.prototype.resize = function () {
    const parent = this.canvas.parentElement;
    const dpr = window.devicePixelRatio || 1;
    // Drop the size the last orientation set. Left in place, it props the
    // parent open and the next measurement just reads it back, so repeated
    // rotations ratchet the stage up to the dial's full width.
    this.canvas.style.width = '';
    this.canvas.style.height = '';
    if (wideLayout()) {
      const rem = parseFloat(getComputedStyle(document.documentElement).fontSize) || 16;
      // The page's own cap (100dvh - 2.4rem) and the viewport bound the stage.
      const availW = Math.min(parent.clientWidth || window.innerWidth, window.innerWidth);
      const availH = Math.min(parent.clientHeight || window.innerHeight, window.innerHeight - 2.4 * rem);
      const aspect = 800 / 480;
      let cssW;
      let cssH;
      if (availH > 0 && availW / availH > aspect) {
        cssH = availH;
        cssW = availH * aspect;
      } else {
        cssW = availW;
        cssH = availW / aspect;
      }
      this.canvas.style.width = cssW + 'px';
      this.canvas.style.height = cssH + 'px';
      this.canvas.width = Math.round(cssW * dpr);
      this.canvas.height = Math.round(cssH * dpr);
      this.ctx.setTransform((cssW * dpr) / 800, 0, 0, (cssH * dpr) / 480, 0, 0);
      this.cssW = 800;
      this.cssH = 480;
      this.boxSpace = true;
    } else {
      const cssW = Math.max(280, parent.clientWidth || window.innerWidth);
      const cssH = Math.max(280, Math.min(window.innerHeight * 0.48, cssW * 0.78))
        + STAGE_STRIP + DIAL_DROP + PORTRAIT_DROP;
      this.canvas.style.width = cssW + 'px';
      this.canvas.style.height = cssH + 'px';
      this.canvas.width = Math.round(cssW * dpr);
      this.canvas.height = Math.round(cssH * dpr);
      this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      this.cssW = cssW;
      this.cssH = cssH;
      this.boxSpace = false;
    }
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
    // The box's dial as it last drew it: its scale, colour band, figures and centre text.
    // The phone page is served by the box, so these always come; the phone only draws them.
    const sh = (msg.drv && msg.drv.shown) || {};
    const maxVal = Number(sh.max) > 0 ? Number(sh.max) : (Number(msg.gauge_range) || 3);
    const zone = Number(sh.zone) || 0;
    const arc = gaugeArcColor(zone);
    const boxMode = !!this.boxSpace;
    const L = computeCompactGaugeLayout(width, height - (boxMode ? 0 : STAGE_STRIP), boxMode);
    const centerX = L.centerX;
    const centerY = L.centerY;
    const radius = L.radius;
    const fscale = L.fscale;
    // Line weights follow the dial: the box page's dial (radius 375, drawn then scaled down) sets them, so a
    // smaller portrait dial gets proportionally finer lines.
    const ls = boxMode ? 1 : radius / 375;

    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, width, height);

    // Bezel
    ctx.strokeStyle = 'rgb(64,64,64)';
    ctx.lineWidth = 4 * ls;
    ctx.beginPath();
    ctx.arc(centerX, centerY, radius + 18 * ls, Math.PI, 2 * Math.PI);
    ctx.stroke();

    // Arc background
    ctx.strokeStyle = 'rgb(31,31,31)';
    ctx.lineWidth = 28 * ls;
    ctx.beginPath();
    ctx.arc(centerX, centerY, radius, Math.PI, 2 * Math.PI);
    ctx.stroke();

    // Coloured arc
    ctx.strokeStyle = rgb(arc);
    ctx.lineWidth = 12 * ls;
    ctx.beginPath();
    ctx.arc(centerX, centerY, radius, Math.PI, 2 * Math.PI);
    ctx.stroke();

    // Ticks
    const labelsVisible = zone === 0;
    const tickCount = Math.floor(maxVal);
    ctx.strokeStyle = 'rgb(230,230,230)';
    ctx.fillStyle = 'rgb(230,230,230)';
    ctx.lineWidth = NEEDLE_HALF_WIDTH * 2 * ls;
    ctx.font = 'bold ' + (24 * fscale) + 'px monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    // Fine ticks as the box: every tenth on +-1 s, every two tenths on +-2 s and +-3 s.
    const range = Number(msg.gauge_range) || 3;
    const step = (maxVal >= range + 1) ? 0 : (range === 1) ? 1 : 2;
    if (step) {
      const lw = ctx.lineWidth;
      ctx.lineWidth = lw * 0.6;
      for (let k = -39; k <= 39; k++) {
        if (k % 10 === 0 || k % step !== 0 || Math.abs(k / 10) > maxVal) continue;
        const a = gaugeTickAngle(k / 10, maxVal);
        const inner = (Math.abs(k) % 5 === 0) ? radius - 12 * ls : radius - 2 * ls;
        ctx.beginPath();
        ctx.moveTo(centerX + inner * Math.cos(a), centerY + inner * Math.sin(a));
        ctx.lineTo(centerX + (radius + 8 * ls) * Math.cos(a), centerY + (radius + 8 * ls) * Math.sin(a));
        ctx.stroke();
      }
      ctx.lineWidth = lw;
    }
    for (let i = -tickCount; i <= tickCount; i++) {
      const angle = gaugeTickAngle(i, maxVal);
      const x1 = centerX + (radius - 20 * ls) * Math.cos(angle);
      const y1 = centerY + (radius - 20 * ls) * Math.sin(angle);
      const x2 = centerX + (radius + 8 * ls) * Math.cos(angle);
      const y2 = centerY + (radius + 8 * ls) * Math.sin(angle);
      ctx.beginPath();
      ctx.moveTo(x1, y1);
      ctx.lineTo(x2, y2);
      ctx.stroke();
      if (!labelsVisible) continue;
      const label = gaugeTickLabel(i);
      if (!label) continue;
      const labelR = radius - 38 * ls;
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
    const triY = centerY - radius - 12 * ls;
    ctx.moveTo(centerX, triY + 10 * ls);
    ctx.lineTo(centerX - 6 * ls, triY);
    ctx.lineTo(centerX + 6 * ls, triY);
    ctx.closePath();
    ctx.fill();

    // The box page: 1 italian pc/pm, 2 italian navi, 3 greek stage, 4 between stages.
    const drv = msg.drv || null;
    const hideNeedle = drv && drv.needle === false;

    // Needle
    const needle = computeNeedleGeometry(seconds, maxVal, radius);
    needle.halfWidth *= ls;
    const dirX = Math.cos(needle.angle);
    const dirY = Math.sin(needle.angle);
    const perpX = -Math.sin(needle.angle);
    const perpY = Math.cos(needle.angle);
    const tipX = centerX + needle.length * dirX;
    const tipY = centerY + needle.length * dirY;
    // Under the readout while the needle sweeps. Pegged at either end, it is
    // drawn again after the tag and the error box so those boxes do not hide it.
    const drawNeedleBar = () => {
      ctx.fillStyle = 'rgb(230,230,230)';
      ctx.beginPath();
      ctx.moveTo(tipX + needle.halfWidth * perpX, tipY + needle.halfWidth * perpY);
      ctx.lineTo(tipX - needle.halfWidth * perpX, tipY - needle.halfWidth * perpY);
      ctx.lineTo(centerX - needle.halfWidth * perpX, centerY - needle.halfWidth * perpY);
      ctx.lineTo(centerX + needle.halfWidth * perpX, centerY + needle.halfWidth * perpY);
      ctx.closePath();
      ctx.fill();
    };
    if (!hideNeedle) drawNeedleBar();
    const needlePinned = !hideNeedle && maxVal > 0 && Math.abs(seconds) >= maxVal;
    // (no hub disc: the box below covers it, and the needle appears to start
    // at the box's top edge)

    // Digital ahead/behind box. Empty when the box hides the needle.
    const digital = hideNeedle ? '' : String(sh.digital || '');
    let rightText = '';
    let ahead = true;
    if (drv && drv.page === 1 && drv.box) rightText = String(drv.box);
    else if (drv && drv.page === 2 && drv.min !== undefined && drv.min !== null) {
      ahead = Number(drv.min) >= 0;
      rightText = (Number(drv.min) > 0 ? '+' : '') + drv.min;
    }
    ctx.font = 'bold ' + (boxMode ? L.boxFontSize : L.valSize) + 'px monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    const textW = ctx.measureText(digital).width;
    // Portrait with the three-box row: equal widths, edges touching, across the
    // screen. Landscape uses the box's own widths. Otherwise the error box keeps
    // a scaled floor so an empty readout is not a small square.
    const thirds = !boxMode && !!drv;
    let boxW;
    let boxH;
    let boxY;
    if (boxMode) {
      boxW = Math.max(180, textW + 24) * 1.1 + 16 * fscale;
      boxH = 80 * fscale;
      boxY = L.boxY;
      const needleTop = centerY - NEEDLE_BAR_HALF_WIDTH;
      if (boxY < needleTop) {
        boxH -= needleTop - boxY;
        boxY = needleTop;
      }
    } else if (thirds) {
      boxW = (width - 16) * 0.46;   // the error box is the widest; the tag and countdown boxes are equal
      boxH = L.boxHeight;
      boxY = L.boxY;
    } else {
      boxW = Math.max(170 * fscale, textW + 20);
      boxH = L.boxHeight;
      boxY = L.boxY;
    }
    const boxCenterY = boxY + boxH / 2;
    const boxX = thirds ? 8 + (width - 16) * 0.27 : centerX - boxW / 2;   // portrait: tag | error | countdown fill the width
    ctx.fillStyle = '#000';
    ctx.fillRect(boxX, boxY, boxW, boxH);
    // The stroke is centred on the path, so a line on the rectangle edge hangs
    // half its width outside and the box looks taller than the S1 tag. Draw it
    // inside, as the box does, so the outer edge is the same height.
    const border = 3;
    const inset = border / 2;
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = border;
    ctx.strokeRect(boxX + inset, boxY + inset, boxW - border, boxH - border);
    ctx.fillStyle = '#fff';
    // The decimal stays on the box centre, as on the box, so the columns
    // do not step when the number gains a digit.
    if (digital) {
      ctx.textAlign = 'left';
      const dot = digital.indexOf('.');
      let x = centerX - ctx.measureText(digital).width / 2;
      if (dot >= 0) {
        x = centerX - ctx.measureText(digital.slice(0, dot)).width
            - ctx.measureText('.').width / 2;
      }
      ctx.fillText(digital, x, boxCenterY);
    }

    // Left tag (navi / pc / pm / S#) and, on Italian, the countdown or minutes box.
    if (drv) {
      // Same row as the error box, right edge touching its left edge.
      const tagText = String(drv.tag || '');
      const tagSize = boxMode ? 46 * L.fscale : (thirds ? 30 * L.fscale : Math.max(16, 22 * L.fscale));
      ctx.font = 'bold ' + tagSize + 'px sans-serif';
      const tw = ctx.measureText(tagText).width;
      const iconKind = Number(drv.icon) || 0;
      const icon = iconKind ? (boxMode ? 36 : (thirds ? 26 : 22)) * L.fscale : 0;
      const igap = icon ? (boxMode ? 8 : 10) * L.fscale : 0;
      const sideW = boxMode ? 160 * L.fscale
        : (thirds ? (width - 16) * 0.27 : Math.min(boxX - 4, Math.max(boxH * 1.7, tw + igap + icon + 16)));
      const tagY = boxY;
      const tagCy = boxCenterY;
      const drawTag = (x, text, bg, fg) => {
        ctx.fillStyle = bg;
        ctx.fillRect(x, tagY, sideW, boxH);
        ctx.fillStyle = fg;
        ctx.textAlign = 'left';
        ctx.textBaseline = 'middle';
        ctx.font = 'bold ' + tagSize + 'px sans-serif';
        const content = tw + igap + icon;
        const tx = x + Math.max(6, (sideW - content) / 2);
        ctx.fillText(text, tx, tagCy);
        if (!iconKind) return;
        const ox = tx + tw + igap + icon / 2;
        const oy = tagCy + 1;
        const r = icon * 0.34;
        ctx.strokeStyle = fg;
        ctx.lineWidth = Math.max(1.5, 1.6 * L.fscale);
        ctx.beginPath();
        if (iconKind === 1) {
          ctx.fillRect(ox - r * 0.4, oy - r * 1.7, r * 0.8, r * 0.35);
          ctx.arc(ox, oy, r, 0, Math.PI * 2);
          ctx.stroke();
          ctx.beginPath();
          ctx.moveTo(ox, oy);
          ctx.lineTo(ox, oy - r * 0.6);
          ctx.stroke();
        } else {
          ctx.arc(ox, oy, r, 0.75 * Math.PI, 2.25 * Math.PI);
          ctx.stroke();
          ctx.beginPath();
          ctx.moveTo(ox, oy);
          ctx.lineTo(ox - r * 0.5, oy - r * 0.6);
          ctx.stroke();
        }
      };
      drawTag(boxX - sideW, tagText,
        drv.icon === 2 ? '#FFDD00' : (drv.page === 2 || drv.page === 4 ? '#666' : '#DD0000'),
        drv.icon === 2 ? '#000' : '#fff');
      if (rightText) {
        const rx = boxX + boxW;
        ctx.fillStyle = ahead ? '#00aa00' : '#dd1111';
        const rw = thirds ? (width - 16) * 0.27 : sideW;
        ctx.fillRect(rx, boxY, rw, boxH);
        ctx.fillStyle = '#fff';
        let rtSize = boxMode ? L.boxFontSize * 1.5 : Math.max(18, L.valSize * (thirds ? 0.95 : 0.7));
        ctx.font = 'bold ' + rtSize + 'px monospace';
        if (thirds) {   // the countdown fills its box, shrunk only if it would touch the sides
          const maxW = (width - 16) * 0.27 - 14;
          const w = ctx.measureText(rightText).width;
          if (w > maxW) { rtSize *= maxW / w; ctx.font = 'bold ' + rtSize + 'px monospace'; }
        }
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(rightText, rx + rw / 2, boxCenterY);
        // Portrait drops the word. The box still draws it, to the right of the box.
        if (boxMode && drv.legend) {
          const adv = ctx.measureText(rightText).width;
          const textRight = rx + (sideW + adv) / 2;
          const legendX = captionLeftAfter(centerX + 272 * fscale, Math.max(rx + sideW, textRight), 6 * fscale);
          ctx.font = (18 * fscale) + 'px monospace';
          ctx.textAlign = 'left';
          ctx.textBaseline = 'alphabetic';
          ctx.fillText(drv.legend, legendX, boxY + boxH / 2 + 6 * fscale);
        }
      }
    }
    // Averages sit under the arc. Draw them before the pegged needle so the
    // bar and the chevron stay on top, as they do on the box.
    if (boxMode) {
      drawBoxReadouts(ctx, L, drv, msg, boxX, boxY, boxW, fscale,
        !!(drv && (drv.page === 1 || drv.page === 2)));
    }
    if (needlePinned) drawNeedleBar();

    // One chevron on the needle, as the box. Italian pc / pm: it waits at the
    // start and slides to the line tick over the last 5 s. Greek and Surrey: it
    // walks the row. Hidden with the needle (navi, ct, co, between stages).
    if (!hideNeedle) {
      let progress = 0;
      let showTick = false;
      if (drv && typeof drv.chev === 'number') {
        progress = drv.chev;
        showTick = !!drv.tick;
      }
      if (progress < 0) progress = 0;
      if (progress > 1) progress = 1;
      const fStart = 0.20;
      const fTick = 0.80;
      const fArrive = fTick - 3 * fscale / needle.length;   // the tip's outline lands on the bar, as the box does
      const fTop = fStart + progress * (fArrive - fStart);
      const halfW = 18 * fscale;
      const depth = 14 * fscale;
      const len = needle.length;
      const strokeV = (x1, y1, x2, y2, colour, widthPx) => {
        ctx.strokeStyle = colour;
        ctx.lineWidth = widthPx;
        ctx.beginPath();
        ctx.moveTo(x1, y1);
        ctx.lineTo(x2, y2);
        ctx.stroke();
      };
      if (showTick) {
        const tx = centerX + fTick * len * dirX;
        const ty = centerY + fTick * len * dirY;
        strokeV(tx + halfW * perpX, ty + halfW * perpY,
                 tx - halfW * perpX, ty - halfW * perpY, '#fff', Math.max(2, 3 * fscale));
      }
      if (fTop >= 0.03) {
        const ax = centerX + fTop * len * dirX;
        const ay = centerY + fTop * len * dirY;
        const bx = ax - depth * dirX;
        const by = ay - depth * dirY;
        const e1x = bx + halfW * perpX;
        const e1y = by + halfW * perpY;
        const e2x = bx - halfW * perpX;
        const e2y = by - halfW * perpY;
        const mark = (colour, widthPx) => {
          ctx.strokeStyle = colour;
          ctx.lineWidth = widthPx;
          ctx.beginPath();
          ctx.moveTo(e1x, e1y);
          ctx.lineTo(ax, ay);
          ctx.lineTo(e2x, e2y);
          ctx.stroke();
        };
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        mark('#000', Math.max(3, 6 * fscale));
        mark('#fff', Math.max(2, 3.5 * fscale));
        ctx.lineCap = 'butt';
        ctx.lineJoin = 'miter';
      }
    }

    // Current (right) / Target (left). Italian figures are the box's km/h strings.
    const italianFig = drv && (drv.page === 1 || drv.page === 2);
    const cur = String(sh.speed || '');
    const tgt = String(sh.target || '');
    ctx.textBaseline = 'alphabetic';

    // Portrait: the dial moved down with the taller canvas. Draw the speeds back
    // at the old baseline so the arc drops clear of them. Landscape top-aligns
    // the glyphs to the arc's top, as the box does.
    const topRightInk = width - 4 * L.fscale;
    const topLeftInk = 4 * L.fscale;
    const capDrop = L.legendDrop * 0.2;
    const drawCorner = (text, anchor, right, size, baseline) => {
      ctx.font = 'bold ' + size + 'px monospace';
      ctx.textAlign = 'left';
      const m = ctx.measureText(text);
      let y = baseline;
      let x = right ? anchor - m.width : anchor;
      if (boxMode && baseline == null) {
        const ascent = m.actualBoundingBoxAscent > 1 ? m.actualBoundingBoxAscent : size * 0.8;
        y = L.bandTopY + ascent;
      }
      ctx.fillText(text, x, y);
      return y;
    };
    const drawCornerLabel = (text, anchor, right, valueBaseline) => {
      ctx.font = L.labelSize + 'px sans-serif';
      ctx.textAlign = 'left';
      const m = ctx.measureText(text);
      const x = right ? anchor - m.width : anchor;
      ctx.fillText(text, x, valueBaseline + L.labelSize + L.labelGap + capDrop);
    };
    const figureBase = boxMode ? null : L.speedBase - PORTRAIT_DROP;
    ctx.fillStyle = '#fff';
    const curBase = drawCorner(cur, topRightInk, true, L.curTopSize, figureBase);
    drawCornerLabel(italianFig ? 'Kph' : (msg.units === 'mph' ? 'Mph' : 'Kph'), topRightInk, true, curBase);

    // Same inset as the box: the arc's top is near the hub, so the yellow
    // stack sits a few pixels in from the panel edge.
    ctx.fillStyle = '#FFDD00';
    if (sh.cap && sh.cap !== 'Target') {   // the box's between-stages arrival, its own figure and caption
      const b = drawCorner(tgt, topLeftInk, false, L.curTopSize, figureBase);
      drawCornerLabel(sh.cap, topLeftInk, false, b);
    } else {
      const b = drawCorner(tgt, topLeftInk, false, L.curTopSize, figureBase);
      drawCornerLabel('Target', topLeftInk, false, b);
      if (italianFig && drv.cd != null) {
        const dur = String(drv.cd);   // the countdown to the line, the box's own text
        const durSize = L.curTopSize * 0.64;
        ctx.font = 'bold ' + durSize + 'px monospace';
        ctx.textAlign = 'left';
        ctx.fillText(dur, topLeftInk, b + L.labelSize + L.labelGap + capDrop + durSize * 1.05);
      }
    }

    // The stage number lives in the left tag once the page is known.
    if (!drv) {
      const stageNo = Number(msg.stage_no) || 0;
      ctx.fillStyle = '#FFDD00';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'alphabetic';
      const stageSize = 46.5 * L.fscale;
      ctx.font = 'bold ' + stageSize + 'px sans-serif';
      ctx.fillText('Stage ' + (stageNo >= 1 ? stageNo : '-'), centerX, L.footBaseline + 2 + stageSize * 0.8);
    }

    // Portrait keeps Total and Trip in the footer under the canvas.
    // Landscape has already drawn them on the dial. No rally clock here.
  };

  global.RallyDriverGauge = DriverGauge;
})(window);
