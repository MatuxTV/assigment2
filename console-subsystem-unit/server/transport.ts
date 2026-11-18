// A Transport is "a line-oriented link to the RSU": the real serial port
// or the built-in simulator. The rest of the console does not care which.

export interface Transport {
  /** called for every complete line received (without the newline) */
  onLine(cb: (line: string) => void): void;
  /** called when the link goes up / down */
  onStatus(cb: (connected: boolean) => void): void;
  /** send one line (the '\n' is added by the transport) */
  write(line: string): void;
  open(): void;
  close(): void;
}

/** Real Arduino on a serial port. Reconnects automatically. */
export class SerialTransport implements Transport {
  private lineCb: (l: string) => void = () => {};
  private statusCb: (c: boolean) => void = () => {};
  private port: import('serialport').SerialPort | null = null;
  private timer: NodeJS.Timeout | null = null;
  private closed = false;

  constructor(private readonly path: string, private readonly baud = 115200) {}

  onLine(cb: (line: string) => void): void { this.lineCb = cb; }
  onStatus(cb: (connected: boolean) => void): void { this.statusCb = cb; }

  open(): void {
    this.closed = false;
    void this.connect();
  }

  private async connect(): Promise<void> {
    if (this.closed) return;
    // imported lazily so that --mock works even without the native module
    const { SerialPort } = await import('serialport');
    const { ReadlineParser } = await import('@serialport/parser-readline');

    const port = new SerialPort({ path: this.path, baudRate: this.baud, autoOpen: false });
    port.on('error', (e) => console.error(`[serial] ${e.message}`));
    port.on('close', () => {
      this.port = null;
      this.statusCb(false);
      this.scheduleReconnect();
    });
    port.pipe(new ReadlineParser({ delimiter: '\n' })).on('data', (l: string) => this.lineCb(l.replace(/\r$/, '')));

    port.open((err) => {
      if (err) {
        console.error(`[serial] cannot open ${this.path}: ${err.message}`);
        this.scheduleReconnect();
        return;
      }
      this.port = port;
      this.statusCb(true);
    });
  }

  private scheduleReconnect(): void {
    if (this.closed || this.timer) return;
    this.timer = setTimeout(() => { this.timer = null; void this.connect(); }, 2000);
  }

  write(line: string): void {
    this.port?.write(line + '\n');
  }

  close(): void {
    this.closed = true;
    if (this.timer) clearTimeout(this.timer);
    this.port?.close();
  }
}
