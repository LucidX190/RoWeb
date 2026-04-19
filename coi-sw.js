self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", e => e.waitUntil(self.clients.claim()));
self.addEventListener("fetch", function(ev) {
  var url = new URL(ev.request.url);
  // Let cross-origin fetches pass through untouched
  if (url.origin !== self.location.origin) return;
  // Blob URLs are same-origin but must not be re-streamed through the SW —
  // the WASM pthread workers fetch them directly and re-wrapping breaks them.
  if (url.protocol === 'blob:') return;
  // Avoid breaking "only-if-cached" requests with non-same-origin mode
  if (ev.request.cache === "only-if-cached" && ev.request.mode !== "same-origin") return;

  ev.respondWith(fetch(ev.request).then(function(r) {
    if (r.status === 0) return r;
    var h = new Headers(r.headers);
    h.set("Cross-Origin-Opener-Policy", "same-origin");
    h.set("Cross-Origin-Embedder-Policy", "credentialless");
    return new Response(r.body, {status: r.status, statusText: r.statusText, headers: h});
  }));
});
