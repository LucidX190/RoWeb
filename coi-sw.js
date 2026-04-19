self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", e => e.waitUntil(self.clients.claim()));
self.addEventListener("fetch", function(ev) {
  if (ev.request.cache === "only-if-cached" && ev.request.mode !== "same-origin") return;
  ev.respondWith(fetch(ev.request).then(function(r) {
    if (r.status === 0) return r;
    var h = new Headers(r.headers);
    // Only add COOP/COEP to same-origin navigation responses (the HTML page itself).
    // Cross-origin resources (JS, data from raw.githubusercontent.com) must NOT get
    // COEP injected — that would cause the browser to block them under require-corp.
    if (ev.request.mode === "navigate") {
      h.set("Cross-Origin-Opener-Policy", "same-origin");
      h.set("Cross-Origin-Embedder-Policy", "require-corp");
    } else {
      // For cross-origin subresources, add CORP so COEP allows them
      h.set("Cross-Origin-Resource-Policy", "cross-origin");
    }
    return new Response(r.body, {status: r.status, statusText: r.statusText, headers: h});
  }));
});
