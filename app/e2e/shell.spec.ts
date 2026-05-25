import { test, expect } from '@playwright/test';
import { waitForTerminal, typeInTerminal } from './helpers';

test.describe('OSC 133 Shell Integration', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await waitForTerminal(page);
    await page.waitForTimeout(500);
  });

  test('R erases output between C and R', async ({ page }) => {
    // Use tr to produce output ("ERASETEST") that doesn't appear in the typed command
    await typeInTerminal(page, "printf '\\033]133;C\\007'; echo erasetest | tr a-z A-Z; printf '\\033]133;R\\007'\n");
    await page.waitForTimeout(1000);
    const rows = page.locator('.xterm-rows');
    await expect(rows).not.toContainText('ERASETEST');
  });

  test('R with no prior C is a no-op', async ({ page }) => {
    // Emit A first to clear any marker zsh's prompt integration set
    await typeInTerminal(page, "printf '\\033]133;A\\007'; echo beforeR | tr a-z A-Z; printf '\\033]133;R\\007'; echo afterR | tr a-z A-Z\n");
    await page.waitForTimeout(1000);
    const rows = page.locator('.xterm-rows');
    await expect(rows).toContainText('BEFORER');
    await expect(rows).toContainText('AFTERR');
  });

  test('A clears stale marker so R does not erase wrong content', async ({ page }) => {
    await typeInTerminal(page, "printf '\\033]133;C\\007'; echo protected | tr a-z A-Z; printf '\\033]133;A\\007'; printf '\\033]133;R\\007'\n");
    await page.waitForTimeout(1000);
    const rows = page.locator('.xterm-rows');
    await expect(rows).toContainText('PROTECTED');
  });

  test('D clears stale marker so R does not erase wrong content', async ({ page }) => {
    await typeInTerminal(page, "printf '\\033]133;C\\007'; echo protected | tr a-z A-Z; printf '\\033]133;D\\007'; printf '\\033]133;R\\007'\n");
    await page.waitForTimeout(1000);
    const rows = page.locator('.xterm-rows');
    await expect(rows).toContainText('PROTECTED');
  });

  test('R erases multi-line output', async ({ page }) => {
    await typeInTerminal(
      page,
      "printf '\\033]133;C\\007'; for i in $(seq 1 5); do echo \"output_$i\" | tr a-z A-Z; done; printf '\\033]133;R\\007'\n"
    );
    await page.waitForTimeout(1000);
    const rows = page.locator('.xterm-rows');
    for (let i = 1; i <= 5; i++) {
      await expect(rows).not.toContainText(`OUTPUT_${i}`);
    }
  });

  test('second C replaces first — only latest output erased', async ({ page }) => {
    await typeInTerminal(
      page,
      "printf '\\033]133;C\\007'; echo keepthis | tr a-z A-Z; printf '\\033]133;C\\007'; echo erasethis | tr a-z A-Z; printf '\\033]133;R\\007'\n"
    );
    await page.waitForTimeout(1000);
    const rows = page.locator('.xterm-rows');
    await expect(rows).toContainText('KEEPTHIS');
    await expect(rows).not.toContainText('ERASETHIS');
  });

  test('R works after terminal resize', async ({ page }) => {
    // C and R in the same command with a sleep in between for resize
    await typeInTerminal(page, "printf '\\033]133;C\\007'; echo resizeout | tr a-z A-Z; sleep 3; printf '\\033]133;R\\007'\n");
    await page.waitForTimeout(500);
    const rows = page.locator('.xterm-rows');
    await expect(rows).not.toContainText('RESIZEOUT');

    // Resize viewport during the sleep to trigger terminal reflow
    await page.setViewportSize({ width: 600, height: 400 });

    // Wait for sleep + R to complete
    await page.waitForTimeout(1000);
    await expect(rows).not.toContainText('RESIZEOUT');
  });
});
