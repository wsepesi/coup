import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "COUP",
  description: "Coup card game - online multiplayer",
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="en">
      <body className="font-mono antialiased">
        {children}
      </body>
    </html>
  );
}
