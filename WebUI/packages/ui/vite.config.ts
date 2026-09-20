import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import dts from "vite-plugin-dts";

const pkg = JSON.parse(readFileSync(resolve(import.meta.dirname, "package.json"), "utf8")) as {
  dependencies: Record<string, string>;
  peerDependencies: Record<string, string>;
};
const externals = [...Object.keys(pkg.peerDependencies), ...Object.keys(pkg.dependencies)];

export default defineConfig({
  plugins: [
    react(),
    tailwindcss(),
    dts({
      tsconfigPath: "./tsconfig.json",
      bundleTypes: false,
      entryRoot: "src",
      exclude: ["**/*.test.*", "**/*.stories.*", "src/test/**", ".storybook/**", "vite.config.ts", "vitest.config.ts"],
    }),
  ],
  resolve: { alias: { "@": resolve(import.meta.dirname, "src") } },
  build: {
    outDir: "dist",
    emptyOutDir: true,
    sourcemap: true,
    lib: {
      entry: resolve(import.meta.dirname, "src/index.ts"),
      formats: ["es"],
      fileName: "index",
      cssFileName: "ui",
    },
    rollupOptions: {
      // Ogni dipendenza resta esterna: la risolve chi consuma la libreria.
      // Bundlandole ci finirebbe dentro anche il CJS (use-sync-external-store,
      // via @base-ui/react), che rolldown traduce in un require() inesistente
      // nel browser.
      external: (id: string) => externals.some((dep) => id === dep || id.startsWith(`${dep}/`)),
    },
  },
});
