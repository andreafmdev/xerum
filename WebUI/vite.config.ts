import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";

/** Gli asset del marchio vivono in Resources/branding, fuori dalla root di Vite: una sola copia
    per plugin, icone e Web UI. L'alias li importa da `@branding/...`; `fs.allow` permette al dev
    server di servirli, perche' di default Vite serve solo cio' che sta sotto il workspace. */
const branding = fileURLToPath(new URL("../Resources/branding", import.meta.url));

export default defineConfig({
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: { "@branding": branding },
  },
  server: {
    port: 5173,
    strictPort: true,
    fs: { allow: [fileURLToPath(new URL(".", import.meta.url)), branding] },
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
  },
});
