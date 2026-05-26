import type { ITerminalAddon, IDisposable, IMarker } from '@xterm/xterm';
import { Terminal } from '@xterm/xterm';

export class ShellIntegrationAddon implements ITerminalAddon {
  private terminal!: Terminal;
  private disposables: IDisposable[] = [];
  private outputStartMarker: IMarker | null = null;

  activate(terminal: Terminal): void {
    this.terminal = terminal;
    this.disposables.push(
      terminal.parser.registerOscHandler(133, data => this.handleOsc133(data))
    );
  }

  dispose(): void {
    for (const d of this.disposables) d.dispose();
    this.disposables.length = 0;
    this.clearMarker();
  }

  private clearMarker(): void {
    this.outputStartMarker?.dispose();
    this.outputStartMarker = null;
  }

  private handleOsc133(data: string): boolean {
    const type = data.charAt(0);
    switch (type) {
      case 'A':
      case 'D':
        this.clearMarker();
        return false;
      case 'C':
        this.clearMarker();
        if (this.terminal.buffer.active.type === 'normal') {
          this.outputStartMarker = this.terminal.registerMarker(0);
        }
        return false;
      case 'R':
        this.eraseOutput();
        return true;
      default:
        return false;
    }
  }

  private eraseOutput(): void {
    const marker = this.outputStartMarker;
    if (!marker || marker.line < 0) {
      this.clearMarker();
      return;
    }

    const buffer = this.terminal.buffer.active;
    if (buffer.type !== 'normal') {
      this.clearMarker();
      return;
    }

    const viewportRow = marker.line - buffer.viewportY;

    if (viewportRow < 0) {
      this.terminal.write('\x1b[1;1H\x1b[J');
    } else {
      this.terminal.write(`\x1b[${viewportRow + 1};1H\x1b[J\x1b[${viewportRow + 2};1H`);
    }
  }
}
