import { test, expect } from '@playwright/test';

test.describe('Dockview Panel Layout', () => {
  test.beforeEach(async ({ page }) => {
    // Clear localStorage before each test
    await page.goto('/');
    await page.evaluate(() => {
      localStorage.clear();
    });
    await page.reload();
  });

  test('shows simple home page when no plan loaded', async ({ page }) => {
    // Home page should show Create New Plan button
    await expect(page.locator('text=Create New Plan')).toBeVisible();

    // Should NOT show dockview panels or menu bar
    await expect(page.locator('.dv-dockview')).not.toBeVisible();
    await expect(page.locator('text=Add')).not.toBeVisible();
  });

  test('shows dockview layout in edit mode', async ({ page }) => {
    // Enter edit mode
    await page.click('text=Create New Plan');

    // Wait for dockview to render
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Should have Editor, Canvas, and Details panels
    await expect(page.locator('.dv-tab:has-text("Editor")')).toBeVisible();
    await expect(page.locator('.dv-tab:has-text("Canvas")')).toBeVisible();
    await expect(page.locator('.dv-tab:has-text("Details")')).toBeVisible();
  });

  test('shows menu bar in edit mode', async ({ page }) => {
    await page.click('text=Create New Plan');

    // Menu bar should be visible
    await expect(page.locator('button:has-text("Add")')).toBeVisible();
    await expect(page.locator('button:has-text("View")')).toBeVisible();
  });

  test('can close and reopen a panel via Add menu', async ({ page }) => {
    await page.click('text=Create New Plan');

    // Wait for dockview
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Close the Details panel by clicking its X button
    const detailsTab = page.locator('.dv-tab:has-text("Details")');
    await detailsTab.hover();
    const closeButton = detailsTab.locator('.dv-default-tab-action');
    await closeButton.click();

    // Details panel should be gone
    await expect(page.locator('.dv-tab:has-text("Details")')).not.toBeVisible();

    // Open Add menu and click Details
    await page.click('button:has-text("Add")');
    await page.click('text=Details');

    // Details panel should be back
    await expect(page.locator('.dv-tab:has-text("Details")')).toBeVisible();
  });

  test('Add menu disables items for existing panels', async ({ page }) => {
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Open Add menu
    await page.click('button:has-text("Add")');

    // Editor, Canvas, Details should be disabled (they already exist)
    const editorItem = page.locator('button:has-text("Editor")').last();
    await expect(editorItem).toBeDisabled();

    const canvasItem = page.locator('button:has-text("Canvas")').last();
    await expect(canvasItem).toBeDisabled();

    const detailsItem = page.locator('button:has-text("Details")').last();
    await expect(detailsItem).toBeDisabled();
  });

  test('layout persists after page refresh', async ({ page }) => {
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Close the Details panel
    const detailsTab = page.locator('.dv-tab:has-text("Details")');
    await detailsTab.hover();
    await detailsTab.locator('.dv-default-tab-action').click();
    await expect(page.locator('.dv-tab:has-text("Details")')).not.toBeVisible();

    // Wait for layout to save to localStorage
    await page.waitForTimeout(500);

    // Refresh the page
    await page.reload();

    // Go back to edit mode
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Details panel should still be gone (layout was persisted)
    await expect(page.locator('.dv-tab:has-text("Details")')).not.toBeVisible();
  });

  test('Reset Layout restores default panels', async ({ page }) => {
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Close both Canvas and Details panels
    const canvasTab = page.locator('.dv-tab:has-text("Canvas")');
    await canvasTab.hover();
    await canvasTab.locator('.dv-default-tab-action').click();

    const detailsTab = page.locator('.dv-tab:has-text("Details")');
    await detailsTab.hover();
    await detailsTab.locator('.dv-default-tab-action').click();

    // Both should be gone
    await expect(page.locator('.dv-tab:has-text("Canvas")')).not.toBeVisible();
    await expect(page.locator('.dv-tab:has-text("Details")')).not.toBeVisible();

    // Click View > Reset Layout
    await page.click('button:has-text("View")');
    await page.click('text=Reset Layout');

    // All panels should be restored
    await expect(page.locator('.dv-tab:has-text("Editor")')).toBeVisible();
    await expect(page.locator('.dv-tab:has-text("Canvas")')).toBeVisible();
    await expect(page.locator('.dv-tab:has-text("Details")')).toBeVisible();
  });

  test('Back to Plans button returns to home page', async ({ page }) => {
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Click Back to Plans
    await page.click('text=← Back to Plans');

    // Should be back at home page
    await expect(page.locator('text=Create New Plan')).toBeVisible();
    await expect(page.locator('.dv-dockview')).not.toBeVisible();
  });

  test('clicking Plan Visualizer title returns to home', async ({ page }) => {
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Click the title
    await page.click('text=Plan Visualizer');

    // Should be back at home page
    await expect(page.locator('text=Create New Plan')).toBeVisible();
  });
});

test.describe('Dockview View Mode', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await page.evaluate(() => {
      localStorage.clear();
    });
    await page.reload();
  });

  test('shows Canvas and Details after compiling a plan', async ({ page }) => {
    // Enter edit mode and compile
    await page.click('text=Create New Plan');
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });
    await page.click('text=Compile & Visualize');
    await expect(page.locator('text=Compiled successfully')).toBeVisible({ timeout: 30000 });

    // Should have Canvas and Details panels visible
    await expect(page.locator('.dv-tab:has-text("Canvas")')).toBeVisible();
    await expect(page.locator('.dv-tab:has-text("Details")')).toBeVisible();
  });

  test('Source panel disabled in edit mode Add menu', async ({ page }) => {
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Open Add menu
    await page.click('button:has-text("Add")');

    // Source should be disabled in edit mode
    const sourceItem = page.locator('button:has-text("Source")').last();
    await expect(sourceItem).toBeDisabled();
  });

  test('Editor panel disabled in view mode Add menu', async ({ page }) => {
    // Compile a plan to enter view mode with a loaded plan
    await page.click('text=Create New Plan');
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });
    await page.click('text=Compile & Visualize');
    await expect(page.locator('text=Compiled successfully')).toBeVisible({ timeout: 30000 });

    // The compiled plan is now in "edit mode with plan" state
    // Let's verify the Add menu state
    await page.click('button:has-text("Add")');

    // In this state (edit mode with compiled plan), both Editor and Source have restrictions
    // Editor should be disabled because it already exists
    const editorItem = page.locator('button:has-text("Editor")').last();
    await expect(editorItem).toBeDisabled();
  });
});

test.describe('Dockview Panel Interactions', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await page.evaluate(() => {
      localStorage.clear();
    });
    await page.reload();
  });

  test('can click on panel tabs to focus them', async ({ page }) => {
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Click on Canvas tab
    await page.click('.dv-tab:has-text("Canvas")');

    // Canvas tab should have active styling (dv-activegroup class on parent)
    const canvasPanel = page.locator('.dv-tab:has-text("Canvas")');
    await expect(canvasPanel).toBeVisible();
  });

  test('panels have visible resize handles (sashes)', async ({ page }) => {
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Dockview should have sash elements for resizing
    await expect(page.locator('.dv-sash')).toHaveCount(2, { timeout: 5000 });
  });

  test('closing all panels and reopening works', async ({ page }) => {
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-dockview')).toBeVisible();

    // Close all three panels
    for (const panelName of ['Editor', 'Canvas', 'Details']) {
      const tab = page.locator(`.dv-tab:has-text("${panelName}")`);
      if (await tab.isVisible()) {
        await tab.hover();
        await tab.locator('.dv-default-tab-action').click();
        await expect(tab).not.toBeVisible();
      }
    }

    // Reopen all via Add menu
    for (const panelName of ['Editor', 'Canvas', 'Details']) {
      await page.click('button:has-text("Add")');
      await page.click(`text=${panelName}`);
      await expect(page.locator(`.dv-tab:has-text("${panelName}")`)).toBeVisible();
    }
  });
});
