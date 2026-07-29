# cwb applications

A cwb application is what a web application is to a web server, restated for
CES: a live Lua service runs on the server, CWB is its native client, and the
multi-user and economic reality it needs is physics it inherits. HTML is a
response format and widget-embedding envelope, not a generated application
cache.

## Anatomy

An application is five things, three of them optional:

1. **A name.** Not `ces-*` or `cwb-*`; a brand, like any web application.
   The first application is Vellum (below).
2. **Pages: the integrator.** HTML responses from a live Lua service embed
   widgets via `<object type="application/x-cwb-...">` with `<param>`s. The
   page composes and configures; Lua owns routing and shared state.
3. **Widgets: the runtime.** Native C++ QWidgets compiled into cwb, registered
   by MIME type, handed a `WidgetContext` (identity, server, navigation).
   Widgets hold all application logic and all CES client verbs. cwb is
   deliberately a fat binary: every application's widgets ship in the one
   browser, the way every DOM API ships in Chrome.
4. **State: the zones.** Application data is files under the actor's own zone
   (`/h/<pub>/...`, always writable by its signer), a named zone
   (`/f/<app>/...`, owner-gated, transferable), or the operator zone (`/s/`).
   Paths are capabilities; there is no LIST verb, so any index an application
   wants it must maintain itself (see middleware).
5. **A server half (optional).** Shared multi-writer state — feeds, counters,
   inboxes — cannot be a flat file some stranger writes (every zone has one
   writer). It is a Lua compute extension owning that state, spoken to over
   `ces.conn`. Installing that extension is the full "installing an
   application on the server" move.

## The middleware discipline

Application logic lives in the widget, between the user gesture and the file
store. Rules learned by building the first application:

- **Vellum publishing is name-backed.** `/f/<name>/` is the product contract:
  keep the keyname funded or its writing is no longer yours to maintain.
- **Views are served, not stored.** Catalogs, shelves, and feeds render live
  in the application service from its own records, pruned against the file
  store at request time. The only rent-paying artifacts are the works
  themselves: an index that could die with the catalog in it must not exist.
- **STAT, then READ exactly.** READ rejects a range past EOF; it never
  short-reads.
- **RESIZE, then WRITE.** WRITE never grows a file; growth pays upfront rent
  through RESIZE/APPEND. Size the file exactly, then write the whole page.
- **Mutations invalidate.** Navigation after an action must bypass the page
  cache (`WidgetContext::navigateTo` does) or the user is shown the
  pre-mutation page.
- **Deposits adapt.** A widget cannot read the payer's balance on the rpc
  lane; ladder the deposit downward on INSUFFICIENT_BALANCE and report what
  the file was fed.
- **Local first.** The canonical local work copy commits atomically before a
  remote mutation. Discovery failures do not erase the live artifact.

## Economics

The publisher's identity pays: file deposits (rent), write I/O, per-op query
fees. Readers pay read I/O and any per-KB price the owner set. An idle server
discounts fees toward zero (feemult), so the empty city is nearly free. An
application surfaces costs in its own vocabulary ("story fund"), never raw
protocol numbers as the primary reading.

## Security

Pages are data. A hostile page can embed a widget with hostile params, but
every widget spends only through consent (arm+confirm), signs only with the
browser identity, and can only do what its compiled verbs do. Adding
application capability = adding a widget = a cwb code change, reviewed and
compiled, never downloaded.

## Vellum

The first cwb application: writing meant to be kept. A riff on Medium —
same cadence, opposite thesis: stories live because their author feeds them
rent, on a server nobody owns twice.

Current shape:
- **Desk** (`/write` on Vellum's live Lua service): the fullscreen
  `x-cwb-write` widget. Serif title, Charter body, word count, green Publish
  with consent. Publishing typesets the story, uploads it to
  `/f/<author_name>/vellum/<slug>.html`, funds its rent, announces it, and
  navigates to the live page.
- **Article**: 680px measure, 42px title, byline, 21/1.58 Charter body,
  footer linking the author's live shelf via the stable dynamic-app address
  (`compute://<host>/s/vellum.lua/by/<name>`; cwb resolves the live instance
  and relays); the quiet `vellum` mark; the `x-cwb-story` vitals bar (fund
  balance + address for everyone; list/unpublish/delete for the author).
- **Shelf** (`/by/<name>` on Vellum's live Lua service): the author's public
  page of stories, rendered at request time from announce records and pruned
  against the file store — the ONLY rent-paying artifacts are the stories.
  A dead index cannot lose the catalog, because there is no index.

The server half (built; ships in the CES extensions catalog):

- **`cwb.lua`, the live application directory server.** Applications discover
  it through the compute catalog and register over `ces.conn`. It verifies the
  caller is the advertised live `/s/` instance and keeps a bounded in-memory
  lease. `GET /` renders the current directory at request time.
  `ces://<server>/apps` (and the Apps toolbar button) asks the main protocol
  for the advertised CesPlex port, discovers this service, and dials it, with
  `/s/index.html` only as a compatibility fallback. There is no registry file
  or cached HTML. A bare host means the CES server root, not Apps.
- **`vellum.lua`** — the reference application extension. Registers with the
  directory, serves routes and pages live, and accepts
  `published|path|title|author` announcements over
  `ces.conn` from the browser's write surface — verifying the story exists
  in the file store before listing it (the announcement is a hint, the file
  store is the truth). It authenticates the caller against the path owner and
  persists a compact bounded snapshot in `/s/vellum/stories.state`.

Known gap (the tourist problem): file READs are signed and paid, so an
identity with NO ACCOUNT on a server cannot read even `/s/` pages there
(CES_ERROR_ORIGIN_NOT_FOUND). Every funnel that starts at "visit a strange
server" runs through mining-first or a gateway (cesweb). Deliberate CES
economics, but it shapes onboarding: the browser should probably surface
"mine a little to enter this server" rather than a bare error.
