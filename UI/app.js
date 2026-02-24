// Shared helpers for all pages

export function $(id) {
  return document.getElementById(id);
}

// Same-origin (UI + API served from the same Crow server)
export function apiBase() {
  return ""; // keep empty so fetch uses the current origin automatically
}

export function setStatus(text, kind = "muted") {
  const el = $("status");
  if (!el) return;

  el.className =
    kind === "error" ? "pill danger" :
    kind === "ok" ? "pill ok" :
    "muted";

  el.textContent = text;
}

export function pretty(obj) {
  return JSON.stringify(obj, null, 2);
}

export async function apiFetch(path, opts = {}) {
  const url = apiBase() + path;

  const headers = Object.assign(
    { "Content-Type": "application/json" },
    opts.headers || {}
  );

  const res = await fetch(url, Object.assign({}, opts, { headers }));

  const text = await res.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = { raw: text };
  }

  const last = $("lastJson");
  if (last) {
    // Display a fully qualified URL for readability in the UI
    const fullUrl = window.location.origin + path;
    last.textContent = pretty({ url: fullUrl, status: res.status, ok: res.ok, data });
  }

  if (!res.ok) {
    const msg = (data && data.error) ? data.error : ("Request failed: " + res.status);
    throw new Error(msg);
  }

  return data;
}

export async function healthCheck() {
  setStatus("Checking health…");
  try {
    const path = "/health";
    const res = await fetch(apiBase() + path);
    const text = await res.text();

    const last = $("lastJson");
    if (last) {
      last.textContent = pretty({
        url: window.location.origin + path,
        status: res.status,
        ok: res.ok,
        data: text
      });
    }

    if (!res.ok) throw new Error("Health check failed: " + res.status);
    setStatus("Health: " + text, "ok");
  } catch (e) {
    setStatus(e.message, "error");
  }
}

// No-op now (API base URL input removed from UI)
export function wireApiBaseInput() {
  // intentionally empty
}

export function setActiveNav(pathname) {
  document.querySelectorAll("a.navlink").forEach(a => {
    const href = a.getAttribute("href");
    if (!href) return;
    a.classList.toggle("active", href.endsWith(pathname));
  });
}

export function esc(s) {
  return String(s)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

export function qs(name) {
  const u = new URL(window.location.href);
  return u.searchParams.get(name);
}

export function setQs(name, value) {
  const u = new URL(window.location.href);
  u.searchParams.set(name, value);
  window.location.href = u.toString();
}