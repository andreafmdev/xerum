import type { Preview } from "@storybook/react-vite";
import "../src/index.css";

const preview: Preview = {
  parameters: {
    backgrounds: {
      options: { dark: { name: "dark", value: "#0e1016" } },
    },
    layout: "centered",
  },
  initialGlobals: { backgrounds: { value: "dark" } },
};

export default preview;
