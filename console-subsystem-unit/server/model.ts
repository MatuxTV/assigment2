// The console's picture of the RSU. Pure logic (no I/O), driven by parsed messages.

import type { SectorInfo, Snapshot, StateName } from '../shared/types.js';
import type { RsuMessage } from './protocol.js';

export interface ModelConfig {
  sectorCount: number;
  d1: number;
  d2: number;
  maxCm: number;
  aliveTimeoutMs: number;   // no message for this long => RSU considered gone
}

export const DEFAULT_CONFIG: ModelConfig = {
  sectorCount: 12,
  d1: 30,
  d2: 80,
  maxCm: 200,
  aliveTimeoutMs: 3500,     // RSU sends a heartbeat every 1 s
};

interface Stored { sector: number; angle: number; cm: number; detected: boolean; at: number }

export class RadarModel {
  private connected = false;
  private lastRx: number | null = null;
  private state: StateName = 'IDLE';
  private remote = false;
  private tempC: number | null = null;
  private sectors: (Stored | null)[];
  private current: { sector: number; angle: number } | null = null;

  constructor(private readonly cfg: ModelConfig = DEFAULT_CONFIG) {
    this.sectors = new Array<Stored | null>(cfg.sectorCount).fill(null);
  }

  setConnected(c: boolean): void {
    this.connected = c;
    if (!c) { this.lastRx = null; }
  }

  /** Apply one message from the RSU. Returns true if anything visible changed. */
  apply(msg: RsuMessage, now: number): boolean {
    this.lastRx = now;
    switch (msg.kind) {
      case 'state':
        this.setState(msg.state);
        return true;
      case 'hb': {
        // heartbeat re-synchronises a console that connected mid-run
        const changed = this.state !== msg.state || this.remote !== msg.remote;
        this.setState(msg.state);
        this.remote = msg.state === 'IDLE' || msg.state === 'SLEEP' ? false : msg.remote;
        return changed;
      }
      case 'temp':
        this.tempC = msg.tenths / 10;
        return true;
      case 'sec':
        if (msg.sector >= this.cfg.sectorCount) return false;
        this.sectors[msg.sector] = { sector: msg.sector, angle: msg.angle, cm: msg.cm, detected: msg.detected, at: now };
        this.current = { sector: msg.sector, angle: msg.angle };
        return true;
    }
  }

  private setState(next: StateName): void {
    const prev = this.state;
    this.state = next;
    const off = next === 'IDLE' || next === 'SLEEP';
    if (off) this.remote = false;
    // a fresh run starts from a blank picture
    if (off || (prev === 'IDLE' || prev === 'SLEEP') && next === 'WORKING') {
      this.sectors.fill(null);
      this.current = null;
    }
  }

  snapshot(now: number): Snapshot {
    const sectors = this.sectors.map<SectorInfo | null>((s) =>
      s ? { sector: s.sector, angle: s.angle, cm: s.cm, detected: s.detected, ageMs: now - s.at } : null,
    );
    return {
      connected: this.connected,
      rsuAlive: this.lastRx !== null && now - this.lastRx <= this.cfg.aliveTimeoutMs,
      state: this.state,
      remote: this.remote,
      tempC: this.tempC,
      sectorCount: this.cfg.sectorCount,
      sectors,
      currentSector: this.current?.sector ?? null,
      currentAngle: this.current?.angle ?? null,
      thresholds: { d1: this.cfg.d1, d2: this.cfg.d2, maxCm: this.cfg.maxCm },
    };
  }
}
