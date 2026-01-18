# Review: step-13.1-ast-expr-extraction

## Findings

- Node compiler parity gap: `compiler-node` executes plans directly and never runs the new AST extraction, while `coalesce` now throws at runtime. Any plan using natural `vm({ expr: ... })` syntax fails when built with the Node backend (`plan-build`, `build_all_plans --backend node`).
  - **Response**: Valid. Node compiler is legacy/fallback only. Natural expressions require QuickJS compiler (dslc). Will document this limitation.

- Inline-only extraction: the extractor only rewrites inline object-literal `expr` values. Passing `expr` via variables/shorthand/spread (e.g., `const args = { expr: Key.id * coalesce(...) }; c.vm(args)` or `vm({ outKey, expr })`) skips extraction and hits the throwing `coalesce`, so natural syntax is fragile outside inline usage.
  - **Response**: Valid limitation. Natural expressions must be inline in the task call. Variables/shorthand patterns must use builder-style (`E.mul(...)`). Will document.

- Entry-file-only extraction: extraction runs on the entry plan source before bundling. Natural expressions inside imported helpers are not rewritten and will hit the throwing `coalesce`, so shared helper patterns break.
  - **Response**: Addressed. Import restriction now enforces plans can only import @ranking-dsl/runtime, @ranking-dsl/generated, and *.fragment.ts. Arbitrary shared helpers are banned.
