import type { ReactNode } from 'react';

import './modal.scss';

interface Props {
  show: boolean;
  children: ReactNode;
}

export function Modal(props: Props) {
  const { show, children } = props;

  return (
    show && (
      <div className="modal">
        <div className="modal-background" />
        <div className="modal-content">
          <div className="box">{children}</div>
        </div>
      </div>
    )
  );
}
