// Software RSU: speaks the same protocol as the Arduino so the console can be
// developed, tested and demoed without hardware (`npm run mock`).
//
// Scenario once WORKING (time counted from START / RESET):
//    6 s  an object appears in sector 4 at 60 cm   -> PREALARM
//   14 s  it moves closer, 20 cm                   -> ALARM (until RESET)
//   after RESET the room is empty again and the story repeats.

import type { Transport } from './transport.js';
import { STATE_NAMES, type StateName } from '../shared/types.js';

const SECTORS = 12;
const TICK_MS = 30;

export class MockTransport implements Transport {
  private lineCb: (l: string) => void = () => {};
  private statusCb: (c: boolean) => void = () => {};
  private timer: NodeJS.Timeout | null = null;

  private state: StateName = 'IDLE';
  private remote = false;
  private remoteAngle = 90;
  private sector = 0;
  private dir = 1;
  private sinceMs = 0;      // time since START/RESET
  private stepMs = 0;
  private hbMs = 0;
  private tempMs = 0;
  private sweepMin = 0;
  private idleMs = 0;

  onLine(cb: (line: string) => void): void { this.lineCb = cb; }
  onStatus(cb: (connected: boolean) => void): void { this.statusCb = cb; }

  open(): void {
    this.timer = setInterval(() => this.tick(), TICK_MS);
    setTimeout(() => { this.statusCb(true); this.emit(`TEMP,${232}`); }, 50);
  }
  close(): void { if (this.timer) clearInterval(this.timer); this.timer = null; }

  write(line: string): void {
    const [cmd, arg] = line.trim().split(',');
    const running = this.state === 'WORKING' || this.state === 'PREALARM' || this.state === 'ALARM';
    switch (cmd) {
      case 'START': if (!running) this.enter('WORKING'); break;
      case 'STOP':  if (running) this.enter('IDLE'); break;
      case 'RESET': if (this.state === 'ALARM') this.enter('WORKING'); break;
      case 'TAKE':  if (running) this.remote = true; break;
      case 'RELEASE': this.remote = false; break;
      case 'ANGLE': if (this.remote) this.remoteAngle = Math.min(180, Number(arg) || 0); break;
    }
  }

  // ---- internals ----
  private emit(line: string): void { setImmediate(() => this.lineCb(line)); }

  private enter(s: StateName): void {
    this.state = s;
    if (s === 'WORKING' || s === 'IDLE') { this.sinceMs = 0; }
    if (s === 'WORKING') { this.sector = 0; this.dir = 1; this.sweepMin = 0; }
    if (s === 'IDLE') { this.remote = false; this.idleMs = 0; }
    this.emit(`STATE,${STATE_NAMES.indexOf(s)},${s}`);
  }

  private distanceIn(sector: number): number {
    if (sector !== 4) return 0;
    if (this.sinceMs > 14000) return 20;
    if (this.sinceMs > 6000) return 60;
    return 0;
  }

  private tick(): void {
    this.hbMs += TICK_MS; this.tempMs += TICK_MS;
    if (this.hbMs >= 1000) {
      this.hbMs = 0;
      this.emit(`HB,${STATE_NAMES.indexOf(this.state)},${this.remote ? 1 : 0}`);
    }
    if (this.tempMs >= 500) { this.tempMs = 0; this.emit(`TEMP,${230 + Math.round(Math.random() * 6)}`); }

    if (this.state === 'IDLE') {
      this.idleMs += TICK_MS;
      if (this.idleMs >= 10000) { this.state = 'SLEEP'; this.emit(`STATE,4,SLEEP`); }
      return;
    }
    if (this.state === 'SLEEP') return;

    this.sinceMs += TICK_MS;
    this.stepMs += TICK_MS;
    const stepNeeded = this.state === 'PREALARM' ? 90 : 180;     // S2 is twice as fast
    if (this.stepMs < stepNeeded) return;
    this.stepMs = 0;

    if (this.state === 'ALARM' || this.remote) {
      // manual: measure where the (simulated) pot / console points
      const angle = this.remote ? this.remoteAngle : 90;
      const sec = Math.min(SECTORS - 1, Math.floor((angle * SECTORS) / 180));
      this.emit(`SEC,${sec},${angle},${this.distanceIn(sec)},${this.distanceIn(sec) ? 1 : 0}`);
      return;
    }

    const cm = this.distanceIn(this.sector);
    const angle = Math.floor((this.sector * 180 + 90) / SECTORS);
    this.emit(`SEC,${this.sector},${angle},${cm},${cm ? 1 : 0}`);
    if (cm && (!this.sweepMin || cm < this.sweepMin)) this.sweepMin = cm;

    if (cm && cm < 30) { this.enter('ALARM'); return; }
    if (cm && cm <= 80 && this.state === 'WORKING') this.enter('PREALARM');

    const next = this.sector + this.dir;
    if (next < 0 || next >= SECTORS) {                           // end of a pass
      if (this.state === 'PREALARM' && (this.sweepMin === 0 || this.sweepMin > 80)) this.enter('WORKING');
      this.sweepMin = 0;
      this.dir = -this.dir;
      this.sector += this.dir;                                   // step back inside
    } else {
      this.sector = next;
    }
  }
}
