const CONTROL_ESCAPES: Record<number, string> = {
  0x00: '\\0',
  0x01: '\\x01',
  0x02: '\\x02',
  0x03: '\\x03',
  0x04: '\\x04',
  0x05: '\\x05',
  0x06: '\\x06',
  0x07: '\\a',
  0x08: '\\b',
  0x09: '\\t',
  0x0a: '\\n',
  0x0b: '\\v',
  0x0c: '\\f',
  0x0d: '\\r',
  0x0e: '\\x0e',
  0x0f: '\\x0f',
  0x10: '\\x10',
  0x11: '\\x11',
  0x12: '\\x12',
  0x13: '\\x13',
  0x14: '\\x14',
  0x15: '\\x15',
  0x16: '\\x16',
  0x17: '\\x17',
  0x18: '\\x18',
  0x19: '\\x19',
  0x1a: '\\x1a',
  0x1b: '\\e',
  0x1c: '\\x1c',
  0x1d: '\\x1d',
  0x1e: '\\x1e',
  0x1f: '\\x1f',
  0x7f: '\\x7f',
};

function utf8SeqLen(firstByte: number): number {
  if ((firstByte & 0xf8) === 0xf0) return 4;
  if ((firstByte & 0xf0) === 0xe0) return 3;
  if ((firstByte & 0xe0) === 0xc0) return 2;
  return 1;
}

/** For logging/debug only: raw bytes → string.
 *  - ASCII printable (0x20–0x7E) → literal char
 *  - UTF-8 sequences that decode cleanly → literal char(s)
 *  - Common control chars → named escape (\n, \r, \t, …)
 *  - Everything else → \xHH
 */
export function binaryToEscapedString(bytes: Uint8Array): string {
  const decoder = new TextDecoder('utf-8', { fatal: false });
  const parts: string[] = [];
  let i = 0;

  while (i < bytes.length) {
    const b = bytes[i]!;

    // 1) Named control char
    const named = CONTROL_ESCAPES[b];
    if (named !== undefined) {
      parts.push(named);
      i++;
      continue;
    }

    // 2) Single-byte printable ASCII
    if (b >= 0x20 && b <= 0x7e) {
      parts.push(String.fromCharCode(b));
      i++;
      continue;
    }

    // 3) Try multi-byte UTF-8
    const len = utf8SeqLen(b);
    if (len > 1 && i + len <= bytes.length) {
      const slice = bytes.slice(i, i + len);
      const s = decoder.decode(slice);
      // Clean decode = no U+FFFD replacement char
      if (!s.includes('\uFFFD')) {
        parts.push(s);
        i += len;
        continue;
      }
    }

    // 4) Fallback: hex escape
    parts.push('\\x' + b.toString(16).padStart(2, '0'));
    i++;
  }

  return parts.join('');
}
