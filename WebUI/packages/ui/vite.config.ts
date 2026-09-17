import { resolve } from "node:path";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import dts from "vite-plugin-dts";

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
  resolve: { alias: { "@": resolve(__dirname, "src") } },
  build: {
    outDir: "dist",
    emptyOutDir: true,
    sourcemap: true,
    lib: {
      entry: resolve(__dirname, "src/index.ts"),
      formats: ["es"],
      fileName: "index",
      cssFileName: "ui",
    },
    rollupOptions: {
      external: ["react", "react-dom", "react/jsx-runtime"],
    },
  },
});
