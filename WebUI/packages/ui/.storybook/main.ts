import type { StorybookConfig } from "@storybook/react-vite";

const config: StorybookConfig = {
  framework: "@storybook/react-vite",
  stories: ["../src/**/*.stories.tsx"],
  core: { disableTelemetry: true },
  async viteFinal(config) {
    // La build lib del package non serve a Storybook: rimuove lib mode e dts.
    // Il plugin vite-plugin-dts (via unplugin) si registra con name "unplugin-dts",
    // non "vite:dts" (quella è solo il prefisso di log "[unplugin:dts]").
    return {
      ...config,
      build: { ...config.build, lib: undefined, rollupOptions: {} },
      plugins: (config.plugins ?? []).filter(
        (p) => !(p && typeof p === "object" && "name" in p && String(p.name).startsWith("unplugin-dts")),
      ),
    };
  },
};

export default config;
