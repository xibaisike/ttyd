import type { ITerminalOptions, ITheme } from '@xterm/xterm';

import { Terminal } from './components/terminal';
import { SessionLayout } from './components/session';
import type { ClientOptions, FlowControl } from './components/terminal/xterm';

const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
const path = window.location.pathname.replace(/[/]+$/, '');
const wsUrl = [protocol, '//', window.location.host, path, '/ws', window.location.search].join('');
const wsBase = [protocol, '//', window.location.host, path, '/ws'].join('');
const tokenUrl = [window.location.protocol, '//', window.location.host, path, '/token'].join('');
const clientOptions: ClientOptions = {
  rendererType: 'dom',
  disableLeaveAlert: false,
  disableResizeOverlay: false,
  enableZmodem: false,
  enableTrzsz: false,
  enableSixel: false,
  closeOnDisconnect: false,
  isWindows: false,
  unicodeVersion: '11',
  trzszDragInitTimeout: 300,
};
const termOptions: ITerminalOptions = {
  fontSize: 13,
  fontFamily: 'Consolas,Liberation Mono,Menlo,Courier,PingFang SC,Microsoft YaHei,WenQuanYi Micro Hei Mono,Noto Sans Mono CJK SC,monospace',
  lineHeight: 1.2,
  theme: {
    foreground: '#d2d2d2',
    background: '#2b2b2b',
    cursor: '#adadad',
    black: '#000000',
    red: '#d81e00',
    green: '#5ea702',
    yellow: '#cfae00',
    blue: '#427ab3',
    magenta: '#89658e',
    cyan: '#00a7aa',
    white: '#dbded8',
    brightBlack: '#686a66',
    brightRed: '#f54235',
    brightGreen: '#99e343',
    brightYellow: '#fdeb61',
    brightBlue: '#84b0d8',
    brightMagenta: '#bc94b7',
    brightCyan: '#37e6e8',
    brightWhite: '#f1f1f0',
  } as ITheme,
  allowProposedApi: true,
};
const flowControl: FlowControl = {
  limit: 100000,
  highWater: 10,
  lowWater: 4,
};

const mode = new URLSearchParams(window.location.search).get('mode');

export default function App() {
  if (mode === 'session') {
    return (
      <SessionLayout
        wsBase={wsBase}
        tokenUrl={tokenUrl}
        clientOptions={clientOptions}
        termOptions={termOptions}
        flowControl={flowControl}
      />
    );
  }

  return (
    <Terminal
      id="terminal-container"
      wsUrl={wsUrl}
      tokenUrl={tokenUrl}
      clientOptions={clientOptions}
      termOptions={termOptions}
      flowControl={flowControl}
    />
  );
}
