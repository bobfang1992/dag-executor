/**
 * Predicate compiler: converts TypeScript AST predicates to PredIR.
 *
 * Supported predicates (fail-closed - anything else is rejected):
 * - true, false          → { op: "const_bool", value }
 * - a && b               → { op: "and", a, b }
 * - a || b               → { op: "or", a, b }
 * - !a                   → { op: "not", x }
 * - a == b, a === b      → { op: "cmp", cmp: "==", a, b }
 * - a != b, a !== b      → { op: "cmp", cmp: "!=", a, b }
 * - a < b                → { op: "cmp", cmp: "<", a, b }
 * - a <= b               → { op: "cmp", cmp: "<=", a, b }
 * - a > b                → { op: "cmp", cmp: ">", a, b }
 * - a >= b               → { op: "cmp", cmp: ">=", a, b }
 * - Key.x == null        → { op: "is_null", x }
 * - Key.x != null        → { op: "not_null", x }
 * - regex(Key.x, "pat")  → { op: "regex", key_id, pattern, flags }
 *
 * Comparison terms (for a and b in comparisons):
 * - Key.foo              → { op: "key_ref", key_id }
 * - P.bar                → { op: "param_ref", param_id }
 * - 123, 0.5             → { op: "const_number", value }
 * - null                 → { op: "const_null" }
 *
 * NOT supported (fail-closed):
 * - Arithmetic in predicates (Key.id * 2 > 10)
 * - Ternary (?:)
 * - Arbitrary function calls
 * - undefined comparisons
 */

import ts from "typescript";
import type { PredNode, ExprNode, RegexPattern } from "@ranking-dsl/runtime";

export interface CompilePredSuccess {
  pred: PredNode;
}

export interface CompilePredError {
  error: string;
  node: ts.Node;
}

export type CompilePredResult = CompilePredSuccess | CompilePredError;

interface CompileTermSuccess {
  expr: ExprNode;
}

type CompileTermResult = CompileTermSuccess | CompilePredError;

/**
 * Compile a TypeScript AST expression to a comparison term (ExprNode).
 * Only supports: Key.x, P.y, number literal, null
 */
function compileTerm(
  node: ts.Expression,
  keyLookup: Map<string, number>,
  paramLookup: Map<string, number>,
  sourceFile: ts.SourceFile
): CompileTermResult {
  // Numeric literal
  if (ts.isNumericLiteral(node)) {
    const sanitized = node.text.replace(/_/g, "");
    const value = Number(sanitized);
    if (!Number.isFinite(value)) {
      return { error: `Invalid numeric literal: ${node.text}`, node };
    }
    return { expr: { op: "const_number", value } };
  }

  // String literal (for string comparisons)
  if (ts.isStringLiteral(node)) {
    // String comparisons use the string value directly
    // but PredIR cmp expects ExprNode, so we need to handle this specially
    // For now, reject - string comparisons should use Pred.in() or regex()
    return {
      error: "String literals in comparisons are not supported. Use regex() or an 'in' list instead.",
      node,
    };
  }

  // null literal
  if (node.kind === ts.SyntaxKind.NullKeyword) {
    return { expr: { op: "const_null" } };
  }

  // Property access: Key.foo, P.bar
  if (ts.isPropertyAccessExpression(node)) {
    const objName = node.expression.getText(sourceFile);
    const propName = node.name.text;

    if (objName === "Key") {
      const keyId = keyLookup.get(propName);
      if (keyId === undefined) {
        return { error: `Unknown key: Key.${propName}`, node };
      }
      return { expr: { op: "key_ref", key_id: keyId } };
    }

    if (objName === "P") {
      const paramId = paramLookup.get(propName);
      if (paramId === undefined) {
        return { error: `Unknown param: P.${propName}`, node };
      }
      return { expr: { op: "param_ref", param_id: paramId } };
    }

    return {
      error: `Unsupported property access in predicate: ${objName}.${propName}. Only Key.* and P.* are allowed.`,
      node,
    };
  }

  // Parenthesized expression
  if (ts.isParenthesizedExpression(node)) {
    return compileTerm(node.expression, keyLookup, paramLookup, sourceFile);
  }

  return {
    error: `Unsupported term in predicate comparison: ${ts.SyntaxKind[node.kind]}. Only Key.*, P.*, numbers, and null are allowed.`,
    node,
  };
}

/**
 * Check if a node is the null keyword
 */
function isNullLiteral(node: ts.Expression): boolean {
  return node.kind === ts.SyntaxKind.NullKeyword;
}

/**
 * Check if a node is undefined (reject)
 */
function isUndefinedLiteral(node: ts.Expression, sourceFile: ts.SourceFile): boolean {
  if (ts.isIdentifier(node) && node.getText(sourceFile) === "undefined") {
    return true;
  }
  return false;
}

/**
 * Compile a TypeScript AST expression to PredIR.
 */
export function compilePred(
  node: ts.Expression,
  keyLookup: Map<string, number>,
  paramLookup: Map<string, number>,
  sourceFile: ts.SourceFile
): CompilePredResult {
  // Boolean literals: true, false
  if (node.kind === ts.SyntaxKind.TrueKeyword) {
    return { pred: { op: "const_bool", value: true } };
  }
  if (node.kind === ts.SyntaxKind.FalseKeyword) {
    return { pred: { op: "const_bool", value: false } };
  }

  // Parenthesized expression: (a && b)
  if (ts.isParenthesizedExpression(node)) {
    return compilePred(node.expression, keyLookup, paramLookup, sourceFile);
  }

  // Prefix unary: !a
  if (ts.isPrefixUnaryExpression(node)) {
    if (node.operator === ts.SyntaxKind.ExclamationToken) {
      const operand = compilePred(node.operand, keyLookup, paramLookup, sourceFile);
      if ("error" in operand) return operand;
      return { pred: { op: "not", x: operand.pred } };
    }
    return {
      error: `Unsupported unary operator in predicate: ${ts.SyntaxKind[node.operator]}`,
      node,
    };
  }

  // Binary expressions: &&, ||, comparisons
  if (ts.isBinaryExpression(node)) {
    const opKind = node.operatorToken.kind;

    // Logical operators: &&, ||
    if (opKind === ts.SyntaxKind.AmpersandAmpersandToken) {
      const left = compilePred(node.left, keyLookup, paramLookup, sourceFile);
      if ("error" in left) return left;
      const right = compilePred(node.right, keyLookup, paramLookup, sourceFile);
      if ("error" in right) return right;
      return { pred: { op: "and", a: left.pred, b: right.pred } };
    }

    if (opKind === ts.SyntaxKind.BarBarToken) {
      const left = compilePred(node.left, keyLookup, paramLookup, sourceFile);
      if ("error" in left) return left;
      const right = compilePred(node.right, keyLookup, paramLookup, sourceFile);
      if ("error" in right) return right;
      return { pred: { op: "or", a: left.pred, b: right.pred } };
    }

    // Comparison operators: ==, ===, !=, !==, <, <=, >, >=
    const cmpOps: Record<number, "==" | "!=" | "<" | "<=" | ">" | ">="> = {
      [ts.SyntaxKind.EqualsEqualsToken]: "==",
      [ts.SyntaxKind.EqualsEqualsEqualsToken]: "==",
      [ts.SyntaxKind.ExclamationEqualsToken]: "!=",
      [ts.SyntaxKind.ExclamationEqualsEqualsToken]: "!=",
      [ts.SyntaxKind.LessThanToken]: "<",
      [ts.SyntaxKind.LessThanEqualsToken]: "<=",
      [ts.SyntaxKind.GreaterThanToken]: ">",
      [ts.SyntaxKind.GreaterThanEqualsToken]: ">=",
    };

    const cmpOp = cmpOps[opKind];
    if (cmpOp) {
      // Check for undefined comparisons (reject)
      if (isUndefinedLiteral(node.left, sourceFile) || isUndefinedLiteral(node.right, sourceFile)) {
        return {
          error: "Comparisons with 'undefined' are not allowed. Use null instead.",
          node,
        };
      }

      // Special case: null checks → is_null / not_null
      const leftIsNull = isNullLiteral(node.left);
      const rightIsNull = isNullLiteral(node.right);

      if (leftIsNull || rightIsNull) {
        // One side is null, the other should be a term
        const termNode = leftIsNull ? node.right : node.left;
        const termResult = compileTerm(termNode, keyLookup, paramLookup, sourceFile);
        if ("error" in termResult) return termResult;

        // Note: cmpOps already normalizes === to == and !== to !=
        if (cmpOp === "==") {
          return { pred: { op: "is_null", x: termResult.expr } };
        } else if (cmpOp === "!=") {
          return { pred: { op: "not_null", x: termResult.expr } };
        } else {
          return {
            error: `Null comparisons only support == and !=, not ${cmpOp}`,
            node,
          };
        }
      }

      // Regular comparison
      const left = compileTerm(node.left, keyLookup, paramLookup, sourceFile);
      if ("error" in left) return left;
      const right = compileTerm(node.right, keyLookup, paramLookup, sourceFile);
      if ("error" in right) return right;

      return {
        pred: { op: "cmp", cmp: cmpOp, a: left.expr, b: right.expr },
      };
    }

    return {
      error: `Unsupported binary operator in predicate: ${ts.SyntaxKind[opKind]}`,
      node,
    };
  }

  // Call expression: regex(Key.x, "pattern")
  if (ts.isCallExpression(node)) {
    const fnName = node.expression.getText(sourceFile);

    if (fnName === "regex") {
      return compileRegexCall(node, keyLookup, paramLookup, sourceFile);
    }

    return {
      error: `Unsupported function call in predicate: ${fnName}(). Only regex() is allowed.`,
      node,
    };
  }

  return {
    error: `Unsupported predicate expression: ${ts.SyntaxKind[node.kind]}`,
    node,
  };
}

/**
 * Compile a regex() call to PredIR regex node.
 * Syntax: regex(Key.strCol, "pattern") or regex(Key.strCol, P.paramName)
 * Optional third arg for flags: regex(Key.strCol, "pattern", "i")
 */
function compileRegexCall(
  node: ts.CallExpression,
  keyLookup: Map<string, number>,
  paramLookup: Map<string, number>,
  sourceFile: ts.SourceFile
): CompilePredResult {
  if (node.arguments.length < 2 || node.arguments.length > 3) {
    return {
      error: `regex() requires 2 or 3 arguments, got ${node.arguments.length}`,
      node,
    };
  }

  // First arg: Key.strCol
  const keyArg = node.arguments[0];
  if (!ts.isPropertyAccessExpression(keyArg)) {
    return {
      error: "regex() first argument must be Key.<name>",
      node: keyArg,
    };
  }
  const keyObjName = keyArg.expression.getText(sourceFile);
  if (keyObjName !== "Key") {
    return {
      error: `regex() first argument must be Key.<name>, got ${keyObjName}.<name>`,
      node: keyArg,
    };
  }
  const keyName = keyArg.name.text;
  const keyId = keyLookup.get(keyName);
  if (keyId === undefined) {
    return {
      error: `Unknown key: Key.${keyName}`,
      node: keyArg,
    };
  }

  // Second arg: "pattern" or P.paramName
  const patternArg = node.arguments[1];
  let pattern: RegexPattern;

  if (ts.isStringLiteral(patternArg)) {
    pattern = { kind: "literal", value: patternArg.text };
  } else if (ts.isPropertyAccessExpression(patternArg)) {
    const patternObjName = patternArg.expression.getText(sourceFile);
    if (patternObjName !== "P") {
      return {
        error: `regex() pattern must be a string literal or P.<name>, got ${patternObjName}.<name>`,
        node: patternArg,
      };
    }
    const paramName = patternArg.name.text;
    const paramId = paramLookup.get(paramName);
    if (paramId === undefined) {
      return {
        error: `Unknown param: P.${paramName}`,
        node: patternArg,
      };
    }
    pattern = { kind: "param", param_id: paramId };
  } else {
    return {
      error: "regex() pattern must be a string literal or P.<name>",
      node: patternArg,
    };
  }

  // Third arg (optional): flags "" or "i"
  let flags = "";
  if (node.arguments.length === 3) {
    const flagsArg = node.arguments[2];
    if (!ts.isStringLiteral(flagsArg)) {
      return {
        error: 'regex() flags must be a string literal ("" or "i")',
        node: flagsArg,
      };
    }
    flags = flagsArg.text;
    if (flags !== "" && flags !== "i") {
      return {
        error: `regex() flags must be "" or "i", got "${flags}"`,
        node: flagsArg,
      };
    }
  }

  return {
    pred: {
      op: "regex",
      key_id: keyId,
      pattern,
      flags,
    },
  };
}

// Re-export getNodeLocation for use in ast-extractor
export { getNodeLocation } from "./expr-compiler.js";
