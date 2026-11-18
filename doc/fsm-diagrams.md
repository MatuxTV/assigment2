# RSU – FSM diagrams

All diagrams are generated from the firmware in `radar-subsystem-unit/` (state names and
conditions match the code). Mermaid renders on GitHub, VS Code (Markdown Preview Mermaid
Support) and https://mermaid.live.

Conventions: `A --> B : condition / action`. *Pulse* = signal that is high for exactly one clock
period. *Moore output* = depends only on the state; *Mealy output* = depends on state and input.

---

## 1. Architecture: one clock, many synchronous FSMs

The RSU is modelled like a VHDL design. There is a single clock (`CLK_MS = 5 ms`). Every FSM has
registers (`Reg<T>`: current value `q`, next value `d`). On every clock edge:

1. **wire** – concurrent assignments: registered outputs of one FSM feed the inputs of another,
   plus the physical inputs (pins, ADC, serial);
2. **comb()** – every FSM computes its next state from `q` values only and writes only `d`;
3. **commit()** – all registers take `q <= d` at the same instant.

Because comb() never changes `q`, the order of the FSMs is irrelevant (as in hardware), and a
signal produced in cycle *n* is seen by its consumer in cycle *n + 1*.

```mermaid
flowchart LR
  subgraph IN[Physical inputs]
    BTN[START / STOP / RESET<br/>buttons]
    POT[POT]
    LM[TEMP sensor]
    ECHO[HC-SR04 echo<br/>pin-change ISR]
    RX[Serial RX]
  end

  subgraph FSMS[Synchronous FSMs - one clock]
    B[ButtonFsm x3]
    CT[ControllerFsm]
    RD[RadarFsm]
    SN[SonarFsm]
    TP[TempFsm]
    BL[BlinkerFsm]
    DP[DisplayFsm]
    CM[CommsFsm]
  end

  subgraph OUT[Physical outputs]
    LON[ON led]
    LAL[ALARM led]
    SRV[Servo]
    TRG[Sonar TRIG]
    LCD[LCD I2C]
    TX[Serial TX]
  end

  BTN --> B -- pressed pulse --> CT
  RX --> CM -- start/stop/reset/take/release pulses --> CT
  CM -- remoteAngle --> RD
  POT -- manualAngle --> RD
  LM --> TP -- tenths of degC --> SN
  TP --> DP
  TP --> CM

  CT -- radarMode, radarReset --> RD
  RD -- sonarReq pulse --> SN
  ECHO -- echoValid, echoUs --> SN
  SN -- done, cm --> RD
  RD -- sectorReady, outCm, sweepDone, sweepMin --> CT
  RD --> DP
  RD --> CM
  CT -- alarmLed mode --> BL
  CT -- state --> DP
  CT -- stateChanged --> CM

  CT -- onLed --> LON
  BL --> LAL
  RD -- angle, servoOn --> SRV
  SN -- trig --> TRG
  DP -- wr, wrIdx, wrCh --> LCD
  CM -- txValid, txType, txA..C --> TX
```

### The clock cycle

```mermaid
flowchart LR
  A([clock edge due<br/>every 5 ms]) --> W[wire<br/>read pins/ADC/serial,<br/>copy q of one FSM to inputs of another]
  W --> C[comb of ALL FSMs<br/>read q, write d]
  C --> K[commit of ALL FSMs<br/>q := d]
  K --> D[drive<br/>q to pins, servo, LCD, serial]
  D --> A
```

---

## 2. ControllerFsm – top-level behaviour

States: `IDLE`, `SLEEP`, `WORKING`, `PREALARM`, `ALARM`.
Inputs: `start`, `stop`, `reset` (button or console), measurement `meas` with distance `measCm`,
`sweepDone` with `sweepMinCm`.
`D1 = 30 cm`, `D2 = 80 cm` (see `Config.h`).

```mermaid
stateDiagram-v2
  [*] --> IDLE

  IDLE --> WORKING: start / radarReset
  IDLE --> SLEEP: 10 s without start
  SLEEP --> WORKING: start (wake-up on INT0) / radarReset

  state RUNNING {
    [*] --> WORKING
    WORKING --> PREALARM: object, D1 <= d <= D2
    WORKING --> ALARM: object, d < D1
    PREALARM --> ALARM: object, d < D1
    PREALARM --> WORKING: sweep done and no echo <= D2
    ALARM --> WORKING: reset / radarReset
  }

  RUNNING --> IDLE: stop (from any running state)
```

> The diagram nests the three running states in one box only to avoid drawing three `stop`
> arrows. In the code the `stop` transition exists separately in WORKING, PREALARM and ALARM, and
> an alarm has priority over a pre-alarm when both are detected in the same cycle.

**Outputs (Moore – function of the state only)**

| State | ON led | ALARM led | Radar mode | LCD / servo |
|---|---|---|---|---|
| IDLE | off | off | OFF | off, servo parked |
| SLEEP | off | off | OFF | off, MCU in deep sleep |
| WORKING | on | off | SWEEP_S1 (slow) | on |
| PREALARM | on | **on (steady)** | SWEEP_S2 (fast) | on |
| ALARM | on | **blinking** | MANUAL_POT (operator) | on |

With remote control taken (`remote = 1`) the radar mode in any running state is `MANUAL_REMOTE`.

**Mealy outputs (pulses)**: `stateChanged` on every transition (the console receives `STATE,…`),
`radarReset` on IDLE→WORKING, SLEEP→WORKING and ALARM→WORKING (radar returns to 0°).

---

## 3. RadarFsm – sector by sector sweep

```mermaid
stateDiagram-v2
  [*] --> PARKED

  PARKED --> HOME: mode != OFF
  HOME --> MOVE: 300 ms at 0 deg
  MOVE --> MEASURE: servo settled (S1 180 ms / S2 90 ms) / sonarReq
  MEASURE --> MOVE: sonar done, or 240 ms watchdog / sectorReady, next sector

  note right of MEASURE
    end of a pass (sector 0 or N-1):
    sweepDone, sweepMin, direction flips
  end note

  note left of MOVE
    sweep modes: angle = centre of the sector
    manual modes: angle = POT or console,
    sector derived from the angle
  end note
```

Transitions that apply from **any** state:

| Condition | Next state |
|---|---|
| `mode == OFF` (idle / sleep) | `PARKED` (servo detached) |
| `reset` pulse (alarm cleared, new start) | `HOME` (back to 0°, sector 0, direction forward) |

The sector order in a sweep is `0 → N-1`, then `N-1 → 0` (forward and backward), with each
end sector measured once per pass.

---

## 4. SonarFsm – non-blocking HC-SR04 measurement

No `pulseIn()` is used. The width of the echo is measured by an interrupt service routine on
a pin-change interrupt (PCINT0, D8); the FSM only sees the result as the synchronised signals `echoValid` / `echoUs`.

```mermaid
stateDiagram-v2
  [*] --> IDLE
  IDLE --> TRIG: req / trig = 1
  TRIG --> WAIT: next clock / trig = 0
  WAIT --> IDLE: echoValid / cm = echoUs * v(T) / 2, done
  WAIT --> IDLE: timeout 120 ms / cm = 0, done
```

`v(T) = 331.3 + 0.606 · T` m/s, with `T` from the temperature sensor (calibration of the sound
speed, as required). Distances above `SONAR_MAX_CM` give `cm = 0` (nothing in range).

---

## 5. ButtonFsm – debounce (one instance per button)

```mermaid
stateDiagram-v2
  [*] --> UP
  UP --> PRESS_WAIT: raw pressed
  PRESS_WAIT --> UP: raw released (bounce)
  PRESS_WAIT --> DOWN: stable for 25 ms / pressed pulse
  DOWN --> RELEASE_WAIT: raw released
  RELEASE_WAIT --> DOWN: raw pressed (bounce)
  RELEASE_WAIT --> UP: stable for 25 ms
```

---

## 6. BlinkerFsm – ALARM led driver

```mermaid
stateDiagram-v2
  [*] --> OFF
  OFF --> ON: mode = ON
  OFF --> BLINK: mode = BLINK
  ON --> OFF: mode = OFF
  ON --> BLINK: mode = BLINK
  BLINK --> OFF: mode = OFF
  BLINK --> ON: mode = ON
  BLINK --> BLINK: every 125 ms / out = not out
```

(`mode` comes from the Moore output `alarmLed()` of the Controller: OFF, ON in pre-alarm, BLINK in alarm.)

---

## 7. DisplayFsm – non-blocking LCD refresh

An I2C LCD is slow, so one character is written per clock tick instead of blocking for a whole
frame.

```mermaid
stateDiagram-v2
  [*] --> WAIT
  WAIT --> WRITE: display on and 250 ms refresh strobe / compose frame
  WRITE --> WRITE: idx < 31 / wr pulse with (idx, char), idx + 1
  WRITE --> WAIT: idx = 31 (frame complete)
  WRITE --> WAIT: display switched off
```

Screen layout:

```
PREALARM 23.5C        <- state, temperature
S5/12   45cm          <- last visited sector, distance ("clear" if no object)
```

---

## 8. CommsFsm – serial link

CommsFsm is a small datapath around a queue rather than a state graph.

```mermaid
flowchart LR
  P[CommandParser<br/>one line per clock] -- cmd --> RXD{decode}
  RXD -- START / STOP / RESET / TAKE / RELEASE --> PUL[one-cycle pulses to Controller]
  RXD -- ANGLE n --> RA[remoteAngle register]

  E1[stateChanged] --> Q
  E2[sectorReady] --> Q
  E3[tempUpdated] --> Q
  E4[heartbeat strobe 1 s] --> Q
  Q[(ring buffer, 8 slots<br/>drop when full)] -- one message per clock --> TXV[txValid + txType/A/B/C]
  TXV --> SER[Serial: STATE / SEC / TEMP / HB lines]
```

Protocol (ASCII, one line per message):

| Direction | Line | Meaning |
|---|---|---|
| RSU → CSU | `STATE,<id>,<NAME>` | state change |
| RSU → CSU | `SEC,<sector>,<angle>,<cm>,<detected>` | one sector measurement |
| RSU → CSU | `TEMP,<tenths>` | temperature in 0.1 °C |
| RSU → CSU | `HB,<stateId>,<remote>` | heartbeat every 1 s |
| CSU → RSU | `START`, `STOP`, `RESET` | control (RESET clears the alarm) |
| CSU → RSU | `TAKE`, `RELEASE`, `ANGLE,<0..180>` | optional remote control of the radar |

---

## 9. CSU (PC, TypeScript) – data flow

```mermaid
flowchart LR
  A[Arduino RSU] <-- serial 115200 --> T[Transport<br/>SerialPort or Mock]
  T -- line --> PR[parseLine]
  PR --> M[RadarModel]
  M -- snapshot --> S[WebSocket server]
  S <-- JSON --> UI[Browser UI<br/>radar, table, controls]
  UI -- ClientCommand --> S
  S -- encodeCommand, validated --> T
```
