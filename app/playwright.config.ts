import { defineConfig } from '@playwright/test';

const cwd = import.meta.dirname;

export default defineConfig({
  testDir: './e2e',
  timeout: 30000,
  expect: { timeout: 5000 },
  fullyParallel: false,
  retries: 0,
  reporter: [['list'], ['html', { open: 'never' }]],
  use: {
    baseURL: 'http://localhost:7681',
    trace: 'on-first-retry',
  },
  projects: [
    {
      name: 'chromium',
      use: { browserName: 'chromium' },
    },
  ],
  webServer: {
    timeout: 4000,
    command: `../build/ttyd -w ${cwd} -I ./dist/index.html -D ./dist --writable zsh`,
    url: 'http://localhost:7681',
    reuseExistingServer: !process.env.CI,
    env: {
      log_level: 'debug',
      log_output: 'file',
      log_file: '/tmp/ttyd.log',
    },
  },
});
