// Core IDE logic: projects stored in browser localStorage, language detection,
// compiler-log parsing (gcc / python traceback / nasm) and code execution.
export type IdeFile = { id: string; name: string; content: string };
export type Project = { id: string; name: string; files: IdeFile[]; last?: string | undefined };
export type Diag = { line: number; col: number; message: string; isError: boolean; fileId?: string };
export type Lang = "cpp" | "c" | "python" | "asm" | "markdown" | "text";

const KEY = "proj-ide/projects";
export const uid = () => Math.random().toString(36).slice(2, 10);

export function loadProjects(): Project[] {
  try {
    return JSON.parse(localStorage.getItem(KEY) || "[]");
  } catch {
    return [];
  }
}
export function saveProjects(p: Project[]) {
  localStorage.setItem(KEY, JSON.stringify(p));
}

export function detectLang(name: string): Lang {
  const ext = name.split(".").pop()?.toLowerCase() ?? "";
  if (["cpp", "cc", "cxx", "hpp", "hh", "h"].includes(ext)) return "cpp";
  if (ext === "c") return "c";
  if (ext === "py") return "python";
  if (["asm", "s", "nasm"].includes(ext)) return "asm";
  if (["md", "markdown"].includes(ext)) return "markdown";
  return "text";
}
export const monacoLang: Record<Lang, string> = {
  cpp: "cpp", c: "c", python: "python", asm: "mips", markdown: "markdown", text: "plaintext",
};
export const langLabel: Record<Lang, string> = {
  cpp: "C++", c: "C", python: "Python", asm: "NASM", markdown: "Markdown", text: "Plain Text",
};

/** Parses gcc/clang "file:line:col: error: msg", nasm "file:line: error: msg" and python tracebacks. */
export function parseLog(log: string): Diag[] {
  const out: Diag[] = [];
  const gcc = /^[^\s:][^:]*:(\d+):(?:(\d+):)?\s*(fatal error|error|warning):\s*(.*)$/;
  const lines = log.split("\n");
  for (const l of lines) {
    const m = l.match(gcc);
    if (m) out.push({ line: +(m[1] ?? 1), col: +(m[2] ?? 1), isError: m[3] !== "warning", message: m[4] ?? "" });
  }
  if (!out.length && (log.includes("Traceback") || /^\w*Error:/m.test(log))) {
    let line = 0;
    for (const l of lines) {
      const f = l.match(/File "([^"]+)", line (\d+)/);
      if (f && f[1] && !f[1].includes("pyodide") && !f[1].startsWith("/lib")) line = +(f[2] ?? 0);
    }
    const last = [...lines].reverse().find((l) => /^\w+(Error|Exception)\b/.test(l.trim()));
    if (last && !out.length) out.push({ line: line || 1, col: 1, isError: true, message: last.trim() });
  }
  return out;
}

// ---------- execution
let pyodide: Promise<any> | null = null;
function loadPy() {
  if (!pyodide) {
    pyodide = new Promise((res, rej) => {
      const s = document.createElement("script");
      s.src = "https://cdn.jsdelivr.net/pyodide/v0.26.4/full/pyodide.js";
      s.onload = () => (window as any).loadPyodide().then(res, rej);
      s.onerror = () => rej(new Error("Ошибка сети"));
      document.head.appendChild(s);
    });
  }
  return pyodide;
}

export type RunResult = { ok: boolean; output: string; diags: Diag[] };

export async function runCode(file: IdeFile, stdin: string): Promise<RunResult> {
  const lang = detectLang(file.name);
  if (lang === "python") {
    const py = await loadPy();
    let buf = "";
    py.setStdout({ batched: (s: string) => (buf += s + "\n") });
    py.setStderr({ batched: (s: string) => (buf += s + "\n") });
    const inLines = stdin.split("\n");
    py.setStdin({ stdin: () => inLines.shift() ?? null });
    try {
      await py.runPythonAsync(file.content, { filename: file.name });
      return { ok: true, output: buf, diags: [] };
    } catch (e: any) {
      const msg = String(e.message ?? e);
      const log = buf + msg.replace(/File "<exec>"/g, `File "${file.name}"`);
      return { ok: false, output: log, diags: parseLog(log) };
    }
  }
  if (lang === "cpp" || lang === "c") {
    const r = await fetch("https://wandbox.org/api/compile.json", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        code: file.content,
        compiler: lang === "cpp" ? "gcc-head" : "gcc-head-c",
        options: "warning",
        "compiler-option-raw": lang === "cpp" ? "-std=c++20\n-Wextra" : "-std=c17\n-Wextra",
        stdin,
      }),
    }).catch(() => null);
    if (!r || !r.ok) throw new Error("Ошибка сети");
    const j = await r.json();
    const clog = j.compiler_message ?? "";
    const diags = parseLog(clog);
    const ok = !diags.some((d) => d.isError) && String(j.status) === "0";
    return { ok, output: (clog ? clog + "\n" : "") + (j.program_message ?? ""), diags };
  }
  if (lang === "asm") throw new Error("NASM пока нельзя запустить в браузере");
  throw new Error(`Файл «${file.name}» нельзя запустить`);
}
