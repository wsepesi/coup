import { redirect } from "next/navigation";

// The lobby route hosts the whole room lifecycle; keep old /game links working.
export default async function GamePage({ params }: { params: Promise<{ code: string }> }) {
  const { code } = await params;
  redirect(`/lobby/${encodeURIComponent(code)}`);
}
