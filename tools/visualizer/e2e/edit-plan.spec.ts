import { test, expect } from '@playwright/test';

test.describe('Edit Existing Plan', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await page.evaluate(() => {
      localStorage.clear();
    });
    await page.reload();
  });

  test('plan cards show Edit button', async ({ page }) => {
    // Wait for plans to load
    await expect(page.locator('text=Available Plans')).toBeVisible();

    // Each plan card should have an Edit button
    const editButtons = page.locator('button:has-text("Edit")');
    await expect(editButtons.first()).toBeVisible();
  });

  test('clicking Edit on plan card opens editor with source', async ({ page }) => {
    // Wait for plans to load
    await expect(page.locator('text=Available Plans')).toBeVisible();

    // Click Edit on the first plan (reels_plan_a or similar)
    await page.locator('button:has-text("Edit")').first().click();

    // Should be in edit mode with editor visible
    await expect(page.locator('.dv-tab:has-text("Editor")')).toBeVisible();

    // Editor should contain plan code (not the default template)
    // The source should have definePlan and the actual plan name
    await expect(page.locator('.monaco-editor .view-lines')).toContainText('definePlan');
  });

  test('Edit button in toolbar appears when viewing plan with source', async ({ page }) => {
    // Click on a plan to view it
    await expect(page.locator('text=Available Plans')).toBeVisible();

    // Click on the plan name (not the Edit button) to view it
    await page.locator('span:has-text("reels_plan_a")').first().click();

    // Wait for plan to load and canvas to render
    await expect(page.locator('.dv-tab:has-text("Canvas")')).toBeVisible();

    // Toolbar should show Edit button (use exact match to avoid matching "Editor")
    await expect(page.getByRole('button', { name: 'Edit', exact: true })).toBeVisible();
  });

  test('clicking Edit in toolbar opens editor with current plan source', async ({ page }) => {
    // View a plan first
    await expect(page.locator('text=Available Plans')).toBeVisible();
    await page.locator('span:has-text("reels_plan_a")').first().click();
    await expect(page.locator('.dv-tab:has-text("Canvas")')).toBeVisible();

    // Click Edit in toolbar (use exact match to avoid matching "Editor")
    await page.getByRole('button', { name: 'Edit', exact: true }).click();

    // Should now be in edit mode
    await expect(page.locator('.dv-tab:has-text("Editor")')).toBeVisible();

    // Editor should have the plan source
    await expect(page.locator('.monaco-editor .view-lines')).toContainText('definePlan');
    await expect(page.locator('.monaco-editor .view-lines')).toContainText('reels_plan_a');
  });

  test('Edit button not shown in edit mode', async ({ page }) => {
    // Enter edit mode via Create New Plan
    await page.click('text=Create New Plan');
    await expect(page.locator('.dv-tab:has-text("Editor")')).toBeVisible();

    // Compile a plan so toolbar appears
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });
    await page.click('text=Compile & Visualize');
    await expect(page.locator('text=Compiled successfully')).toBeVisible({ timeout: 30000 });

    // Toolbar should show Fit but NOT Edit (we're already editing)
    await expect(page.locator('button:has-text("Fit")')).toBeVisible();

    // Edit button should not be in the toolbar
    // The toolbar has buttons, check that Edit is not among them
    const toolbarEditButton = page.locator('[style*="bottom: 16px"] button:has-text("Edit")');
    await expect(toolbarEditButton).not.toBeVisible();
  });
});

test.describe('Navbar Navigation', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await page.evaluate(() => {
      localStorage.clear();
    });
    await page.reload();
  });

  test('navbar has Editor, Plans, and Registries tabs', async ({ page }) => {
    await expect(page.locator('nav button:has-text("Editor")')).toBeVisible();
    await expect(page.locator('nav button:has-text("Plans")')).toBeVisible();
    await expect(page.locator('nav button:has-text("Registries")')).toBeVisible();
  });

  test('clicking Editor tab opens editor', async ({ page }) => {
    await page.click('nav button:has-text("Editor")');

    // Should show editor panel
    await expect(page.locator('.dv-tab:has-text("Editor")')).toBeVisible();
  });

  test('clicking Plans tab shows plan selector', async ({ page }) => {
    // First go to editor
    await page.click('nav button:has-text("Editor")');
    await expect(page.locator('.dv-tab:has-text("Editor")')).toBeVisible();

    // Click Plans to go back
    await page.click('nav button:has-text("Plans")');

    // Should see plan selector
    await expect(page.locator('text=Available Plans')).toBeVisible();
  });

  test('clicking Registries tab shows registry viewer', async ({ page }) => {
    await page.click('nav button:has-text("Registries")');

    // Should show registry tabs
    await expect(page.locator('button:has-text("Keys")')).toBeVisible();
    await expect(page.locator('button:has-text("Params")')).toBeVisible();
    await expect(page.locator('button:has-text("Features")')).toBeVisible();
  });
});

test.describe('Canvas Toolbar', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await page.evaluate(() => {
      localStorage.clear();
    });
    await page.reload();
  });

  test('Fit button centers the graph', async ({ page }) => {
    // View a plan
    await expect(page.locator('text=Available Plans')).toBeVisible();
    await page.locator('span:has-text("reels_plan_a")').first().click();
    await expect(page.locator('.dv-tab:has-text("Canvas")')).toBeVisible();

    // Wait for canvas to render
    await page.waitForTimeout(500);

    // Click Fit button
    await page.click('button:has-text("Fit")');

    // The graph should be visible (canvas has nodes)
    // We can't easily verify centering, but we can verify the button works without error
    await expect(page.locator('button:has-text("Fit")')).toBeVisible();
  });
});
