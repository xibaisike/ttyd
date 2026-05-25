import { test, expect } from '@playwright/test';
import { waitForTerminal, typeInTerminal, createSession, expectTerminalOutput } from './helpers';

test.describe('Session Mode', () => {
  test.beforeEach(async ({ request }) => {
    const res = await request.get('/api/sessions?pageSize=100&pageNumber=1');
    if (res.ok()) {
      const data = await res.json();
      for (const s of data.sessions || []) {
        await request.delete(`/api/sessions/${encodeURIComponent(s.name)}`);
      }
    }
  });

  test('page loads at /?mode=session', async ({ page }) => {
    await page.goto('/?mode=session');
    await expect(page.locator('.session-layout')).toBeVisible();
  });

  test('sidebar renders with New Session UI', async ({ page }) => {
    await page.goto('/?mode=session');
    await expect(page.locator('.new-session-form input')).toBeVisible();
    await expect(page.locator('.new-session-form button')).toBeVisible();
  });

  test('create session appears in list', async ({ page }) => {
    await page.goto('/?mode=session');
    await createSession(page, 'test-sess-1');
    await expect(page.locator('.session-item')).toHaveCount(1, { timeout: 5000 });
    await expect(page.locator('.session-name')).toContainText('test-sess-1');
  });

  test('click session connects terminal', async ({ page }) => {
    await page.clock.install();
    await page.goto('/?mode=session');
    await createSession(page, 'test-sess-2');
    await expect(page.locator('.session-item')).toHaveCount(1, { timeout: 5000 });
    await page.locator('.session-item').click();
    await Promise.all([page.clock.fastForward(5000) ,waitForTerminal(page)]);
    await expect(page.locator('.xterm')).toBeVisible();
    await expect(page.locator('.session-item')).toHaveCount(1, { timeout: 5000 });
  });

  test('type in terminal shows output', async ({ page }) => {
    await page.goto('/?mode=session');
    await createSession(page, 'test-sess-3');
    await page.locator('.session-item').click();
    await waitForTerminal(page);
    await page.waitForTimeout(500);
    await typeInTerminal(page, 'echo sess-output\n');
    await expectTerminalOutput(page, 'sess-output');
  });

  test('switch sessions remounts terminal', async ({ page }) => {
    await page.goto('/?mode=session');
    await createSession(page, 'sess-a');
    await expect(page.locator('.session-item')).toHaveCount(1, { timeout: 5000 });
    await createSession(page, 'sess-b');
    await expect(page.locator('.session-item')).toHaveCount(2, { timeout: 5000 });

    await page.locator('.session-item').nth(0).click();
    await waitForTerminal(page);

    await page.locator('.session-item').nth(1).click();
    await waitForTerminal(page);
    await expect(page.locator('.xterm')).toBeVisible();
  });

  test('delete session removes from list', async ({ page }) => {
    await page.goto('/?mode=session');
    await createSession(page, 'del-sess');
    await expect(page.locator('.session-item')).toHaveCount(1, { timeout: 5000 });

    // Double-click delete (confirm pattern)
    await page.locator('.session-item button').click();
    await page.locator('.session-item button.confirm').click();

    await expect(page.locator('.session-item')).toHaveCount(0, { timeout: 5000 });
  });

  test('placeholder shown when no session selected', async ({ page }) => {
    await page.goto('/?mode=session');
    await expect(page.locator('.session-placeholder')).toContainText('Select or create a session');
  });
});
