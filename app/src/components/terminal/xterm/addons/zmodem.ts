// zmodem_browser ships without TypeScript typings.
/* eslint-disable @typescript-eslint/no-explicit-any */
import { saveAs } from 'file-saver';
import type { IDisposable, ITerminalAddon } from '@xterm/xterm';
import { Terminal } from '@xterm/xterm';
import * as Zmodem from 'zmodem.js/src/zmodem_browser';
import { TrzszFilter } from 'trzsz';

export interface ZmodeOptions {
  zmodem: boolean;
  trzsz: boolean;
  windows: boolean;
  trzszDragInitTimeout: number;
  onSend: () => void;
  sender: (data: string | Uint8Array) => void;
  writer: (data: string | Uint8Array) => void;
}

export class ZmodemAddon implements ITerminalAddon {
  private disposables: IDisposable[] = [];
  private terminal!: Terminal;
  private sentry!: Zmodem.Sentry;
  private session: Zmodem.Session | null = null;
  private denier?: () => void;
  private trzszFilter!: TrzszFilter;

  constructor(private options: ZmodeOptions) {}

  activate(terminal: Terminal) {
    this.terminal = terminal;
    if (this.options.zmodem) this.zmodemInit();
    if (this.options.trzsz) this.trzszInit();
  }

  dispose() {
    for (const d of this.disposables) {
      d.dispose();
    }
    this.disposables.length = 0;
  }

  consume(data: ArrayBuffer) {
    try {
      if (this.options.trzsz) {
        this.trzszFilter.processServerOutput(data);
      } else {
        this.sentry.consume(data);
      }
    } catch (e) {
      console.error('[ttyd] zmodem consume: ', e);
      this.reset();
    }
  }

  private reset = (): void => {
    this.terminal.options.disableStdin = false;
    this.terminal.focus();
  };

  private addDisposableListener(target: EventTarget, type: string, listener: EventListener) {
    target.addEventListener(type, listener);
    this.disposables.push({ dispose: () => target.removeEventListener(type, listener) });
  }

  private trzszInit = (): void => {
    const { terminal } = this;
    const { sender, writer, zmodem } = this.options;
    this.trzszFilter = new TrzszFilter({
      writeToTerminal: data => {
        if (!this.trzszFilter.isTransferringFiles() && zmodem) {
          this.sentry.consume(data as ArrayBuffer);
        } else {
          writer(typeof data === 'string' ? data : new Uint8Array(data as ArrayBuffer));
        }
      },
      sendToServer: data => sender(data),
      terminalColumns: terminal.cols,
      isWindowsShell: this.options.windows,
      dragInitTimeout: this.options.trzszDragInitTimeout,
    });
    const element = terminal.element as EventTarget;
    this.addDisposableListener(element, 'dragover', event => event.preventDefault());
    this.addDisposableListener(element, 'drop', event => {
      event.preventDefault();
      const items = (event as DragEvent).dataTransfer?.items;
      if (!items) return;
      this.trzszFilter
        .uploadFiles(items as DataTransferItemList)
        .then(() => console.log('[ttyd] upload success'))
        .catch(err => console.log('[ttyd] upload failed: ' + String(err)));
    });
    this.disposables.push(
      terminal.onResize(size => {
        this.trzszFilter.setTerminalColumns(size.cols);
      }),
    );
  };

  private zmodemInit = (): void => {
    const { sender, writer } = this.options;
    const { terminal, reset } = this;
    this.session = null;
    this.sentry = new Zmodem.Sentry({
      to_terminal: octets => writer(new Uint8Array(octets)),
      sender: octets => sender(new Uint8Array(octets)),
      on_retract: () => reset(),
      on_detect: detection => this.zmodemDetect(detection),
    });
    this.disposables.push(
      terminal.onKey(e => {
        const evt = e.domEvent;
        if (evt.ctrlKey && evt.key === 'c') {
          this.denier?.();
        }
      }),
    );
  };

  private zmodemDetect = (detection: Zmodem.Detection): void => {
    const { terminal } = this;
    terminal.options.disableStdin = true;

    this.denier = (): void => detection.deny();
    this.session = detection.confirm();
    this.session.on('session_end', () => this.reset());

    if (this.session.type === 'send') {
      this.options.onSend();
    } else {
      this.receiveFile();
    }
  };

  sendFile = (files: FileList): void => {
    const { session } = this;
    if (!session) return;
    Zmodem.Browser.send_files(session, files, {
      // eslint-disable-next-line @typescript-eslint/no-unused-vars
      on_progress: (_obj, offer) => this.writeProgress(offer),
    })
      .then(() => session.close())
      .catch(() => this.reset());
  };

  private receiveFile = (): void => {
    const session = this.session;
    if (!session) return;

    session.on('offer', offer => {
      offer.on('input', (): void => this.writeProgress(offer));
      offer
        .accept()
        .then(payloads => {
          const blob = new Blob(payloads, { type: 'application/octet-stream' });
          saveAs(blob, offer.get_details().name);
        })
        .catch(() => this.reset());
    });

    session.start();
  };

  private writeProgress = (offer: Zmodem.Offer): void => {
    const file = offer.get_details();
    const name = file.name;
    const size = file.size;
    const offset = offer.get_offset();
    const percent = ((100 * offset) / size).toFixed(2);

    this.options.writer(`${name} ${percent}% ${this.bytesHuman(offset, 2)}/${this.bytesHuman(size, 2)}\r`);
  };

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private bytesHuman(bytes: any, precision: number): string {
    if (!/^([-+])?|(\.\d+)(\d+(\.\d+)?|(\d+\.)|Infinity)$/.test(bytes)) {
      return '-';
    }
    if (bytes === 0) return '0';
    let p = precision;
    if (typeof p === 'undefined') p = 1;
    const units = ['bytes', 'KB', 'MB', 'GB', 'TB', 'PB'];
    const num = Math.floor(Math.log(bytes) / Math.log(1024));
    const value = (bytes / Math.pow(1024, Math.floor(num))).toFixed(p);
    return `${value} ${units[num]}`;
  }
}
