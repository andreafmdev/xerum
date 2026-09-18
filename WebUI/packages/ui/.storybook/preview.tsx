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
  decorators: [
    (Story) => (
      <div className="font-sans text-foreground">
        <Story />
      </div>
    ),
  ],
};

export default preview;
