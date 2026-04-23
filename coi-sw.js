// !! Set this to your Cloudflare Worker URL after deploying cf_worker.js !!
var CF_WORKER = 'https://rowebproxy.lucidx190.workers.dev/';

self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", e => e.waitUntil(self.clients.claim()));
self.addEventListener("fetch", function(ev) {
  var url = new URL(ev.request.url);
  if (url.protocol === 'blob:') return;

  // CORS proxy: any URL whose path contains /proxy/<host>/<rest>
  // C++ emits relative "proxy/<host>/<path>" which resolves to
  // <page-origin>/<page-dir>/proxy/<host>/<path> — within SW scope.
  var proxyIdx = url.pathname.indexOf('/proxy/');
  if (proxyIdx !== -1) {
    var hostAndPath = url.pathname.slice(proxyIdx + 7); // after /proxy/
    // Route through Cloudflare Worker which fetches server-side (no CORS restriction)
    var target = CF_WORKER + '/' + hostAndPath + url.search;
    ev.respondWith(
      fetch(target, { method: ev.request.method })
        .then(function(r) {
          return r;
        })
        .catch(function(err) {
          console.error('[SW proxy] CF Worker fetch failed for', target, ':', err);
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
