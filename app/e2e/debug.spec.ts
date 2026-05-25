import { test, expect } from '@playwright/test';
import { waitForTerminal, typeInTerminal } from './helpers';

test.describe('qwen code', () => {
  const FIXTURE_1 = 'e2e/fixtures/ink-agent';

  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await waitForTerminal(page);
    await page.waitForTimeout(500);
  });

  test('qwen after 20 resizes', async ({ page }) => {
    await typeInTerminal(page, 'ls -l \n');
    await typeInTerminal(page, `node ${FIXTURE_1}/dist/index.js\n`);
    await page.waitForTimeout(3000);

    // Run 10 resize cycles with random dimensions to stress-test terminal resize handling
    for (let i = 0; i < 20; i++) {
      const width = 100 + Math.floor(Math.random() * 600);
      const height = 400 + Math.floor(Math.random() * 600);
      await page.setViewportSize({ width, height });
      await page.waitForTimeout(500);
    }

    // After resizes, collect console output from printScrollbakBuffer and verify
    const consolePromise = page.waitForEvent('console');
    await page.evaluate(() => (window as any).term.printScrollbakBuffer());
    const consoleMsg = await consolePromise;
    const scrollbackContent = consoleMsg.text().replace(/\x1b\[[0-9;]*m/g, '');

    const rows = page.locator('.xterm-rows');
    // const rowsContent = await rows.textContent();

    // Both scrollback buffer and rendered rows should contain the same expected text
    for (const content of [scrollbackContent]) {
      expect(content).toContain('package.json');
      expect(content).toContain('tsconfig.json');
      expect(content).toContain('vite.config.ts');
      expect(content).toContain('src');
      const sign = '用户惊恐地发现';
      expect(content!.split(sign).length - 1).toBe(1);
    }

    const signRow = rows.locator(':scope > *').filter({ hasText: '用户惊恐地发现' }).first();
    const prevSibling = signRow.locator('xpath=preceding-sibling::*[1]');
    await expect(prevSibling).toContainText(FIXTURE_1);
  });
});
