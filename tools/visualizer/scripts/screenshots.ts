import { chromium, Browser, Page } from 'playwright';
import { spawn, ChildProcess } from 'child_process';
import { mkdir } from 'fs/promises';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const SCREENSHOTS_DIR = join(__dirname, '..', 'screenshots');
const PORT = 5175; // Vite dev server port (preview doesn't have artifacts since copyPublicDir: false)
const BASE_URL = `http://localhost:${PORT}`;

async function waitForServer(url: string, timeout = 30000): Promise<void> {
  const start = Date.now();
  while (Date.now() - start < timeout) {
    try {
      const res = await fetch(url);
      if (res.ok) return;
    } catch {
      // Server not ready yet
    }
    await new Promise((r) => setTimeout(r, 500));
  }
  throw new Error(`Server at ${url} did not start within ${timeout}ms`);
}

async function captureScreenshots(page: Page): Promise<void> {
  // 1. Plan selector (landing page)
  await page.goto(BASE_URL);
  await page.waitForSelector('text=Available Plans', { timeout: 10000 }).catch(() => {
    // Might show "No plans" if index.json not available
  });
  await page.waitForTimeout(500);
  await page.screenshot({
    path: join(SCREENSHOTS_DIR, '01-plan-selector.png'),
    fullPage: true,
  });
  console.log('Captured: 01-plan-selector.png');

  // 2. Try to load a plan (if available)
  const planLink = page.locator('button:has-text("concat")').first();
  if (await planLink.isVisible().catch(() => false)) {
    await planLink.click();
    await page.waitForTimeout(1000); // Wait for graph to render
    await page.screenshot({
      path: join(SCREENSHOTS_DIR, '02-graph-view.png'),
      fullPage: true,
    });
    console.log('Captured: 02-graph-view.png');

    // 3. Click on a node to show details
    const canvas = page.locator('canvas');
    if (await canvas.isVisible()) {
      // Click roughly in center where a node might be
      const box = await canvas.boundingBox();
      if (box) {
        await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
        await page.waitForTimeout(500);
        await page.screenshot({
          path: join(SCREENSHOTS_DIR, '03-node-selected.png'),
          fullPage: true,
        });
        console.log('Captured: 03-node-selected.png');
      }
    }

    // 4. Open source panel if available
    const sourceButton = page.locator('button:has-text("Source")');
    if (await sourceButton.isVisible().catch(() => false)) {
      await sourceButton.click();
      await page.waitForTimeout(500);
      await page.screenshot({
        path: join(SCREENSHOTS_DIR, '04-source-panel.png'),
        fullPage: true,
      });
      console.log('Captured: 04-source-panel.png');
    }
  } else {
    console.log('No plans available, skipping graph screenshots');
  }
}

async function main(): Promise<void> {
  // Create screenshots directory
  await mkdir(SCREENSHOTS_DIR, { recursive: true });

  // Start dev server (preview doesn't have artifacts since copyPublicDir: false)
  console.log('Starting dev server...');
  const server: ChildProcess = spawn('pnpm', ['run', 'dev', '--', '--open', 'false'], {
    cwd: join(__dirname, '..'),
    stdio: 'pipe',
    shell: true,
  });

  let browser: Browser | null = null;

  try {
    // Wait for server to be ready
    console.log(`Waiting for server at ${BASE_URL}...`);
    await waitForServer(BASE_URL);
    console.log('Server ready!');

    // Launch browser
    browser = await chromium.launch();
    const context = await browser.newContext({
      viewport: { width: 1280, height: 800 },
    });
    const page = await context.newPage();

    // Capture screenshots
    await captureScreenshots(page);

    console.log('\nScreenshots saved to:', SCREENSHOTS_DIR);
  } finally {
    // Cleanup
    if (browser) await browser.close();
    server.kill();
  }
}

main().catch((err) => {
  console.error('Screenshot capture failed:', err);
  process.exit(1);
});
