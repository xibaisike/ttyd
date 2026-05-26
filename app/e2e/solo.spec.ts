import { test, expect } from '@playwright/test';
import { waitForTerminal, typeInTerminal, expectTerminalOutput } from './helpers';

test.describe('Solo Mode', () => {
  test('page loads at /', async ({ page }) => {
    await page.goto('/');
    await expect(page).toHaveURL(/localhost:7681/);
  });

  test('terminal renders', async ({ page }) => {
    await page.goto('/');
    await waitForTerminal(page);
    await expect(page.locator('.xterm')).toBeVisible();
  });

  test('can type and see output', async ({ page }) => {
    await page.goto('/');
    await waitForTerminal(page);
    await page.waitForTimeout(500);
    await typeInTerminal(page, 'echo hello-e2e\n');
    await expectTerminalOutput(page, 'hello-e2e');
  });

  test('WebSocket connection established', async ({ page }) => {
    const wsPromise = page.waitForEvent('websocket');
    await page.goto('/');
    const ws = await wsPromise;
    expect(ws.url()).toContain('/ws');
  });

  test('window title updates', async ({ page }) => {
    await page.goto('/');
    await waitForTerminal(page);
    await page.waitForTimeout(1000);
    const title = await page.title();
    expect(title.length).toBeGreaterThan(0);
  });
});
