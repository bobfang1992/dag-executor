/**
 * Type declarations for virtual modules used in browser build.
 * These are replaced at build time with actual source code strings.
 */

declare module "virtual:runtime-source" {
  const source: string;
  export default source;
}

declare module "virtual:generated-source" {
  const source: string;
  export default source;
}
