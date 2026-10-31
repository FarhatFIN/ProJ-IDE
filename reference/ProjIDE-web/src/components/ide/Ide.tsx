import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import Editor, { type OnMount } from "@monaco-editor/react";
import {
  Files, Play, Settings, Plus, FilePlus, FolderPlus, ChevronRight, ChevronDown, X,
  CircleX, TriangleAlert, Trash2, FileCode, Search, Upload, Loader2, Moon, Sun, Monitor,
} from "lucide-react";
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogFooter } from "@/components/ui/dialog";
import { CommandDialog, CommandInput, CommandList, CommandEmpty, CommandItem, CommandGroup } from "@/components/ui/command";
import { Input } from "@/components/ui/input";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import {
  type Project, type IdeFile, type Diag, uid, loadProjects, saveProjects,
  detectLang, monacoLang, langLabel, runCode,
} from "@/lib/ide";

type Theme = "dark" | "light" | "system";
type Tab = { projectId: string; fileId: string };
type PanelTab = "problems" | "output" | "input";

const SAMPLE: Project[] = [{
  id: "demo", name: "hello", last: "m",
  files: [
    { id: "m", name: "main.cpp", content: '#include <iostream>\n\nint main() {\n    std::cout << "Привет, ProJ IDE!" << std::endl;\n    return 0;\n}\n' },
    { id: "p", name: "script.py", content: 'name = "мир"\nfor i in range(3):\n    print(f"{i}: привет, {name}")\n' },
  ],
}];

export default function Ide() {
  const [projects, setProjects] = useState<Project[]>([]);
  const [open, setOpen] = useState<Record<string, boolean>>({});
  const [tabs, setTabs] = useState<Tab[]>([]);
  const [active, setActive] = useState<Tab | null>(null);
  const [buffers, setBuffers] = useState<Record<string, string>>({});
  const [diags, setDiags] = useState<Diag[]>([]);
  const [output, setOutput] = useState("");
  const [stdin, setStdin] = useState("");
  const [panel, setPanel] = useState<PanelTab>("output");
  const [running, setRunning] = useState(false);
  const [view, setView] = useState<"explorer" | "run" | "settings">("explorer");
  const [theme, setTheme] = useState<Theme>("dark");
  const [sysDark, setSysDark] = useState(true);
  const [cursor, setCursor] = useState({ line: 1, col: 1 });
  const [ask, setAsk] = useState<null | { title: string; value: string; onOk: (v: string) => void }>(null);
  const [errDialog, setErrDialog] = useState<Diag | null>(null);
  const [palette, setPalette] = useState(false);
  const editorRef = useRef<Parameters<OnMount>[0] | null>(null);
  const monacoRef = useRef<Parameters<OnMount>[1] | null>(null);
  const uploadRef = useRef<HTMLInputElement>(null);
  const uploadTarget = useRef<string | null>(null);

  // load
  useEffect(() => {
    const p = loadProjects();
    setProjects(p.length ? p : SAMPLE);
    setTheme((localStorage.getItem("proj-ide/theme") as Theme) || "dark");
    const mq = matchMedia("(prefers-color-scheme: dark)");
    setSysDark(mq.matches);
    const h = (e: MediaQueryListEvent) => setSysDark(e.matches);
    mq.addEventListener("change", h);
    return () => mq.removeEventListener("change", h);
  }, []);
  const persist = useCallback((p: Project[]) => { setProjects(p); saveProjects(p); }, []);

  const dark = theme === "system" ? sysDark : theme === "dark";
  useEffect(() => {
    document.documentElement.classList.toggle("dark", dark);
    localStorage.setItem("proj-ide/theme", theme);
  }, [dark, theme]);

  const key = (t: Tab) => `${t.projectId}/${t.fileId}`;
  const findFile = (t: Tab | null) =>
    t ? projects.find((p) => p.id === t.projectId)?.files.find((f) => f.id === t.fileId) : undefined;
  const activeFile = findFile(active);
  const activeContent = active ? buffers[key(active)] ?? activeFile?.content ?? "" : "";

  const openFile = (projectId: string, fileId: string) => {
    const t = { projectId, fileId };
    setTabs((ts) => (ts.some((x) => key(x) === key(t)) ? ts : [...ts, t]));
    setActive(t);
    setDiags([]);
    persist(projects.map((p) => (p.id === projectId ? { ...p, last: fileId } : p)));
  };
  const toggleProject = (p: Project) => {
    const willOpen = !open[p.id];
    setOpen((o) => ({ ...o, [p.id]: willOpen }));
    if (willOpen) {
      const f = p.files.find((x) => x.id === p.last) ?? p.files[0];
      if (f) openFile(p.id, f.id);
    }
  };
  const closeTab = (t: Tab) => {
    const rest = tabs.filter((x) => key(x) !== key(t));
    setTabs(rest);
    setBuffers(({ [key(t)]: _, ...b }) => b);
    if (active && key(active) === key(t)) setActive(rest[rest.length - 1] ?? null);
  };

  const save = useCallback(() => {
    if (!active) return;
    const k = key(active);
    if (buffers[k] === undefined) return;
    persist(projects.map((p) => p.id !== active.projectId ? p : {
      ...p, files: p.files.map((f) => (f.id === active.fileId ? { ...f, content: buffers[k] ?? f.content } : f)),
    }));
    setBuffers(({ [k]: _, ...b }) => b);
  }, [active, buffers, projects, persist]);

  const newProject = (files: IdeFile[] = []) =>
    setAsk({
      title: "Название вашего проекта:", value: "",
      onOk: (name) => {
        const p: Project = { id: uid(), name, files, last: files[0]?.id };
        persist([...projects, p]);
        setOpen((o) => ({ ...o, [p.id]: true }));
        if (files[0]) { const t = { projectId: p.id, fileId: files[0].id }; setTabs((ts) => [...ts, t]); setActive(t); }
      },
    });
  const newFile = (projectId: string) =>
    setAsk({
      title: "Имя файла:", value: "main.cpp",
      onOk: (name) => {
        const f = { id: uid(), name, content: "" };
        persist(projects.map((p) => (p.id === projectId ? { ...p, files: [...p.files, f], last: f.id } : p)));
        setOpen((o) => ({ ...o, [projectId]: true }));
        const t = { projectId, fileId: f.id };
        setTabs((ts) => [...ts, t]); setActive(t);
      },
    });
  const onUpload = async (list: FileList | null) => {
    if (!list?.length) return;
    const files = await Promise.all([...list].map(async (f) => ({ id: uid(), name: f.name, content: await f.text() })));
    const target = uploadTarget.current;
    if (target) persist(projects.map((p) => (p.id === target ? { ...p, files: [...p.files, ...files] } : p)));
    else newProject(files);
    if (uploadRef.current) uploadRef.current.value = "";
  };
  const deleteProject = (id: string) => {
    if (!confirm("Удалить проект?")) return;
    persist(projects.filter((p) => p.id !== id));
    setTabs((ts) => ts.filter((t) => t.projectId !== id));
    if (active?.projectId === id) setActive(null);
  };
  const deleteFile = (pid: string, fid: string) => {
    persist(projects.map((p) => (p.id === pid ? { ...p, files: p.files.filter((f) => f.id !== fid) } : p)));
    closeTab({ projectId: pid, fileId: fid });
  };

  const start = useCallback(async () => {
    if (!activeFile || !active || running) return;
    save();
    setRunning(true); setPanel("output"); setOutput(`▶ ${activeFile.name}\n`);
    try {
      const r = await runCode({ ...activeFile, content: activeContent }, stdin);
      const ds = r.diags.map((d) => ({ ...d, fileId: active.fileId }));
      setDiags(ds);
      setOutput(`▶ ${activeFile.name}\n${r.output}\n${r.ok ? "✔ Готово" : "✖ Завершено с ошибками"}`);
      const firstErr = ds.find((d) => d.isError);
      if (firstErr) { setErrDialog(firstErr); setPanel("problems"); }
    } catch (e: any) {
      setOutput((o) => o + "\n" + (e.message ?? e));
    } finally { setRunning(false); }
  }, [activeFile, active, activeContent, stdin, running, save]);

  // markers
  useEffect(() => {
    const m = monacoRef.current, model = editorRef.current?.getModel();
    if (!m || !model) return;
    m.editor.setModelMarkers(model, "proj", diags.map((d) => ({
      startLineNumber: d.line, endLineNumber: d.line, startColumn: d.col, endColumn: model.getLineMaxColumn(Math.min(d.line, model.getLineCount())),
      message: d.message, severity: d.isError ? m.MarkerSeverity.Error : m.MarkerSeverity.Warning,
    })));
  }, [diags, active]);

  // shortcuts
  useEffect(() => {
    const h = (e: KeyboardEvent) => {
      if (!(e.ctrlKey || e.metaKey)) return;
      const k = e.key.toLowerCase();
      if (k === "s") { e.preventDefault(); save(); }
      else if (k === "b" || k === "r" || e.key === "Enter") { e.preventDefault(); start(); }
      else if (k === "p") { e.preventDefault(); setPalette(true); }
    };
    addEventListener("keydown", h);
    return () => removeEventListener("keydown", h);
  }, [save, start]);

  const goto = (d: Diag) => {
    const ed = editorRef.current;
    if (!ed) return;
    ed.revealLineInCenter(d.line); ed.setPosition({ lineNumber: d.line, column: d.col }); ed.focus();
  };

  const errors = diags.filter((d) => d.isError).length;
  const warnings = diags.length - errors;
  const lang = activeFile ? detectLang(activeFile.name) : "text";
  const activeProject = projects.find((p) => p.id === active?.projectId);
  const allFiles = useMemo(() => projects.flatMap((p) => p.files.map((f) => ({ p, f }))), [projects]);

  return (
    <div className="flex h-screen flex-col bg-background text-[13px] text-foreground">
      <input ref={uploadRef} type="file" multiple hidden onChange={(e) => onUpload(e.target.files)} />
      {/* title bar */}
      <div className="flex h-9 items-center border-b border-border bg-activity px-3">
        <span className="font-mono text-xs font-semibold text-primary">ProJ</span>
        <span className="ml-1 text-xs text-muted-foreground">IDE</span>
        <button onClick={() => setPalette(true)} className="mx-auto flex w-full max-w-md items-center gap-2 rounded border border-border bg-muted px-3 py-1 text-xs text-muted-foreground hover:bg-secondary">
          <Search className="h-3.5 w-3.5" />
          {activeProject ? activeProject.name : "Поиск файлов"}
          <kbd className="ml-auto font-mono text-[10px]">Ctrl+P</kbd>
        </button>
        <Button size="sm" variant="ghost" className="h-7 gap-1 text-success" onClick={start} disabled={!activeFile || running} title="Старт (Ctrl+B)">
          {running ? <Loader2 className="h-4 w-4 animate-spin" /> : <Play className="h-4 w-4 fill-current" />} Старт
        </Button>
      </div>

      <div className="flex min-h-0 flex-1">
        {/* activity bar */}
        <div className="flex w-12 flex-col items-center gap-1 border-r border-border bg-activity py-2">
          {([["explorer", Files, "Проводник"], ["run", Play, "Запуск"], ["settings", Settings, "Настройки"]] as const).map(([v, Icon, t]) => (
            <button key={v} title={t} onClick={() => setView(v)}
              className={cn("relative flex h-11 w-12 items-center justify-center text-muted-foreground hover:text-foreground", view === v && "text-foreground before:absolute before:left-0 before:h-6 before:w-0.5 before:bg-primary")}>
              <Icon className="h-5 w-5" />
            </button>
          ))}
        </div>

        <div className="flex min-w-0 flex-1">
          <div className="w-64 shrink-0 border-r border-border bg-sidebar">
            {view === "explorer" && (
              <div className="flex h-full flex-col">
                <div className="flex h-9 items-center px-4 text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
                  Проекты
                  <div className="ml-auto flex gap-1">
                    <IconBtn title="Новый проект" onClick={() => newProject()}><FolderPlus className="h-4 w-4" /></IconBtn>
                    <IconBtn title="+ Добавить файлы" onClick={() => { uploadTarget.current = null; uploadRef.current?.click(); }}><Upload className="h-4 w-4" /></IconBtn>
                  </div>
                </div>
                <div className="flex-1 overflow-auto pb-4">
                  {projects.map((p, i) => (
                    <div key={p.id}>
                      <div className="group flex h-6 cursor-pointer items-center gap-1 px-2 hover:bg-muted" onClick={() => toggleProject(p)}>
                        {open[p.id] ? <ChevronDown className="h-4 w-4" /> : <ChevronRight className="h-4 w-4" />}
                        <span className="font-mono text-muted-foreground">{i + 1}.</span>
                        <span className="truncate font-semibold">{p.name}</span>
                        <div className="ml-auto hidden gap-0.5 group-hover:flex" onClick={(e) => e.stopPropagation()}>
                          <IconBtn title="Новый файл" onClick={() => newFile(p.id)}><FilePlus className="h-3.5 w-3.5" /></IconBtn>
                          <IconBtn title="Добавить файлы" onClick={() => { uploadTarget.current = p.id; uploadRef.current?.click(); }}><Plus className="h-3.5 w-3.5" /></IconBtn>
                          <IconBtn title="Удалить проект" onClick={() => deleteProject(p.id)}><Trash2 className="h-3.5 w-3.5" /></IconBtn>
                        </div>
                      </div>
                      {open[p.id] && p.files.map((f) => {
                        const isActive = active?.projectId === p.id && active.fileId === f.id;
                        return (
                          <div key={f.id} onClick={() => openFile(p.id, f.id)}
                            className={cn("group flex h-6 cursor-pointer items-center gap-1.5 pl-9 pr-2 hover:bg-muted", isActive && "bg-accent text-accent-foreground")}>
                            <FileCode className={cn("h-3.5 w-3.5", langColor(f.name))} />
                            <span className="truncate">{f.name}</span>
                            {p.last === f.id && <span className="h-1.5 w-1.5 rounded-full bg-primary" title="Последний открытый" />}
                            <button className="ml-auto hidden text-muted-foreground hover:text-destructive group-hover:block" onClick={(e) => { e.stopPropagation(); deleteFile(p.id, f.id); }}>
                              <X className="h-3.5 w-3.5" />
                            </button>
                          </div>
                        );
                      })}
                    </div>
                  ))}
                  {!projects.length && <p className="px-4 py-6 text-muted-foreground">Нет проектов. Нажмите «+ Добавить файлы».</p>}
                </div>
                {/* Errors badge */}
                <div className="border-t border-border">
                  <button onClick={() => setPanel("problems")} className={cn("m-2 inline-flex items-center gap-1 rounded px-2 py-0.5 font-mono text-xs", errors ? "bg-destructive text-destructive-foreground" : "bg-muted text-muted-foreground")}>
                    Errors&lt;{errors}&gt;
                  </button>
                  <div className="max-h-40 overflow-auto">
                    {diags.map((d, i) => (
                      <button key={i} onClick={() => goto(d)} className="flex w-full gap-1.5 px-3 py-0.5 text-left text-xs hover:bg-muted">
                        {d.isError ? <CircleX className="h-3.5 w-3.5 shrink-0 text-destructive" /> : <TriangleAlert className="h-3.5 w-3.5 shrink-0 text-warning" />}
                        <span className="truncate">{d.line}: {d.message}</span>
                      </button>
                    ))}
                  </div>
                </div>
              </div>
            )}
            {view === "run" && (
              <div className="space-y-3 p-4">
                <div className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">Запуск</div>
                <Button className="w-full" onClick={start} disabled={!activeFile || running}><Play className="mr-1 h-4 w-4" /> Старт ({activeFile?.name ?? "нет файла"})</Button>
                <ul className="space-y-1 text-xs text-muted-foreground">
                  <li>C++ — g++ -std=c++20 -Wall -Wextra</li>
                  <li>C — gcc -std=c17 -Wall -Wextra</li>
                  <li>Python — запуск прямо в браузере</li>
                  <li>NASM — только подсветка</li>
                </ul>
                <p className="text-xs text-muted-foreground">Ctrl+B — старт · Ctrl+S — сохранить · Ctrl+P — файлы</p>
              </div>
            )}
            {view === "settings" && (
              <div className="space-y-2 p-4">
                <div className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">Тема</div>
                {([["dark", Moon, "Тёмная"], ["light", Sun, "Светлая"], ["system", Monitor, "Системная"]] as const).map(([t, Icon, l]) => (
                  <button key={t} onClick={() => setTheme(t)} className={cn("flex w-full items-center gap-2 rounded px-2 py-1.5 hover:bg-muted", theme === t && "bg-accent text-accent-foreground")}>
                    <Icon className="h-4 w-4" /> {l}
                  </button>
                ))}
              </div>
            )}
          </div>
                    <div className="flex min-w-0 flex-1">
            <div className="flex min-w-0 flex-1 flex-col">
              <div className="flex min-h-0 flex-1 flex-col">
                {/* tabs */}
                <div className="flex h-9 shrink-0 overflow-x-auto bg-tabbar">
                  {tabs.map((t) => {
                    const f = findFile(t); if (!f) return null;
                    const isA = active && key(active) === key(t);
                    const dirty = buffers[key(t)] !== undefined;
                    return (
                      <div key={key(t)} onClick={() => setActive(t)} onContextMenu={(e) => { e.preventDefault(); closeTab(t); }}
                        className={cn("group flex cursor-pointer items-center gap-1.5 border-r border-border px-3", isA ? "border-t-2 border-t-primary bg-background" : "text-muted-foreground")}>
                        <FileCode className={cn("h-3.5 w-3.5", langColor(f.name))} />
                        {f.name}
                        <button onClick={(e) => { e.stopPropagation(); closeTab(t); }} className="flex h-4 w-4 items-center justify-center rounded hover:bg-muted">
                          {dirty ? <span className="h-2 w-2 rounded-full bg-foreground group-hover:hidden" /> : null}
                          <X className={cn("h-3.5 w-3.5", dirty && "hidden group-hover:block", !isA && !dirty && "invisible group-hover:visible")} />
                        </button>
                      </div>
                    );
                  })}
                </div>
                {activeFile && activeProject && (
                  <div className="flex h-6 shrink-0 items-center gap-1 px-4 text-xs text-muted-foreground">
                    {activeProject.name} <ChevronRight className="h-3 w-3" /> {activeFile.name}
                  </div>
                )}
                <div className="min-h-0 flex-1">
                  {activeFile ? (
                    <Editor
                      path={(active ? key(active) : "") + "/" + activeFile.name}
                      language={monacoLang[lang]}
                      value={activeContent}
                      theme={dark ? "vs-dark" : "vs"}
                      onChange={(v) => active && setBuffers((b) => ({ ...b, [key(active)]: v ?? "" }))}
                      onMount={(ed, m) => {
                        editorRef.current = ed; monacoRef.current = m;
                        ed.onDidChangeCursorPosition((e) => setCursor({ line: e.position.lineNumber, col: e.position.column }));
                      }}
                      options={{
                        fontFamily: "JetBrains Mono, monospace", fontSize: 14, fontLigatures: true,
                        minimap: { enabled: true }, smoothScrolling: true, cursorSmoothCaretAnimation: "on",
                        bracketPairColorization: { enabled: true }, guides: { bracketPairs: true },
                        padding: { top: 8 }, scrollBeyondLastLine: false, automaticLayout: true,
                      }}
                    />
                  ) : (
                    <div className="flex h-full flex-col items-center justify-center gap-3 text-muted-foreground">
                      <div className="font-mono text-5xl font-bold text-primary/30">ProJ</div>
                      <p>Ctrl+P — открыть файл · Ctrl+B — старт</p>
                      <Button variant="outline" size="sm" onClick={() => { uploadTarget.current = null; uploadRef.current?.click(); }}>+ Добавить файлы</Button>
                    </div>
                  )}
                </div>
              </div>
                            <div className="flex h-56 shrink-0 flex-col border-t border-border">
                <div className="flex h-8 shrink-0 items-center gap-4 px-4 text-[11px] uppercase tracking-wider">
                  {([["problems", `Проблемы ${diags.length || ""}`], ["output", "Вывод"], ["input", "Ввод (stdin)"]] as const).map(([p, l]) => (
                    <button key={p} onClick={() => setPanel(p)} className={cn("h-full border-b", panel === p ? "border-primary text-foreground" : "border-transparent text-muted-foreground hover:text-foreground")}>{l}</button>
                  ))}
                </div>
                <div className="min-h-0 flex-1 overflow-auto px-4 pb-2 font-mono text-xs">
                  {panel === "output" && <pre className="whitespace-pre-wrap">{output || "Нажмите «Старт», чтобы скомпилировать и запустить текущий файл."}</pre>}
                  {panel === "problems" && (diags.length ? diags.map((d, i) => (
                    <button key={i} onClick={() => goto(d)} className="flex w-full items-center gap-2 py-0.5 text-left hover:bg-muted">
                      {d.isError ? <CircleX className="h-3.5 w-3.5 text-destructive" /> : <TriangleAlert className="h-3.5 w-3.5 text-warning" />}
                      {d.message} <span className="text-muted-foreground">[{d.line}, {d.col}]</span>
                    </button>
                  )) : <p className="text-muted-foreground">Проблем не обнаружено.</p>)}
                  {panel === "input" && <textarea value={stdin} onChange={(e) => setStdin(e.target.value)} placeholder="Данные для программы (stdin)…" className="h-full w-full resize-none bg-transparent outline-none" />}
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* status bar */}
      <div className="flex h-6 items-center gap-4 bg-status px-3 text-xs text-status-foreground">
        <button onClick={() => setPanel("problems")} className="flex items-center gap-1">
          <CircleX className="h-3.5 w-3.5" /> {errors} <TriangleAlert className="ml-1 h-3.5 w-3.5" /> {warnings}
        </button>
        {running && <span className="flex items-center gap-1"><Loader2 className="h-3 w-3 animate-spin" /> Сборка…</span>}
        <span className="ml-auto">Стр {cursor.line}, стлб {cursor.col}</span>
        <span>UTF-8</span>
        <span>{langLabel[lang]}</span>
      </div>

      <Dialog open={!!ask} onOpenChange={(o) => !o && setAsk(null)}>
        <DialogContent>
          <DialogHeader><DialogTitle>{ask?.title}</DialogTitle></DialogHeader>
          <form onSubmit={(e) => { e.preventDefault(); if (ask?.value.trim()) { ask.onOk(ask.value.trim()); setAsk(null); } }}>
            <Input autoFocus value={ask?.value ?? ""} onChange={(e) => ask && setAsk({ ...ask, value: e.target.value })} />
            <DialogFooter className="mt-4"><Button type="submit">OK</Button></DialogFooter>
          </form>
        </DialogContent>
      </Dialog>

      <Dialog open={!!errDialog} onOpenChange={(o) => !o && setErrDialog(null)}>
        <DialogContent>
          <DialogHeader><DialogTitle className="text-destructive">Ошибка компиляции — строка {errDialog?.line}</DialogTitle></DialogHeader>
          <p className="font-mono text-sm">{errDialog?.message}</p>
          <DialogFooter><Button onClick={() => { errDialog && goto(errDialog); setErrDialog(null); }}>Перейти</Button></DialogFooter>
        </DialogContent>
      </Dialog>

      <CommandDialog open={palette} onOpenChange={setPalette}>
        <CommandInput placeholder="Найти файл по имени…" />
        <CommandList>
          <CommandEmpty>Ничего не найдено.</CommandEmpty>
          <CommandGroup heading="Файлы">
            {allFiles.map(({ p, f }) => (
              <CommandItem key={p.id + f.id} value={`${f.name} ${p.name} ${f.id}`} onSelect={() => { setOpen((o) => ({ ...o, [p.id]: true })); openFile(p.id, f.id); setPalette(false); }}>
                <FileCode className={cn("mr-2 h-4 w-4", langColor(f.name))} /> {f.name}
                <span className="ml-2 text-xs text-muted-foreground">{p.name}</span>
              </CommandItem>
            ))}
          </CommandGroup>
        </CommandList>
      </CommandDialog>
    </div>
  );
}

function IconBtn({ children, title, onClick }: { children: React.ReactNode; title: string; onClick: () => void }) {
  return <button title={title} onClick={onClick} className="rounded p-0.5 text-muted-foreground hover:bg-secondary hover:text-foreground">{children}</button>;
}
function langColor(name: string) {
  const l = detectLang(name);
  return l === "python" ? "text-warning" : l === "cpp" || l === "c" ? "text-primary" : l === "asm" ? "text-destructive" : "text-muted-foreground";
}
