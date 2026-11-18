// Wire protocol of the RSU (see radar-subsystem-unit/include/Protocol.h).
//
//  RSU -> CSU   STATE,<id>,<NAME>
//               SEC,<sector>,<angle>,<cm>,<detected>
//               TEMP,<tenths_of_celsius>
//               HB,<stateId>,<remote>
//  CSU -> RSU   START | STOP | RESET | TAKE | RELEASE | ANGLE,<0..180>

import { STATE_NAMES, type ClientCommand, type StateName } from '../shared/types.js';

export type RsuMessage =
  | { kind: 'state'; state: StateName }
  | { kind: 'sec'; sector: number; angle: number; cm: number; detected: boolean }
  | { kind: 'temp'; tenths: number }
  | { kind: 'hb'; state: StateName; remote: boolean };

const INT = /^-?\d+$/;

function int(s: string | undefined): number | null {
  return s !== undefined && INT.test(s.trim()) ? Number(s) : null;
}

function stateFromId(id: number | null): StateName | null {
  return id !== null ? (STATE_NAMES[id] ?? null) : null;
}

/** Parse one line from the RSU. Returns null for anything malformed (never throws). */
export function parseLine(raw: string): RsuMessage | null {
  const f = raw.trim().split(',');
  switch (f[0]) {
    case 'STATE': {
      const state = stateFromId(int(f[1]));
      return state ? { kind: 'state', state } : null;
    }
    case 'SEC': {
      const sector = int(f[1]), angle = int(f[2]), cm = int(f[3]), det = int(f[4]);
      if (sector === null || angle === null || cm === null || det === null) return null;
      if (sector < 0 || angle < 0 || angle > 180 || cm < 0) return null;
      return { kind: 'sec', sector, angle, cm, detected: det !== 0 };
    }
    case 'TEMP': {
      const tenths = int(f[1]);
      return tenths === null ? null : { kind: 'temp', tenths };
    }
    case 'HB': {
      const state = stateFromId(int(f[1]));
      const remote = int(f[2]);
      return state && remote !== null ? { kind: 'hb', state, remote: remote !== 0 } : null;
    }
    default:
      return null;
  }
}

/** Encode a console command into the line to send (without '\n'); null if invalid. */
export function encodeCommand(c: ClientCommand): string | null {
  switch (c.cmd) {
    case 'START':
    case 'STOP':
    case 'RESET':
    case 'TAKE':
    case 'RELEASE':
      return c.cmd;
    case 'ANGLE':
      if (!Number.isInteger(c.angle) || c.angle < 0 || c.angle > 180) return null;
      return `ANGLE,${c.angle}`;
    default:
      return null;
  }
}
