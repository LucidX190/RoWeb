self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", e => e.waitUntil(self.clients.claim()));
self.addEventListener("fetch", function(ev) {
  if (ev.request.cache === "only-if-cached" && ev.request.mode !== "same-origin") return;
  ev.respondWith(fetch(ev.request).then(function(r) {
    if (r.status === 0) return r;
    var h = new Headers(r.headers);
    h.set("Cross-Origin-Opener-Policy", "same-origin");
    h.set("Cross-Origin-Embedder-Policy", "require-corp");
    return new Response(r.body, {status: r.status, statusText: r.statusText, headers: h});
  }));
});
