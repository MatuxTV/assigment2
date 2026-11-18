// Types shared by the Node backend and the browser frontend.

export const STATE_NAMES = ['IDLE', 'WORKING', 'PREALARM', 'ALARM', 'SLEEP'] as const;
export type StateName = (typeof STATE_NAMES)[number];

/** Result of one sector scan. */
export interface SectorInfo {
  sector: number;      // 0-based
  angle: number;       // servo angle when measured
  cm: number;          // 0 = no echo
  detected: boolean;
  ageMs: number;       // how old the measurement is (server clock)
}

/** Everything the UI needs to draw the console. */
export interface Snapshot {
  connected: boolean;              // serial link to the RSU is open
  rsuAlive: boolean;               // heard something from the RSU recently
  state: StateName;
  remote: boolean;                 // the console owns the radar
  tempC: number | null;
  sectorCount: number;
  sectors: (SectorInfo | null)[];  // length == sectorCount
  currentSector: number | null;
  currentAngle: number | null;
  thresholds: { d1: number; d2: number; maxCm: number };
}

/** Commands the console can send to the RSU. */
export type ClientCommand =
  | { cmd: 'START' | 'STOP' | 'RESET' | 'TAKE' | 'RELEASE' }
  | { cmd: 'ANGLE'; angle: number };

/** server -> browser */
export type ServerMessage =
  | { type: 'snapshot'; snapshot: Snapshot }
  | { type: 'log'; dir: 'rx' | 'tx'; line: string };
