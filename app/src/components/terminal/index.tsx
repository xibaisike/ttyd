import { Component, type ChangeEvent } from 'react';

import { Modal } from '../modal/index';
import type { XtermOptions } from './xterm';
import { Xterm } from './xterm';
import './xterm.scss';

interface Props extends XtermOptions {
  id: string;
}

interface State {
  modal: boolean;
}

export class Terminal extends Component<Props, State> {
  private container!: HTMLElement;
  private readonly xterm: Xterm;

  constructor(props: Props) {
    super(props);
    this.state = { modal: false };
    this.xterm = new Xterm(props, this.showModal);
  }

  override async componentDidMount() {
    await this.xterm.refreshToken();
    // Defer open+connect to the next animation frame so the container is
    // guaranteed to be laid out and visible before xterm/FitAddon measure it.
    // Without this, a display:none ancestor (e.g. an inactive tab that becomes
    // active in the same render batch) causes FitAddon to see 0-size dimensions
    // and xterm's ResizeObserver delays initialization by up to ~5 seconds.
    requestAnimationFrame(() => {
      this.xterm.open(this.container);
      this.xterm.connect();
    });
  }

  override componentWillUnmount() {
    this.xterm.dispose();
  }

  showModal = (): void => {
    this.setState({ modal: true });
  };

  sendFile = (event: ChangeEvent<HTMLInputElement>): void => {
    this.setState({ modal: false });
    const files = event.target.files;
    if (files) this.xterm.sendFile(files);
  };

  render() {
    const { id } = this.props;
    const { modal } = this.state;

    return (
      <div
        id={id}
        className='xterm-container'
        ref={el => {
          if (el) this.container = el;
        }}
      >
        <Modal show={modal}>
          <label className="file-label">
            <input type="file" className="file-input" multiple onChange={this.sendFile} />
            <span className="file-cta">Choose files…</span>
          </label>
        </Modal>
      </div>
    );
  }
}
