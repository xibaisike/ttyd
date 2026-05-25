declare module 'zmodem.js/src/zmodem_browser' {
  interface OfferDetail {
    name: string;
    size: number;
  }

  export interface Offer {
    get_details(): OfferDetail;
    get_offset(): number;
    on(event: 'input', cb: () => void): void;
    accept(): Promise<BlobPart[]>;
  }

  export interface Session {
    type: string;
    close(): void;
    start(): void;
    on(event: 'session_end', cb: () => void): void;
    on(event: 'offer', cb: (offer: Offer) => void): void;
  }

  export interface Detection {
    deny(): void;
    confirm(): Session;
  }

  export class Sentry {
    constructor(opts: {
      to_terminal: (octets: Iterable<number>) => void;
      sender: (octets: Iterable<number>) => void;
      on_retract: () => void;
      on_detect: (detection: Detection) => void;
    });
    consume(data: ArrayBuffer): void;
  }

  export const Browser: {
    send_files(
      session: Session,
      files: FileList,
      opts: {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        on_progress: (obj: any, offer: Offer) => void;
      },
    ): Promise<void>;
  };
}
