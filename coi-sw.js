self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", e => e.waitUntil(self.clients.claim()));
self.addEventListener("fetch", function(ev) {
  // Only intercept same-origin requests — let cross-origin fetches pass through untouched
  var url = new URL(ev.request.url);
  if (url.origin !== self.location.origin) return;

  ev.respondWith(fetch(ev.request).then(function(r) {
    if (r.status === 0) return r;
    var h = new Headers(r.headers);
    h.set("Cross-Origin-Opener-Policy", "same-origin");
    h.set("Cross-Origin-Embedder-Policy", "credentialless");
    return new Response(r.body, {status: r.status, statusText: r.statusText, headers: h});
  }));
});
