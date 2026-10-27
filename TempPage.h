/*

  web page for Remote Temp Sensor, sent by SendWebsite() in RemoteTempSensor.ino

  same approach as ET Phone Boat: the whole page (HTML + CSS + javascript) is stored in one big
  char array using R"=====( ... )=====" so it can be written naturally

  how the page talks to the ESP
  1. on load        -> GET all     -> server.on("/all", SendAll);
     the ESP replies {"u":"F","i":180,"s":95,"n":1234,"t":[38,37,38,...]}
  2. every 5 s      -> GET latest  -> server.on("/latest", SendLatest);
     the ESP replies {"u":"F","i":180,"s":95,"n":1234,"t":38}
     if n is one more than the last number the page has, the point is added
     if the page missed some (phone was asleep) or the ESP restarted, it asks for /all again

  the plot shows PLOT_POINTS readings, x axis = reading number, y axis = temperature
  once there are more than PLOT_POINTS readings the x axis scrolls so the newest is at the right

*/

const char PAGE_MAIN[] PROGMEM = R"=====(

<!DOCTYPE html>
<html lang="en">

<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
  <!-- lets the page run full screen when added to the iPhone home screen -->
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <meta name="apple-mobile-web-app-title" content="Fridge">
  <meta name="theme-color" content="#0b1220">
  <title>Fridge Temp</title>

  <style>
    :root {
      --bg: #0b1220;
      --card: rgba(255,255,255,0.06);
      --card-border: rgba(255,255,255,0.10);
      --text: #e8eefc;
      --muted: #8a97b4;
      --accent: #38bdf8;
      --accent2: #6366f1;
      --good: #22c55e;
      --radius: 22px;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }

    html, body {
      height: 100%;
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "Segoe UI", Roboto, sans-serif;
      -webkit-tap-highlight-color: transparent;
      -webkit-text-size-adjust: 100%;
      overscroll-behavior: none;
    }

    body {
      background:
        radial-gradient(120% 60% at 50% -10%, rgba(56,189,248,0.22), transparent 60%),
        radial-gradient(80% 50% at 100% 100%, rgba(99,102,241,0.16), transparent 60%),
        var(--bg);
      background-attachment: fixed;
    }

    /* header, readout and plot stacked, the plot takes the rest of the screen */
    .app {
      max-width: 640px;
      height: 100%;
      margin: 0 auto;
      padding: calc(env(safe-area-inset-top) + 14px) 16px calc(env(safe-area-inset-bottom) + 16px);
      display: flex;
      flex-direction: column;
      gap: 14px;
    }

    .card {
      background: var(--card);
      border: 1px solid var(--card-border);
      border-radius: var(--radius);
      backdrop-filter: blur(10px);
      -webkit-backdrop-filter: blur(10px);
    }

    /* ---------- header ---------- */
    .top {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
    }
    .brand { font-size: 22px; font-weight: 700; letter-spacing: -0.02em; }
    .brand span {
      background: linear-gradient(90deg, var(--accent), var(--accent2));
      -webkit-background-clip: text;
      background-clip: text;
      color: transparent;
    }
    .sub { font-size: 12px; color: var(--muted); margin-top: 2px; }

    .pill {
      display: flex;
      align-items: center;
      gap: 7px;
      padding: 7px 12px;
      border-radius: 999px;
      font-size: 13px;
      font-weight: 600;
      white-space: nowrap;
      background: var(--card);
      border: 1px solid var(--card-border);
    }
    .pill .dot { width: 8px; height: 8px; border-radius: 50%; background: var(--muted); }
    .pill.on .dot { background: var(--good); box-shadow: 0 0 0 4px rgba(34,197,94,0.18); }

    /* ---------- current reading ---------- */
    .now {
      padding: 16px 18px;
      display: grid;
      gap: 14px;
    }
    .label { font-size: 12px; color: var(--muted); font-weight: 600; letter-spacing: 0.06em; text-transform: uppercase; }
    .big {
      font-size: 72px;
      font-weight: 700;
      letter-spacing: -0.04em;
      line-height: 1;
      font-variant-numeric: tabular-nums;
      margin-top: 6px;
    }
    .big small { font-size: 0.4em; font-weight: 600; color: var(--muted); letter-spacing: 0; margin-left: 4px; vertical-align: top; position: relative; top: 0.25em; }
    .next { font-size: 13px; color: var(--muted); margin-top: 6px; font-variant-numeric: tabular-nums; }

    .now-head { display: flex; align-items: center; justify-content: space-between; }

    /* °F / °C switch */
    .seg {
      display: flex;
      padding: 3px;
      border-radius: 999px;
      background: rgba(255,255,255,0.06);
      border: 1px solid var(--card-border);
    }
    .seg button {
      border: 0;
      background: transparent;
      color: var(--muted);
      font: inherit;
      font-size: 13px;
      font-weight: 700;
      padding: 6px 12px;
      border-radius: 999px;
      cursor: pointer;
      transition: background 0.15s, color 0.15s;
    }
    .seg button.on {
      background: linear-gradient(90deg, var(--accent), var(--accent2));
      color: #fff;
    }

    .stats {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 8px;
    }
    .stat {
      background: rgba(255,255,255,0.04);
      border-radius: 14px;
      padding: 9px 10px;
    }
    .stat b { display: block; font-size: 20px; font-weight: 700; font-variant-numeric: tabular-nums; margin-top: 2px; }
    .stat .label { font-size: 10px; }

    /* ---------- plot ---------- */
    .plot {
      flex: 1;
      padding: 14px 12px 10px 6px;
      display: flex;
      flex-direction: column;
      min-height: 220px;
    }
    .plot-head {
      display: flex;
      justify-content: space-between;
      align-items: baseline;
      padding: 0 6px 6px 12px;
    }
    .plot-head .range { font-size: 12px; color: var(--muted); font-variant-numeric: tabular-nums; }
    .chart-wrap { position: relative; flex: 1; min-height: 0; }
    canvas { position: absolute; inset: 0; width: 100%; height: 100%; touch-action: pan-y; }

    .tip {
      position: absolute;
      pointer-events: none;
      padding: 6px 10px;
      border-radius: 10px;
      background: rgba(15,23,42,0.92);
      border: 1px solid var(--card-border);
      font-size: 13px;
      white-space: nowrap;
      transform: translate(-50%, -100%);
      opacity: 0;
      transition: opacity 0.12s;
    }
    .tip b { font-size: 15px; }
    .tip span { color: var(--muted); }

    .empty {
      position: absolute;
      inset: 0;
      display: flex;
      align-items: center;
      justify-content: center;
      color: var(--muted);
      font-size: 14px;
    }
  </style>
</head>

<body>
  <div class="app">

    <div class="top">
      <div>
        <div class="brand">Fridge <span>Temp</span></div>
        <div class="sub">ESP32 + DS18B20</div>
      </div>
      <div class="pill" id="link"><div class="dot"></div><span id="linktxt">Connecting</span></div>
    </div>

    <div class="card now">
      <div>
        <div class="now-head">
          <div class="label">Now</div>
          <div class="seg">
            <button id="btnF" onclick="SetUnits('F')">&deg;F</button>
            <button id="btnC" onclick="SetUnits('C')">&deg;C</button>
          </div>
        </div>
        <div class="big"><span id="cur">--</span><small id="units">&deg;F</small></div>
        <div class="next" id="next">&nbsp;</div>
      </div>
      <div class="stats">
        <div class="stat"><div class="label">Min</div><b id="min">--</b></div>
        <div class="stat"><div class="label">Max</div><b id="max">--</b></div>
        <div class="stat"><div class="label">Avg</div><b id="avg">--</b></div>
        <div class="stat"><div class="label">Readings</div><b id="count">0</b></div>
      </div>
    </div>

    <div class="card plot">
      <div class="plot-head">
        <div class="label">Temperature</div>
        <div class="range" id="range"></div>
      </div>
      <div class="chart-wrap" id="wrap">
        <canvas id="chart"></canvas>
        <div class="tip" id="tip"></div>
        <div class="empty" id="empty">Waiting for the first reading&hellip;</div>
      </div>
    </div>

  </div>

  <script>

    // points shown on the plot, after this many the x axis scrolls
    var PLOT_POINTS = 200;
    // how often to ask the ESP for the latest reading
    var POLL_MS = 5000;
    // show "No link" if the ESP hasn't answered for this long
    var LINK_TIMEOUT_MS = 15000;
    // readings kept on the phone, more than the ESP keeps so this never matters in practice
    var MAX_KEEP = 10000;

    var temps = [];      // readings, oldest first
    var lastN = 0;       // reading number of the last value in temps, 0 = none yet
    var units = "F";     // units the ESP sends
    var showUnits = "F"; // units shown on the phone, picked with the °F / °C buttons
    try { showUnits = localStorage.getItem("units") || "F"; } catch (e) {}
    var intervalSec = 180;
    var nextAt = 0;      // Date.now() of the next reading on the ESP
    var lastReply = 0;
    var loadingAll = false;
    var hoverX = null;   // css px x of the finger / mouse on the plot, null = no tooltip

    var canvas = document.getElementById("chart");
    var ctx = canvas.getContext("2d");
    var wrap = document.getElementById("wrap");
    var tip = document.getElementById("tip");

    // ---------- talking to the ESP ----------

    function GetJSON(url) {
      return fetch(url, { cache: "no-store" }).then(function(r) {
        if (!r.ok) throw new Error(r.status);
        return r.json();
      });
    }

    function Common(data) {
      lastReply = Date.now();
      units = data.u;
      intervalSec = data.i;
      nextAt = Date.now() + data.s * 1000;
      UpdateCountdown();
    }

    // asks for every saved reading, on load and whenever the page gets out of step
    function LoadAll() {
      if (loadingAll) return;
      loadingAll = true;
      GetJSON("all").then(function(data) {
        Common(data);
        temps = data.t.slice(-MAX_KEEP);
        lastN = data.n;
        Render();
      }).catch(function() {}).then(function() {
        loadingAll = false;
        UpdateLink();
      });
    }

    function Poll() {
      if (loadingAll) return;
      GetJSON("latest").then(function(data) {
        Common(data);
        if (data.n == 0 || data.n == lastN) {
          // nothing new
        } else if (data.n == lastN + 1 && lastN > 0) {
          temps.push(data.t);
          if (temps.length > MAX_KEEP) temps.shift();
          lastN = data.n;
          Render();
        } else {
          // missed some readings, or the ESP restarted and its numbers went back to 1
          LoadAll();
        }
      }).catch(function() {}).then(UpdateLink);
    }

    function UpdateLink() {
      var ok = (Date.now() - lastReply) < LINK_TIMEOUT_MS;
      document.getElementById("link").className = ok ? "pill on" : "pill";
      document.getElementById("linktxt").textContent = ok ? "Connected" : (lastReply ? "No link" : "Connecting");
    }

    // ---------- units ----------

    // converts a temperature from the ESP's units to the units being shown, as a whole number
    function Show(t) {
      if (units == showUnits) return Math.round(t);
      return Math.round(showUnits == "C" ? (t - 32) * 5 / 9 : t * 9 / 5 + 32);
    }

    // the °F / °C buttons, the choice is remembered on the phone
    function SetUnits(u) {
      showUnits = u;
      try { localStorage.setItem("units", u); } catch (e) {}
      document.getElementById("btnF").className = u == "F" ? "on" : "";
      document.getElementById("btnC").className = u == "C" ? "on" : "";
      Render();
    }

    // ---------- readout ----------

    function Render() {
      var n = temps.length;
      document.getElementById("units").innerHTML = "&deg;" + showUnits;
      document.getElementById("empty").style.display = n ? "none" : "flex";
      document.getElementById("count").textContent = lastN;

      if (n) {
        var mn = temps[0], mx = temps[0], sum = 0;
        for (var i = 0; i < n; i++) {
          mn = Math.min(mn, temps[i]);
          mx = Math.max(mx, temps[i]);
          sum += temps[i];
        }
        document.getElementById("cur").textContent = Show(temps[n - 1]);
        document.getElementById("min").textContent = Show(mn) + "°";
        document.getElementById("max").textContent = Show(mx) + "°";
        document.getElementById("avg").textContent = Show(sum / n) + "°";
      }
      Draw();
    }

    function UpdateCountdown() {
      var el = document.getElementById("next");
      if (!lastReply) { el.innerHTML = "&nbsp;"; return; }
      var s = Math.max(0, Math.round((nextAt - Date.now()) / 1000));
      var every = intervalSec >= 60 ? Math.round(intervalSec / 60) + " min" : intervalSec + " s";
      el.textContent = "Next reading in " + Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2) + "  ·  every " + every;
    }

    // ---------- plot ----------

    // a tick step of 1, 2, 5, 10, 20, 50... that gives about "count" ticks across "span"
    function NiceStep(span, count) {
      var raw = span / Math.max(1, count);
      var mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10));
      var f = raw / mag;
      return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * mag;
    }

    function Draw() {
      var dpr = window.devicePixelRatio || 1;
      var W = wrap.clientWidth, H = wrap.clientHeight;
      if (canvas.width != Math.round(W * dpr) || canvas.height != Math.round(H * dpr)) {
        canvas.width = Math.round(W * dpr);
        canvas.height = Math.round(H * dpr);
      }
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, W, H);

      var css = getComputedStyle(document.documentElement);
      var accent = css.getPropertyValue("--accent").trim();
      var muted = css.getPropertyValue("--muted").trim();
      var bg = css.getPropertyValue("--bg").trim();

      // x axis: reading numbers xMin..xMax, always PLOT_POINTS wide
      // until the plot is full it starts at 1, after that the newest point sits at the right edge
      var xMax = Math.max(PLOT_POINTS, lastN);
      var xMin = xMax - PLOT_POINTS + 1;
      var firstN = lastN - temps.length + 1;         // reading number of temps[0]
      var start = Math.max(0, xMin - firstN);        // first index of temps that is on the plot
      var vis = temps.slice(start).map(Show);         // in the units being shown
      var visFirstN = firstN + start;

      document.getElementById("range").textContent = vis.length ? "#" + visFirstN + " – #" + lastN : "";

      // y axis: fit the visible points, at least 10 degrees tall so small wiggles don't look huge
      var yMin = 30, yMax = 45;
      if (vis.length) {
        yMin = Math.min.apply(null, vis);
        yMax = Math.max.apply(null, vis);
      }
      var pad = Math.max(1, (yMax - yMin) * 0.15);
      yMin -= pad; yMax += pad;
      if (yMax - yMin < 10) {
        var mid = (yMax + yMin) / 2;
        yMin = mid - 5; yMax = mid + 5;
      }
      var yStep = NiceStep(yMax - yMin, Math.max(3, Math.floor(H / 55)));
      yMin = Math.floor(yMin / yStep) * yStep;
      yMax = Math.ceil(yMax / yStep) * yStep;

      ctx.font = "11px -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif";
      var labelW = Math.max(ctx.measureText(String(yMin)).width, ctx.measureText(String(yMax)).width);
      var L = Math.ceil(labelW) + 14, R = 14, T = 12, B = 24;
      var pw = W - L - R, ph = H - T - B;
      if (pw < 20 || ph < 20) return;

      function X(num) { return L + (PLOT_POINTS == 1 ? 0 : (num - xMin) / (PLOT_POINTS - 1) * pw); }
      function Y(t) { return T + (yMax - t) / (yMax - yMin) * ph; }

      // grid and y labels, kept quiet so the line stands out
      ctx.lineWidth = 1;
      ctx.textAlign = "right";
      ctx.textBaseline = "middle";
      for (var v = yMin; v <= yMax + 1e-9; v += yStep) {
        var y = Math.round(Y(v)) + 0.5;
        ctx.strokeStyle = "rgba(255,255,255,0.07)";
        ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(L + pw, y); ctx.stroke();
        ctx.fillStyle = muted;
        ctx.fillText(+v.toFixed(2) + "°", L - 6, y);
      }

      // x labels on round reading numbers
      var xStep = NiceStep(PLOT_POINTS, Math.max(2, Math.floor(pw / 70)));
      ctx.textAlign = "center";
      ctx.textBaseline = "top";
      for (var k = Math.ceil(xMin / xStep) * xStep; k <= xMax; k += xStep) {
        if (k < 1) continue;
        var x = X(k);
        ctx.strokeStyle = "rgba(255,255,255,0.12)";
        ctx.beginPath(); ctx.moveTo(Math.round(x) + 0.5, T + ph); ctx.lineTo(Math.round(x) + 0.5, T + ph + 4); ctx.stroke();
        ctx.fillStyle = muted;
        ctx.fillText(k, x, T + ph + 7);
      }

      if (!vis.length) { tip.style.opacity = 0; return; }

      // soft fill under the line
      var grad = ctx.createLinearGradient(0, T, 0, T + ph);
      grad.addColorStop(0, "rgba(56,189,248,0.28)");
      grad.addColorStop(1, "rgba(56,189,248,0)");
      ctx.beginPath();
      ctx.moveTo(X(visFirstN), T + ph);
      for (var i = 0; i < vis.length; i++) ctx.lineTo(X(visFirstN + i), Y(vis[i]));
      ctx.lineTo(X(visFirstN + vis.length - 1), T + ph);
      ctx.closePath();
      ctx.fillStyle = grad;
      ctx.fill();

      // the line
      ctx.beginPath();
      for (var j = 0; j < vis.length; j++) {
        var px = X(visFirstN + j), py = Y(vis[j]);
        if (j == 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
      }
      ctx.strokeStyle = accent;
      ctx.lineWidth = 2;
      ctx.lineJoin = "round";
      ctx.lineCap = "round";
      ctx.stroke();

      // newest point, with a ring so it stands off the line
      Dot(X(lastN), Y(vis[vis.length - 1]), accent, bg);

      // finger / mouse crosshair and tooltip
      if (hoverX != null) {
        var idx = Math.round((hoverX - L) / pw * (PLOT_POINTS - 1)) + xMin - visFirstN;
        idx = Math.max(0, Math.min(vis.length - 1, idx));
        var hx = X(visFirstN + idx), hy = Y(vis[idx]);
        ctx.strokeStyle = "rgba(255,255,255,0.35)";
        ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(Math.round(hx) + 0.5, T); ctx.lineTo(Math.round(hx) + 0.5, T + ph); ctx.stroke();
        Dot(hx, hy, accent, bg);
        tip.innerHTML = "<b>" + vis[idx] + "&deg;" + showUnits + "</b> <span>#" + (visFirstN + idx) + "</span>";
        var tw = tip.offsetWidth;
        tip.style.left = Math.max(tw / 2, Math.min(W - tw / 2, hx)) + "px";
        tip.style.top = Math.max(T + 34, hy - 10) + "px";
        tip.style.opacity = 1;
      } else {
        tip.style.opacity = 0;
      }
    }

    function Dot(x, y, color, ring) {
      ctx.beginPath();
      ctx.arc(x, y, 6, 0, Math.PI * 2);
      ctx.fillStyle = ring;
      ctx.fill();
      ctx.beginPath();
      ctx.arc(x, y, 4, 0, Math.PI * 2);
      ctx.fillStyle = color;
      ctx.fill();
    }

    // ---------- touch / mouse on the plot ----------

    function SetHover(e) {
      var r = canvas.getBoundingClientRect();
      var p = e.touches ? e.touches[0] : e;
      hoverX = p.clientX - r.left;
      Draw();
    }
    function ClearHover() { hoverX = null; Draw(); }

    canvas.addEventListener("mousemove", SetHover);
    canvas.addEventListener("mouseleave", ClearHover);
    canvas.addEventListener("touchstart", SetHover, { passive: true });
    canvas.addEventListener("touchmove", SetHover, { passive: true });
    canvas.addEventListener("touchend", ClearHover);
    canvas.addEventListener("touchcancel", ClearHover);

    // redraw if the window changes size
    window.addEventListener("resize", Draw);

    // phones pause pages in the background, catch up as soon as it is visible again
    document.addEventListener("visibilitychange", function() {
      if (!document.hidden) Poll();
    });

    // ---------- start ----------

    SetUnits(showUnits);
    LoadAll();
    setInterval(Poll, POLL_MS);
    setInterval(function() { UpdateCountdown(); UpdateLink(); }, 1000);

  </script>

</body>

</html>

)=====";
