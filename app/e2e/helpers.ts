import { expect, type Page } from '@playwright/test';

export async function waitForTerminal(page: Page) {
  await page.waitForSelector('.xterm-helper-textarea', { state: 'visible' });
}

export async function typeInTerminal(page: Page, text: string) {
  const textarea = page.locator('.xterm-helper-textarea');
  await textarea.focus();
  await textarea.pressSequentially(text, { delay: 50 });
}

export async function createSession(page: Page, name: string) {
  const input = page.locator('.new-session-form input');
  await input.fill(name);
  await page.locator('.new-session-form button').click();
}

export async function waitForWs(page: Page) {
  await page.waitForEvent('websocket', ws => ws.url().includes('/ws'));
}

export async function expectTerminalOutput(
  page: Page,
  text: string,
  options: { timeout?: number } = {}
) {
  const { timeout = 5000 } = options;
  await expect(page.locator('.terminal.xterm .xterm-rows')).toContainText(text, { timeout });
}
