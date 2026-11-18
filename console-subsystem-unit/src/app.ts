// Browser side of the console: renders the snapshots pushed by the server
// and turns clicks into commands.

import type { ClientCommand, ServerMessage, Snapshot } from '../shared/types.js';

const $ = <T extends HTMLElement>(id: string): T => {
  const el = document.getElementById(id);
  if (!el) throw new Error(`#${id} missing`);
  return el as T;
};

const radar = document.getElementById('radar') as unknown as SVGSVGElement;
const SVG = 'http://www.w3.org/2000/svg';
const CX = 220, CY = 220, R = 200;
const STALE_MS = 6000;           // measurements older than this are drawn dimmed

// ---------- geometry ----------
/** 0° is the left end of the half-circle, 180° the right end. */
const pt = (deg: number, r: number): [number, number] => {
  const a = (deg * Math.PI) / 180;
  return [CX - r * Math.cos(a), CY - r * Math.sin(a)];
};
const f = (n: number): string => n.toFixed(1);

function wedge(a0: number, a1: number, r0: number, r1: number): string {
  const [x0, y0] = pt(a0, r1), [x1, y1] = pt(a1, r1);
  const [x2, y2] = pt(a1, r0), [x3, y3] = pt(a0, r0);
  return `M${f(x0)} ${f(y0)} A${r1} ${r1} 0 0 1 ${f(x1)} ${f(y1)} L${f(x2)} ${f(y2)} A${r0} ${r0} 0 0 0 ${f(x3)} ${f(y3)}Z`;
}

function el<K extends keyof SVGElementTagNameMap>(tag: K, attrs: Record<string, string>): SVGElementTagNameMap[K] {
  const e = document.createElementNS(SVG, tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  return e;
}

// ---------- drawing ----------
type Kind = 'none' | 'clear' | 'pre' | 'alarm';
const kindOf = (s: Snapshot, i: number): Kind => {
  const x = s.sectors[i];
  if (!x) return 'none';
  if (!x.detected || x.cm === 0) return 'clear';
  return x.cm < s.thresholds.d1 ? 'alarm' : x.cm <= s.thresholds.d2 ? 'pre' : 'clear';
};

function drawRadar(s: Snapshot): void {
  radar.replaceChildren();
  const n = s.sectorCount, step = 180 / n;
  const css = (v: string) => `var(--${v})`;

  radar.append(el('path', { d: wedge(0, 180, 0, R), fill: 'var(--radar-bg)' }));

  for (let i = 0; i < n; i++) {
    const info = s.sectors[i] ?? null;
    const k = kindOf(s, i);
    const stale = info !== null && info.ageMs > STALE_MS;
    const g = el('g', { opacity: stale ? '0.45' : '1' });
    const title = document.createElementNS(SVG, 'title');
    title.textContent = info
      ? `Sector ${i + 1}: ${info.detected ? info.cm + ' cm' : 'clear'}`
      : `Sector ${i + 1}: not scanned`;
    g.append(title);
    g.append(el('path', { d: wedge(i * step + 0.4, (i + 1) * step - 0.4, 0, R), fill: css(k), 'fill-opacity': k === 'none' ? '0.55' : '0.28' }));

    // the echo itself: a bright band at the measured distance
    if (info && info.detected && info.cm > 0) {
      const rr = Math.min(1, info.cm / s.thresholds.maxCm) * R;
      g.append(el('path', { d: wedge(i * step + 0.4, (i + 1) * step - 0.4, Math.max(0, rr - 7), rr), fill: css(k) }));
    }
    const [lx, ly] = pt(i * step + step / 2, R + 14);
    const t = el('text', { x: f(lx), y: f(ly + 4), 'text-anchor': 'middle', 'font-size': '11', fill: 'var(--muted)' });
    t.textContent = String(i + 1);
    g.append(t);
    radar.append(g);
  }

  // threshold rings
  for (const [cm, label] of [[s.thresholds.d1, 'D1'], [s.thresholds.d2, 'D2']] as const) {
    const rr = (cm / s.thresholds.maxCm) * R;
    radar.append(el('path', { d: `M${f(pt(0, rr)[0])} ${f(pt(0, rr)[1])} A${rr} ${rr} 0 0 1 ${f(pt(180, rr)[0])} ${f(pt(180, rr)[1])}`, fill: 'none', stroke: 'var(--muted)', 'stroke-dasharray': '3 4', 'stroke-width': '1' }));
    const t = el('text', { x: f(pt(90, rr)[0] + 4), y: f(pt(90, rr)[1] - 3), 'font-size': '10', fill: 'var(--muted)' });
    t.textContent = `${label} ${cm}cm`;
    radar.append(t);
  }

  // servo needle
  if (s.currentAngle !== null && s.state !== 'IDLE' && s.state !== 'SLEEP') {
    const [x, y] = pt(s.currentAngle, R);
    radar.append(el('line', { x1: String(CX), y1: String(CY), x2: f(x), y2: f(y), stroke: 'var(--needle)', 'stroke-width': '2', 'stroke-linecap': 'round' }));
  }
  radar.append(el('circle', { cx: String(CX), cy: String(CY), r: '5', fill: 'var(--needle)' }));
}

function drawTable(s: Snapshot): void {
  const rows = $('rows');
  rows.replaceChildren();
  for (let i = 0; i < s.sectorCount; i++) {
    const x = s.sectors[i];
    const tr = document.createElement('tr');
    if (i === s.currentSector) tr.className = 'cur';
    const k = kindOf(s, i);
    const td = (text: string, cls = '') => { const c = document.createElement('td'); c.textContent = text; if (cls) c.className = cls; tr.append(c); };
    td(String(i + 1));
    td(x ? `${x.angle}°` : '–');
    td(!x ? '–' : x.detected ? `${x.cm} cm` : 'clear',
       `${k === 'alarm' ? 'd-alarm' : k === 'pre' ? 'd-pre' : ''} ${x && x.ageMs > STALE_MS ? 'old' : ''}`.trim());
    rows.append(tr);
  }
}

// ---------- state ----------
let ws: WebSocket | null = null;
let last: Snapshot | null = null;

function render(s: Snapshot): void {
  last = s;
  const link = $('link');
  link.textContent = !s.connected ? 'serial: disconnected' : s.rsuAlive ? 'RSU online' : 'RSU silent';
  link.className = `chip ${s.connected && s.rsuAlive ? 'ok' : 'bad'}`;

  const st = $('state');
  st.textContent = s.state;
  st.dataset['s'] = s.state;
  $('remote').hidden = !s.remote;
  $('temp').textContent = s.tempC === null ? '– °C' : `${s.tempC.toFixed(1)} °C`;

  const running = s.state === 'WORKING' || s.state === 'PREALARM' || s.state === 'ALARM';
  const online = s.connected;
  $<HTMLButtonElement>('start').disabled = !online || running;
  $<HTMLButtonElement>('stop').disabled = !online || !running;
  $<HTMLButtonElement>('reset').disabled = !online || s.state !== 'ALARM';
  $<HTMLButtonElement>('take').disabled = !online || !running || s.remote;
  $<HTMLButtonElement>('release').disabled = !online || !s.remote;
  $<HTMLInputElement>('angle').disabled = !online || !s.remote;
  $('hint').textContent =
    s.state === 'ALARM' && !s.remote ? 'Alarm: the operator turns the potentiometer on the board. Press Reset to resume.' :
    s.remote ? 'You control the radar. Move the slider to aim it.' :
    s.state === 'SLEEP' ? 'RSU is in deep sleep – press START on the board to wake it.' : '';

  drawRadar(s);
  drawTable(s);
}

function send(c: ClientCommand): void {
  if (ws?.readyState === WebSocket.OPEN) ws.send(JSON.stringify(c));
}

function appendLog(dir: 'rx' | 'tx', line: string): void {
  const log = $('log');
  log.append(`${dir === 'tx' ? '→' : '←'} ${line}\n`);
  while (log.childNodes.length > 200) log.firstChild?.remove();
  log.scrollTop = log.scrollHeight;
}

function connect(): void {
  ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = (ev) => {
    const m = JSON.parse(String(ev.data)) as ServerMessage;
    if (m.type === 'snapshot') render(m.snapshot);
    else appendLog(m.dir, m.line);
  };
  ws.onclose = () => {
    if (last) render({ ...last, connected: false });
    setTimeout(connect, 1500);
  };
}

// ---------- controls ----------
for (const [id, cmd] of [['start', 'START'], ['stop', 'STOP'], ['reset', 'RESET'], ['take', 'TAKE'], ['release', 'RELEASE']] as const) {
  $(id).addEventListener('click', () => send({ cmd }));
}
const slider = $<HTMLInputElement>('angle');
let lastSent = 0;
slider.addEventListener('input', () => {
  $('angleVal').textContent = slider.value;
  const now = performance.now();
  if (now - lastSent > 60) { lastSent = now; send({ cmd: 'ANGLE', angle: Number(slider.value) }); }   // don't flood the serial line
});
slider.addEventListener('change', () => send({ cmd: 'ANGLE', angle: Number(slider.value) }));   // final value always goes out

connect();
