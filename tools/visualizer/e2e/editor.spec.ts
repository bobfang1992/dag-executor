import { test, expect } from '@playwright/test';

test.describe('Live Plan Editor', () => {
  test.beforeEach(async ({ page }) => {
    // Clear localStorage before each test
    await page.goto('/');
    await page.evaluate(() => {
      localStorage.clear();
    });
    await page.reload();
  });

  test('loads with default plan template', async ({ page }) => {
    // Click "New Plan" button to open editor
    await page.click('text=New Plan');

    // Wait for editor panel to appear
    await expect(page.locator('text=Compile & Visualize')).toBeVisible();

    // Editor should contain default plan code (check Monaco's view-lines)
    await expect(page.locator('.monaco-editor .view-lines')).toContainText('definePlan');
    await expect(page.locator('.monaco-editor .view-lines')).toContainText('my_plan');
  });

  test('compiles plan and shows DAG visualization', async ({ page }) => {
    await page.click('text=New Plan');

    // Wait for compiler to be ready (button becomes enabled)
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });

    // Click compile
    await page.click('text=Compile & Visualize');

    // Wait for compilation to complete - status should show success
    await expect(page.locator('text=Compiled successfully')).toBeVisible({ timeout: 30000 });

    // DAG canvas should be visible (use the PixiJS canvas specifically)
    await expect(page.locator('[data-testid="dag-canvas"], .pixi-canvas')).toBeVisible({ timeout: 5000 });
  });

  test('shows error for invalid plan code', async ({ page }) => {
    await page.click('text=New Plan');
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });

    // Focus Monaco editor and type invalid code
    await page.locator('.monaco-editor').click();
    // Select all and delete
    await page.keyboard.press('Meta+a');
    await page.keyboard.type('invalid typescript {{{', { delay: 10 });

    await page.click('text=Compile & Visualize');

    // Should show error banner
    await expect(page.locator('[style*="background"][style*="ff"]')).toBeVisible({ timeout: 30000 });
  });

  test('saves plan with Save As', async ({ page }) => {
    await page.click('text=New Plan');
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });

    // Click Save As
    await page.click('button:has-text("Save As")');

    // Modal should appear
    await expect(page.locator('text=Save Plan')).toBeVisible();

    // Enter name and save
    await page.locator('input[placeholder="Plan name"]').fill('test_plan_1');
    await page.locator('button:has-text("Save")').last().click();

    // Toast should appear
    await expect(page.locator('text=Saved "test_plan_1"')).toBeVisible();

    // Plan should appear in dropdown
    await page.click('[data-testid="dropdown-trigger"]');
    await expect(page.locator('text=test_plan_1')).toBeVisible();
  });

  test('switches between saved plans', async ({ page }) => {
    await page.click('text=New Plan');
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });

    // Save first plan
    await page.click('button:has-text("Save As")');
    await page.locator('input[placeholder="Plan name"]').fill('plan_a');
    await page.locator('button:has-text("Save")').last().click();
    await page.waitForTimeout(500);

    // Modify code in Monaco
    await page.locator('.monaco-editor').click();
    await page.keyboard.press('Meta+a');
    await page.keyboard.type('// Plan B code', { delay: 5 });

    // Save as second plan
    await page.click('button:has-text("Save As")');
    await page.locator('input[placeholder="Plan name"]').fill('plan_b');
    await page.locator('button:has-text("Save")').last().click();
    await page.waitForTimeout(500);

    // Switch back to plan_a via dropdown
    await page.click('[data-testid="dropdown-trigger"]');
    await page.click('text=plan_a');

    // Code should switch back to original
    await expect(page.locator('.monaco-editor .view-lines')).toContainText('definePlan');
  });

  test('renames a saved plan', async ({ page }) => {
    await page.click('text=New Plan');
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });

    // Save plan
    await page.click('button:has-text("Save As")');
    await page.locator('input[placeholder="Plan name"]').fill('old_name');
    await page.locator('button:has-text("Save")').last().click();
    await page.waitForTimeout(500);

    // Click Rename
    await page.click('button:has-text("Rename")');

    // Modal should appear
    await expect(page.locator('text=Rename Plan')).toBeVisible();

    // Clear and enter new name
    await page.locator('input[placeholder="Plan name"]').fill('new_name');
    await page.locator('button:has-text("Save")').last().click();

    // Toast and updated name
    await expect(page.locator('text=Renamed to "new_name"')).toBeVisible();
    await page.click('[data-testid="dropdown-trigger"]');
    await expect(page.locator('text=new_name')).toBeVisible();
  });

  test('deletes a saved plan', async ({ page }) => {
    await page.click('text=New Plan');
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });

    // Save plan
    await page.click('button:has-text("Save As")');
    await page.locator('input[placeholder="Plan name"]').fill('to_delete');
    await page.locator('button:has-text("Save")').last().click();
    await page.waitForTimeout(500);

    // Click Delete
    await page.click('button:has-text("Delete")');

    // Confirm modal should appear
    await expect(page.locator('text=Are you sure you want to delete')).toBeVisible();

    // Confirm deletion (click the danger Delete button in modal)
    await page.locator('button:has-text("Delete")').last().click();

    // Toast should appear
    await expect(page.locator('text=Deleted "to_delete"')).toBeVisible();

    // Dropdown should show "New Plan" as selected
    await expect(page.locator('[data-testid="dropdown-trigger"]')).toContainText('New Plan');
  });

  test('resets to default plan', async ({ page }) => {
    await page.click('text=New Plan');
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });

    // Modify code in Monaco
    await page.locator('.monaco-editor').click();
    await page.keyboard.press('Meta+a');
    await page.keyboard.type('// Modified code only', { delay: 5 });

    // Verify modification
    await expect(page.locator('.monaco-editor .view-lines')).toContainText('Modified code');

    // Click Reset
    await page.click('button:has-text("Reset")');

    // Code should be back to default
    await expect(page.locator('.monaco-editor .view-lines')).toContainText('definePlan');
    await expect(page.locator('.monaco-editor .view-lines')).toContainText('my_plan');
  });

  test('shares plan via URL', async ({ page, context }) => {
    await page.click('text=New Plan');
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });

    // Grant clipboard permissions
    await context.grantPermissions(['clipboard-read', 'clipboard-write']);

    // Click Share
    await page.click('button:has-text("Share")');

    // Toast should appear
    await expect(page.locator('text=Link copied to clipboard')).toBeVisible();
  });

  test('loads plan from URL hash', async ({ page }) => {
    // Encode a simple plan into the URL
    const testCode = '// Loaded from URL hash test';
    const encoded = Buffer.from(testCode).toString('base64');

    await page.goto(`/#code=${encoded}`);
    await page.click('text=New Plan');

    // Editor should contain the decoded code
    await expect(page.locator('.monaco-editor .view-lines')).toContainText('Loaded from URL');
  });

  test('compiles with keyboard shortcut Cmd+Enter', async ({ page }) => {
    await page.click('text=New Plan');
    await expect(page.locator('button:has-text("Compile & Visualize")')).toBeEnabled({ timeout: 30000 });

    // Focus editor and press Cmd+Enter
    await page.locator('.monaco-editor').click();
    await page.keyboard.press('Meta+Enter');

    // Wait for compilation
    await expect(page.locator('text=Compiled successfully')).toBeVisible({ timeout: 30000 });
  });
});

test.describe('Plan Selector Integration', () => {
  test('New Plan button opens editor', async ({ page }) => {
    await page.goto('/');

    // Click New Plan
    await page.click('text=New Plan');

    // Editor should be visible
    await expect(page.locator('text=Compile & Visualize')).toBeVisible();
  });

  test('can return to plan selector from editor', async ({ page }) => {
    await page.goto('/');
    await page.click('text=New Plan');

    // Click the title to go back
    await page.click('h1:has-text("Plan Visualizer")');

    // Should see plan selector again (New Plan button visible but not the editor toolbar)
    await expect(page.locator('button:has-text("New Plan")')).toBeVisible();
  });
});
