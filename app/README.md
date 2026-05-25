# ttyd web UI (React 18 + Vite)

## Develop

1. Start ttyd (e.g. `ttyd bash` on port 7681).
2. From this directory: `yarn install`, then `yarn dev` (default port 9000; proxies `/token` and `/ws` to localhost:7681).

## Build

`yarn build` outputs `dist/` with hashed JS/CSS (not a single inlined document).

## Embedded `html.h` / default binary UI

This project does **not** regenerate [`../src/html.h`](../src/html.h). The C server still uses the existing generated header when no custom index is provided.

To serve this build from ttyd instead:

```bash
ttyd -I /path/to/app/dist/index.html -D /path/to/app/dist ...
```

Use `base: './'` in [`vite.config.ts`](vite.config.ts) so asset URLs resolve under subpaths when needed.
