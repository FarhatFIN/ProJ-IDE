<!-- LOVABLE:BEGIN -->
> [!IMPORTANT]
> This project is connected to [Lovable](https://lovable.dev). Avoid rewriting
> published git history — force pushing, or rebasing/amending/squashing commits
> that are already pushed — as it rewrites history on Lovable's side and the
> user will likely lose their project history.
>
> Commits you push to the connected branch sync back to Lovable and show up in
> the editor, so keep the branch in a working state.
<!-- LOVABLE:END -->

- The IDE is fully client-side (Monaco loaded via lazy import behind ClientOnly); projects persist in localStorage — no backend needed.
- Code execution: Python via Pyodide in-browser, C/C++ via the public Wandbox compile API (src/lib/ide.ts) — no local toolchains on web.
