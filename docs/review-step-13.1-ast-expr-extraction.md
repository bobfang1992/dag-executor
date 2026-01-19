# Review: step-13.1-ast-expr-extraction

## Findings

- Inline-only extraction: the extractor only rewrites inline object-literal `expr` values. Passing `expr` via variables/shorthand/spread (e.g., `const args = { expr: Key.id * coalesce(...) }; c.vm(args)` or `vm({ outKey, expr })`) skips extraction and hits the throwing `coalesce`, so natural syntax is fragile outside inline usage.
  - **Response**: Valid limitation. Natural expressions must be inline in the task call. Variables/shorthand patterns must use builder-style (`E.mul(...)`). Future enhancement: add variable resolution (AST-level constant folding) to support composing expressions via variables.

- Entry-file-only extraction: extraction runs on the entry plan source before bundling. Natural expressions inside imported helpers are not rewritten and will hit the throwing `coalesce`, so shared helper patterns break.
  - **Response**: Partially mitigated by import restrictions (plans cannot use arbitrary helpers), but fragments or any future allowed imports would still need extraction on bundled dependencies or explicit builder-style requirements.

- Identifier rigidity: extraction only recognizes the exact identifiers `Key`, `P`, and `coalesce`. Alias/namespace/re-exported identifiers skip extraction and will either throw (from `coalesce`) or emit incorrect IR.
  - **Response**: Valid. Document that aliasing is not supported. Plans must use standard identifiers: `Key`, `P`, `coalesce`. No `import { Key as K }` patterns. Since Step 13.2, `Key`, `P`, `coalesce` are globals (injected by compiler) and should not be imported.
