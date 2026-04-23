self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", e => e.waitUntil(self.clients.claim()));
self.addEventListener("fetch", function(ev) {
  var url = new URL(ev.request.url);
  if (url.protocol === 'blob:') return;

  // CORS proxy: any URL whose path contains /proxy/<host>/<rest>
  // The WASM uses relative "proxy/<host>/<path>" which resolves to
  // <page-origin>/<page-dir>/proxy/<host>/<path> — always within our scope.
  var proxyIdx = url.pathname.indexOf('/proxy/');
  if (proxyIdx !== -1) {
    var target = 'https://' + url.pathname.slice(proxyIdx + 7) + url.search;
    ev.respondWith(
      fetch(target, { method: ev.request.method })
        .then(function(r) {
          var h = new Headers(r.headers);
          h.set('Access-Control-Allow-Origin', '*');
          return new Response(r.body, { status: r.status, statusText: r.statusText, headers: h });
        })
        .catch(function(err) {
          console.error('[SW proxy] failed to fetch', target, ':', err);
          return new Response('proxy error: ' + err, { status: 502 });
        })
    );
    return;
  }

  if (url.origin !== self.location.origin) return;
  if (ev.request.cache === "only-if-cached" && ev.request.mode !== "same-origin") return;

  ev.respondWith(fetch(ev.request).then(function(r) {
    if (r.status === 0) return r;
    var h = new Headers(r.headers);
    h.set("Cross-Origin-Opener-Policy", "same-origin");
    h.set("Cross-Origin-Embedder-Policy", "credentialless");
    return new Response(r.body, { status: r.status, statusText: r.statusText, headers: h });
  }));
});
