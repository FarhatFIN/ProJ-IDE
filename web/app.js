// ProJ IDE — vanilla JS frontend over the C++ engine API (port of Ide.tsx)
"use strict";

// ---------------------------------------------------------------- state
const S = {
  projects: [],
  expanded: new Set(),
  tabs: [],            // [path]
  active: null,
  buffers: new Map(),  // path -> content
  dirty: new Set(),
  diags: [],
  output: "",
  running: false,
  view: "explorer",
  panel: "output",
  theme: localStorage.getItem("proj-ide/theme") || "dark",
  sysDark: matchMedia("(prefers-color-scheme: dark)").matches,
  cursor: { line: 1, col: 1 },
};
let monacoEd = null, monaco = null;
const models = new Map(); // path -> monaco model

const $ = (id) => document.getElementById(id);
const LANG = {
  cpp: "cpp", c: "c", python: "python", asm: "mips", markdown: "markdown", text: "plaintext",
};
const LABEL = { cpp: "C++", c: "C", python: "Python", asm: "NASM", markdown: "Markdown", text: "Plain Text" };
const monacoLangOf = (lang) => LANG[lang] || "plaintext";

function detectLang(name) {
  const ext = (name.split(".").pop() || "").toLowerCase();
  if (["cpp", "cc", "cxx", "hpp", "hh", "h"].includes(ext)) return "cpp";
  if (ext === "c") return "c";
  if (ext === "py") return "python";
  if (["asm", "s", "nasm"].includes(ext)) return "asm";
  if (["md", "markdown"].includes(ext)) return "markdown";
  return "text";
}
const langColor = (name) => {
  const l = detectLang(name);
  return l === "python" ? "lang-python" : (l === "cpp" || l === "c") ? "lang-cpp"
       : l === "asm" ? "lang-asm" : "lang-other";
};
const fileName = (p) => p.split("/").pop();
const projOf = (path) => S.projects.find((p) => p.files.some((f) => f.path === path));

// ---------------------------------------------------------------- api
async function api(path, body) {
  const r = await fetch(path, body === undefined
    ? {}
    : { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  const j = await r.json();
  if (!r.ok && !j.error) j.error = "HTTP " + r.status;
  return j;
}

// ---------------------------------------------------------------- theme
const dark = () => S.theme === "system" ? S.sysDark : S.theme === "dark";
function applyTheme() {
  document.documentElement.classList.toggle("light", !dark());
  localStorage.setItem("proj-ide/theme", S.theme);
  if (monaco) monaco.editor.setTheme(dark() ? "proj-dark" : "proj-light");
  document.querySelectorAll(".theme-row").forEach((b) =>
    b.classList.toggle("active", b.dataset.theme === S.theme));
}
matchMedia("(prefers-color-scheme: dark)").addEventListener("change", (e) => {
  S.sysDark = e.matches; applyTheme();
});
document.querySelectorAll(".theme-row").forEach((b) =>
  b.addEventListener("click", () => { S.theme = b.dataset.theme; applyTheme(); }));

// ---------------------------------------------------------------- icons
const svgFile = '<svg class="file-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7Z"/><path d="M14 2v4a2 2 0 0 0 2 2h4"/></svg>';
const svgX = '<svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 6 6 18M6 6l12 12"/></svg>';
const svgChev = '<svg class="chev" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="m9 18 6-6-6-6"/></svg>';
const svgTrash = '<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 6h18M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>';
const svgPlus = '<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 5v14M5 12h14"/></svg>';
const svgUpload = '<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M17 8l-5-5-5 5M12 3v12"/></svg>';
const svgErr = '<svg class="ic" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="m15 9-6 6M9 9l6 6"/></svg>';
const svgWarn = '<svg class="ic" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3Z"/><path d="M12 9v4M12 17h.01"/></svg>';

// ---------------------------------------------------------------- rendering
function renderTree() {
  const tree = $("tree");
  tree.innerHTML = "";
  if (!S.projects.length) {
    tree.innerHTML = '<div class="tree-empty">Нет проектов. Нажмите «Файлы+».</div>';
    return;
  }
  S.projects.forEach((p, i) => {
    const row = document.createElement("div");
    row.className = "proj-row" + (S.expanded.has(p.name) ? " open" : "");
    row.innerHTML = `${svgChev}<span class="proj-num">${i + 1}.</span><span class="proj-name">${escapeHtml(p.name)}</span>
      <span class="proj-actions">
        <button class="icon-btn" data-act="newfile" title="Новый файл">${svgPlus}</button>
        <button class="icon-btn" data-act="addfiles" title="Добавить файлы">${svgUpload}</button>
        <button class="icon-btn" data-act="delproj" title="Удалить проект">${svgTrash}</button>
      </span>`;
    row.addEventListener("click", (e) => {
      const act = e.target.closest("[data-act]");
      if (act) { e.stopPropagation(); handleProjectAction(act.dataset.act, p.name); return; }
      toggleProject(p);
    });
    tree.appendChild(row);
    if (S.expanded.has(p.name)) {
      p.files.forEach((f) => {
        const fr = document.createElement("div");
        fr.className = "file-row" + (S.active === f.path ? " active" : "");
        fr.innerHTML = `<span class="${langColor(f.name)}">${svgFile}</span>
          <span class="file-name">${escapeHtml(f.name)}</span>
          ${p.last === f.path ? '<span class="last-dot" title="Последний открытый"></span>' : ""}
          <button class="file-x" title="Удалить из проекта">${svgX}</button>`;
        fr.addEventListener("click", (e) => {
          if (e.target.closest(".file-x")) { e.stopPropagation(); removeFile(p, f); return; }
          openFile(f.path);
        });
        tree.appendChild(fr);
      });
    }
  });
}

function handleProjectAction(act, name) {
  if (act === "newfile") askDialog("Имя файла:", "main.cpp", async (v) => {
    const r = await api("/api/newfile", { project: name, name: v });
    if (r.ok) await refresh(r.path);
  });
  else if (act === "addfiles") { pendingProject = name; $("fileInput").click(); }
  else if (act === "delproj") {
    if (!confirm(`Удалить проект «${name}» из списка? (файлы на диске останутся)`)) return;
    S.tabs = S.tabs.filter((t) => projOf(t)?.name !== name);
    if (S.active && projOf(S.active)?.name === name) { S.active = null; S.tabs = S.tabs.slice(); renderTabs(); }
    api("/api/deleteproject", { project: name }).then(refresh);
  }
}

async function removeFile(p, f) {
  if (!confirm(`Убрать «${f.name}» из проекта? (файл на диске не удаляется)`)) return;
  closeTab(f.path, true);
  await api("/api/deletefile", { project: p.name, path: f.path });
  await refresh();
}

function renderTabs() {
  const bar = $("tabbar");
  bar.innerHTML = "";
  S.tabs.forEach((path) => {
    const p = projOf(path);
    if (!p) return;
    const f = p.files.find((x) => x.path === path);
    const t = document.createElement("div");
    t.className = "tab" + (S.active === path ? " active" : "");
    t.innerHTML = `<span class="${langColor(f.name)}" style="display:inline-flex">${svgFile}</span>
      ${escapeHtml(f.name)}${S.dirty.has(path) ? '<span class="dirty"></span>' : ""}
      <button class="x" title="Закрыть">${svgX}</button>`;
    t.addEventListener("click", (e) => {
      if (e.target.closest(".x")) { e.stopPropagation(); closeTab(path); return; }
      if (e.button === 2) return;
      openFile(path);
    });
    t.addEventListener("contextmenu", (e) => { e.preventDefault(); closeTab(path); });
    bar.appendChild(t);
  });
}

function renderBreadcrumb() {
  const p = projOf(S.active);
  $("breadcrumb").innerHTML = !p ? ""
    : `${escapeHtml(p.name)} <span>›</span> ${escapeHtml(fileName(S.active))}`;
}

function renderDiags() {
  const errs = S.diags.filter((d) => d.isError);
  const badge = $("errBadge");
  badge.textContent = `Errors<${errs.length}>`;
  badge.classList.toggle("bad", errs.length > 0);
  badge.classList.toggle("muted", !errs.length);
  $("probCount").textContent = S.diags.length ? String(S.diags.length) : "";

  const dl = $("diagList");
  dl.innerHTML = "";
  S.diags.forEach((d) => {
    const row = document.createElement("button");
    row.className = "diag-row " + (d.isError ? "err" : "warn");
    row.innerHTML = `${d.isError ? svgErr : svgWarn}<span class="msg">${escapeHtml(d.line + ": " + d.msg)}</span>`;
    row.addEventListener("click", () => gotoDiag(d));
    dl.appendChild(row);
  });
  const pr = $("problems");
  pr.innerHTML = S.diags.length ? "" : '<p class="muted" style="padding:12px 16px">Проблем не обнаружено.</p>';
  S.diags.forEach((d) => {
    const row = document.createElement("button");
    row.className = "prob-row " + (d.isError ? "err" : "warn");
    row.innerHTML = `${d.isError ? svgErr : svgWarn}
      <span>${escapeHtml(d.msg)}</span><span class="loc">[${d.line}, ${d.col}]</span>`;
    row.addEventListener("click", () => gotoDiag(d));
    pr.appendChild(row);
  });
  $("stErrors").textContent = "✖ " + errs.length;
  $("stErrors").className = errs.length ? "bad" : "";
  $("stWarnings").textContent = "⚠ " + (S.diags.length - errs.length);
}

function renderStatus() {
  $("stCursor").textContent = `Стр ${S.cursor.line}, стлб ${S.cursor.col}`;
  const p = projOf(S.active);
  $("stLang").textContent = S.active ? LABEL[detectLang(fileName(S.active))] : "—";
  $("paletteLabel").textContent = p ? p.name : "Поиск файлов";
  $("runFileName").textContent = S.active ? fileName(S.active) : "нет файла";
}

function updateEmptyState() {
  const empty = !S.active;
  $("emptyState").classList.toggle("hidden", !empty);
  $("editorHost").style.visibility = empty ? "hidden" : "visible";
}

// ---------------------------------------------------------------- monaco
function ensureModel(path, content) {
  if (!models.has(path)) {
    models.set(path, monaco.editor.createModel(content, monacoLangOf(detectLang(fileName(path)))));
    models.get(path).onDidChangeContent(() => {
      S.dirty.add(path);
      renderTabs();
    });
  }
  return models.get(path);
}

function setMarkers() {
  if (!monacoEd || !S.active) return;
  const model = models.get(S.active);
  if (!model) return;
  monacoEd.deltaDecorations ? null : null;
  monaco.editor.setModelMarkers(model, "proj", S.diags.map((d) => ({
    startLineNumber: d.line, endLineNumber: d.line,
    startColumn: Math.max(1, d.col), endColumn: Math.max(1, d.col) + 1,
    message: d.msg,
    severity: d.isError ? monaco.MarkerSeverity.Error : monaco.MarkerSeverity.Warning,
  })));
}

function gotoDiag(d) {
  if (!S.active || !monacoEd) return;
  monacoEd.revealLineInCenter(d.line);
  monacoEd.setPosition({ lineNumber: d.line, column: Math.max(1, d.col) });
  monacoEd.focus();
}

function initMonaco() {
  window.MonacoEnvironment = {
    getWorkerUrl: () => URL.createObjectURL(new Blob([
      "self.MonacoEnvironment={baseUrl:'/monaco/vs/'};importScripts('/monaco/vs/base/worker/workerMain.js');"
    ], { type: "text/javascript" })),
  };
  require.config({ paths: { vs: "/monaco/vs" } });
  require(["vs/editor/editor.main"], () => {
    monaco = window.monaco;
    monaco.editor.defineTheme("proj-dark", {
      base: "vs-dark", inherit: true,
      rules: [
        { token: "keyword", foreground: "569CD6" },
        { token: "string", foreground: "CE9178" },
        { token: "comment", foreground: "6A9955" },
        { token: "number", foreground: "B5CEA8" },
        { token: "preprocessor", foreground: "C586C0" },
      ],
      colors: { "editor.background": "#1b1e23", "editorGutter.background": "#1b1e23" },
    });
    monaco.editor.defineTheme("proj-light", {
      base: "vs", inherit: true,
      rules: [
        { token: "keyword", foreground: "0000FF" },
        { token: "string", foreground: "A31515" },
        { token: "comment", foreground: "008000" },
        { token: "number", foreground: "098658" },
        { token: "preprocessor", foreground: "7F0055" },
      ],
      colors: { "editor.background": "#f6f7f8", "editorGutter.background": "#f6f7f8" },
    });
    monacoEd = monaco.editor.create($("editorHost"), {
      model: null,
      theme: dark() ? "proj-dark" : "proj-light",
      fontFamily: "JetBrains Mono, monospace",
      fontSize: 14,
      fontLigatures: true,
      minimap: { enabled: true },
      smoothScrolling: true,
      bracketPairColorization: { enabled: true },
      guides: { bracketPairs: true },
      padding: { top: 8 },
      scrollBeyondLastLine: false,
      automaticLayout: true,
    });
    monacoEd.onDidChangeCursorPosition((e) => {
      S.cursor = { line: e.position.lineNumber, col: e.position.column };
      renderStatus();
    });
    updateEmptyState();
    // reopen current tab into the now-ready editor
    if (S.active) {
      const path = S.active;
      S.active = null;
      openFile(path);
    }
  });
}

// ---------------------------------------------------------------- files
async function refresh(openPath) {
  const st = await api("/api/state");
  if (st.ok) S.projects = st.projects;
  renderTree();
  if (openPath) openFile(openPath);
}

function openFile(path) {
  const doOpen = (content) => {
    if (!S.tabs.includes(path)) S.tabs.push(path);
    S.active = path;
    S.diags = [];
    setMarkers(); renderDiags();
    renderTabs(); renderBreadcrumb(); renderStatus(); updateEmptyState();
    if (monacoEd) {
      const model = ensureModel(path, content);
      monacoEd.setModel(model);
    }
    const p = projOf(path);
    if (p && p.last !== path) api("/api/open", { path }).then(() => {
      p.last = path; renderTree();
    });
  };
  if (S.buffers.has(path)) { doOpen(S.buffers.get(path)); return; }
  api("/api/open", { path }).then((r) => {
    if (!r.ok) { alert(r.error || "Не удалось открыть файл"); return; }
    S.buffers.set(path, r.content);
    doOpen(r.content);
  });
}

function closeTab(path, silent) {
  const idx = S.tabs.indexOf(path);
  if (idx === -1) return;
  const finish = () => {
    S.tabs.splice(idx, 1);
    S.dirty.delete(path);
    S.buffers.delete(path);
    if (S.active === path) {
      S.active = S.tabs[S.tabs.length - 1] || null;
      if (S.active && monacoEd) monacoEd.setModel(ensureModel(S.active, S.buffers.get(S.active)));
      S.diags = []; setMarkers(); renderDiags();
    }
    renderTabs(); renderBreadcrumb(); renderStatus(); updateEmptyState();
  };
  if (!silent && S.dirty.has(path)) {
    if (!confirm("Сохранить изменения в «" + fileName(path) + "» перед закрытием?")) { finish(); return; }
    save();
  }
  finish();
}

async function save() {
  if (!S.active || !S.buffers.has(S.active)) return;
  const content = monacoEd && models.get(S.active) ? models.get(S.active).getValue() : S.buffers.get(S.active);
  const r = await api("/api/save", { path: S.active, content });
  if (r.ok) {
    S.buffers.set(S.active, content);
    S.dirty.delete(S.active);
    renderTabs();
  } else alert(r.error || "Не удалось сохранить");
}

function toggleProject(p) {
  const willOpen = !S.expanded.has(p.name);
  willOpen ? S.expanded.add(p.name) : S.expanded.delete(p.name);
  renderTree();
  if (willOpen && p.files.length) {
    const f = p.files.find((x) => x.path === p.last) || p.files[0];
    openFile(f.path);
  }
}

// ---------------------------------------------------------------- build
function outAppend(text) {
  const pre = $("output");
  const atBottom = pre.scrollHeight - pre.scrollTop - pre.clientHeight < 60;
  pre.textContent += text;
  if (atBottom) pre.scrollTop = pre.scrollHeight;
}

// live interactive run: program attached to the Вывод panel
let liveTimer = null;

function liveStop() {
  if (liveTimer) { clearTimeout(liveTimer); liveTimer = null; }
  $("procRow").classList.add("hidden");
}

function liveRun(produced) {
  api("/api/runlive", { path: S.active, produced }).then((r) => {
    if (!r.ok) { outAppend("\n✖ " + (r.error || "не удалось запустить")); return; }
    $("procRow").classList.remove("hidden");
    $("procInput").focus();
    const poll = () => {
      api("/api/proc/read").then((st) => {
        if (st.chunk) outAppend(st.chunk);
        if (st.running) {
          liveTimer = setTimeout(poll, 350);
        } else {
          liveStop();
          outAppend(`\n✔ программа завершилась (код ${st.exitCode})`);
        }
      });
    };
    poll();
  });
}

async function start() {
  if (!S.active || S.running) return;
  S.running = true;
  $("startBtn").disabled = true;
  liveStop();
  setPanel("output");
  $("output").textContent = "▶ " + fileName(S.active) + "\nСборка…";
  try {
    await runBuild();
  } catch (e) {
    outAppend("\nОшибка: " + (e && e.message ? e.message : e));
  }
  S.running = false;
  $("startBtn").disabled = false;
}

async function runBuild() {
  await save();
  let r = await api("/api/build", { path: S.active, stdin: "" });

  if (r.needInstall) {
    const tool = r.needInstall;
    const okgo = confirm(`У вас не нашлось \"${tool}\".\nМы можем скачать его из доверенных источников.\n\nРазрешаю?`);
    if (!okgo) { outAppend("\nОтменено."); return; }
    await api("/api/install", { tool });
    while (true) {
      const st = await api("/api/installstatus");
      $("output").textContent = `Загружаем ${tool}… ` +
        (st.percent >= 0 ? st.percent + "%" : "…");
      if (st.done) {
        if (st.outcome === 2) { $("output").textContent = "Ошибка сети."; return; }
        if (st.outcome !== 1) {
          $("output").textContent = "Не удалось установить:\n" + (st.detail || "");
          return;
        }
        break;
      }
      await new Promise((res) => setTimeout(res, 400));
    }
    r = await api("/api/build", { path: S.active, stdin: "" });
  }

  S.diags = (r.errors || []).map((e) => ({ ...e }));
  setMarkers(); renderDiags();
  outAppend("\n" + (r.log || "") + (r.ok ? "✔ Собрано\n" : "✖ Завершено с ошибками\n"));
  if (r.ok) {
    const lang = detectLang(fileName(S.active));
    if (lang === "python") liveRun("");
    else if (lang === "cpp" || lang === "c" || lang === "asm") liveRun(r.produced);
  }
  const firstErr = S.diags.find((d) => d.isError);
  if (firstErr) {
    setPanel("problems");
    $("errTitle").textContent = "Ошибка компиляции — строка " + firstErr.line;
    $("errMsg").textContent = firstErr.msg;
    $("errOverlay").classList.remove("hidden");
  }
}

// ---------------------------------------------------------------- palette
let paletteRows = [];
function paletteFill(filter) {
  const f = filter.toLowerCase();
  const list = $("paletteList");
  list.innerHTML = "";
  paletteRows = [];
  S.projects.forEach((p) => p.files.forEach((file) => {
    if (f && !file.name.toLowerCase().includes(f) && !p.name.toLowerCase().includes(f)) return;
    paletteRows.push({ path: file.path, name: file.name, proj: p.name });
  }));
  paletteRows.forEach((row, i) => {
    const el = document.createElement("button");
    el.className = "palette-row" + (i === 0 ? " sel" : "");
    el.innerHTML = `<span class="${langColor(row.name)}" style="display:inline-flex">${svgFile}</span>
      ${escapeHtml(row.name)}<span class="proj">${escapeHtml(row.proj)}</span>`;
    el.addEventListener("click", () => { paletteOpen(row.path); });
    list.appendChild(el);
  });
  if (!paletteRows.length) list.innerHTML = '<div class="palette-empty">Ничего не найдено.</div>';
}
function paletteSelMove(dir) {
  const rows = [...$("paletteList").children];
  if (!rows.length) return;
  let i = rows.findIndex((r) => r.classList.contains("sel"));
  rows[i]?.classList.remove("sel");
  i = (i + dir + rows.length) % rows.length;
  rows[i].classList.add("sel");
  rows[i].scrollIntoView({ block: "nearest" });
}
function paletteOpen(path) {
  $("paletteOverlay").classList.add("hidden");
  const p = projOf(path);
  if (p) S.expanded.add(p.name);
  refresh(path);
}
function paletteShow() {
  $("paletteInput").value = "";
  paletteFill("");
  $("paletteOverlay").classList.remove("hidden");
  $("paletteInput").focus();
}

// ---------------------------------------------------------------- dialogs
let askOkHandler = null;
function askDialog(title, def, onOk) {
  $("askTitle").textContent = title;
  $("askInput").value = def || "";
  askOkHandler = onOk;
  $("askOverlay").classList.remove("hidden");
  $("askInput").focus();
  $("askInput").select();
}
function askConfirmOk() {
  const v = $("askInput").value.trim();
  if (!v) return;
  $("askOverlay").classList.add("hidden");
  if (askOkHandler) askOkHandler(v);
  askOkHandler = null;
}

// ---------------------------------------------------------------- panels/views
function setPanel(name) {
  S.panel = name;
  document.querySelectorAll(".ptab").forEach((b) => b.classList.toggle("active", b.dataset.panel === name));
  ["problems", "output"].forEach((n) =>
    $("panel-" + n).classList.toggle("hidden", n !== name));
}
function setView(name) {
  S.view = name;
  document.querySelectorAll(".act").forEach((b) => b.classList.toggle("active", b.dataset.view === name));
  ["explorer", "run", "settings"].forEach((n) =>
    $("view-" + n).classList.toggle("hidden", n !== name));
}

// ---------------------------------------------------------------- uploads
let pendingProject = null; // null → new project
async function onUpload(files) {
  if (!files || !files.length) return;
  const payload = [];
  for (const f of files) payload.push({ name: f.name, content: await f.text() });
  if (pendingProject) {
    await api("/api/addfiles", { project: pendingProject, files: payload });
  } else {
    askDialog("Название вашего проекта:", "Проект " + (S.projects.length + 1), async (name) => {
      await api("/api/newproject", { name, files: payload });
      await refresh();
      const p = S.projects.find((x) => x.name === name);
      if (p && p.files.length) {
        S.expanded.add(name);
        renderTree();
        openFile(p.files[0].path);
      }
    });
  }
  pendingProject = null;
}

// ---------------------------------------------------------------- misc
function escapeHtml(s) {
  return s.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

// ---------------------------------------------------------------- wire up
$("startBtn").addEventListener("click", start);
$("runStartBtn").addEventListener("click", start);
$("procInput").addEventListener("keydown", (e) => {
  if (e.key === "Enter" && $("procInput").value) {
    api("/api/proc/write", { data: $("procInput").value + "\n" });
    $("procInput").value = "";
  }
});
$("procStop").addEventListener("click", () => api("/api/proc/kill"));
$("statusErr").addEventListener("click", () => { setView("explorer"); setPanel("problems"); });
$("errBadge").addEventListener("click", () => setPanel("problems"));
document.querySelectorAll(".act").forEach((b) => b.addEventListener("click", () => setView(b.dataset.view)));
document.querySelectorAll(".ptab").forEach((b) => b.addEventListener("click", () => setPanel(b.dataset.panel)));
$("paletteBtn").addEventListener("click", paletteShow);
$("newProjectBtn").addEventListener("click", () => { pendingProject = null; $("fileInput").click(); });
$("addFilesBtn").addEventListener("click", () => {
  const p = projOf(S.active);
  pendingProject = p ? p.name : null;
  $("fileInput").click();
});
$("emptyAddBtn").addEventListener("click", () => { pendingProject = null; $("fileInput").click(); });
$("fileInput").addEventListener("change", (e) => { onUpload(e.target.files); e.target.value = ""; });

$("askOk").addEventListener("click", askConfirmOk);
$("askCancel").addEventListener("click", () => $("askOverlay").classList.add("hidden"));
$("askInput").addEventListener("keydown", (e) => {
  if (e.key === "Enter") askConfirmOk();
  if (e.key === "Escape") $("askOverlay").classList.add("hidden");
});
$("errClose").addEventListener("click", () => $("errOverlay").classList.add("hidden"));
$("errGoto").addEventListener("click", () => {
  const firstErr = S.diags.find((d) => d.isError);
  if (firstErr) gotoDiag(firstErr);
  $("errOverlay").classList.add("hidden");
});

$("paletteInput").addEventListener("input", (e) => paletteFill(e.target.value));
$("paletteInput").addEventListener("keydown", (e) => {
  if (e.key === "ArrowDown") { e.preventDefault(); paletteSelMove(1); }
  else if (e.key === "ArrowUp") { e.preventDefault(); paletteSelMove(-1); }
  else if (e.key === "Enter") {
    const sel = $("paletteList").querySelector(".sel");
    if (sel) sel.click();
    else $("paletteList").firstElementChild?.click();
  } else if (e.key === "Escape") $("paletteOverlay").classList.add("hidden");
});
$("paletteOverlay").addEventListener("click", (e) => {
  if (e.target === $("paletteOverlay")) $("paletteOverlay").classList.add("hidden");
});

addEventListener("keydown", (e) => {
  if (!(e.ctrlKey || e.metaKey)) return;
  const k = e.key.toLowerCase();
  if (k === "s") { e.preventDefault(); save(); }
  else if (k === "b" || k === "r" || e.key === "Enter") { e.preventDefault(); start(); }
  else if (k === "p") { e.preventDefault(); paletteShow(); }
});

// ---------------------------------------------------------------- boot
applyTheme();
setView("explorer");
setPanel("output");
refresh().then(() => {
  // restore last project's last file
  if (S.projects.length) {
    const p = S.projects[0];
    S.expanded.add(p.name);
    renderTree();
    const target = p.files.find((f) => f.path === p.last) || p.files[0];
    if (target) openFile(target.path);
  }
});
initMonaco();
