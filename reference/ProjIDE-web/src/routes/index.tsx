import { createFileRoute, ClientOnly } from "@tanstack/react-router";
import { lazy, Suspense } from "react";

const Ide = lazy(() => import("@/components/ide/Ide"));

export const Route = createFileRoute("/")({
  head: () => ({
    meta: [
      { title: "ProJ IDE — редактор кода в браузере" },
      { name: "description", content: "Компактный IDE в стиле VS Code: проекты, подсветка, запуск C, C++ и Python." },
      { property: "og:title", content: "ProJ IDE — редактор кода в браузере" },
      { property: "og:description", content: "Проекты, вкладки, подсветка, запуск C, C++ и Python прямо в браузере." },
      { property: "og:type", content: "website" },
      { name: "twitter:card", content: "summary" },
    ],
  }),
  component: Index,
});

function Index() {
  const fallback = <div className="h-screen bg-background" />;
  return (
    <ClientOnly fallback={fallback}>
      <Suspense fallback={fallback}>
        <Ide />
      </Suspense>
    </ClientOnly>
  );
}
