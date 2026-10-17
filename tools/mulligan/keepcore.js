/* MagicDeckTester -- mulligan checker core (tools/mulligan/keepcore.js)
 * ====================================================================================================
 * Everything the mulligan checker computes, with no engine, no server and no dependencies. Runs in a
 * browser (global `KeepCore`) and in Node (`require('./keepcore')`, used by test/mulligan_app_check.js).
 *
 * WHAT IT READS. Only the deck's RAW keep sidecar (<stem>.keepmodel.exhaustive.raw.json[.gz]) plus the
 * decklist and the play profile's mulligan.keep_table_alias list. The compiled table the engine plays
 * from (<stem>.keepmodel.exhaustive.profile.json.gz, up to 1.7 GB of JSON) is NOT needed: it is a pure
 * function of the raw sidecar, and this file recomputes it exactly --
 *   keep flag   KeepVal(h,m,pd) <= Dopt[pd][m+1]   (forced keep at m == max_mull)
 *   bottoming   the first strict argmin of V over the hand's (7-m)-card subcompositions, restricted to
 *               refined cells (count > sub_target) when the table was generated with adaptive bottoming
 * -- the rules in src/analyzer/ExhaustiveKeep.cpp (KeepVal, ComputeDopt, BuildPolicyFromTables). The
 * floating-point operations are the same ones in the same order, so the decisions are bit-identical;
 * test/mulligan_app_check.js holds this to the shipped compiled table for every deck.
 *
 * MEMORY. The raw sidecar is up to ~300 MB of JSON uncompressed, so it is never held whole: bytes are
 * streamed through StreamJSON, which materialises the small header and hands each table entry to a
 * callback that writes it straight into typed arrays (~33 bytes per cell; ~90 MB for the largest deck).
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) { module.exports = factory(); }
  else { root.KeepCore = factory(); }
})(typeof self !== 'undefined' ? self : this, function () {
'use strict';

const HAND = 7;

// =====================================================================================================
// 1. Streaming JSON
// =====================================================================================================
// A resumable byte-level parser. Containers outside the streamed arrays are materialised as ordinary
// JS values; an array for which streamAt(path) is true is NOT -- each of its elements is parsed whole
// and passed to onElement(value, stack) instead. `stack` exposes the enclosing frames, so an element
// can read keys its ancestors already parsed (a raw sidecar's entry reads its size object's "H").
//
// Input arrives in arbitrary chunks. Every unit (a structural token, a scalar, or one streamed element)
// either completes or throws NEED_MORE, in which case the cursor stays at the unit's first byte and the
// unconsumed tail is carried into the next push(). Frames change only after a unit completes, so a
// boundary can fall anywhere.
const NEED_MORE = { needMore: true };   // sentinel; caught in run(), never escapes

function isWs(c) { return c === 32 || c === 10 || c === 13 || c === 9; }

class StreamJSON {
  constructor(streamAt, onElement) {
    this.streamAt = streamAt;
    this.onElement = onElement;
    this.buf = new Uint8Array(0);
    this.pos = 0;              // committed cursor into buf
    this.q = 0;                // working cursor for the value parsers
    this.base = 0;             // absolute offset of buf[0] (error messages)
    this.stack = [];
    this.root = undefined;
    this.done = false;
    this.td = new TextDecoder('utf-8');
  }

  push(chunk) {
    if (this.done) { return; }
    const rest = this.buf.length - this.pos;
    this.base += this.pos;
    let b = chunk;
    if (rest > 0) {
      b = new Uint8Array(rest + chunk.length);
      b.set(this.buf.subarray(this.pos));
      b.set(chunk, rest);
    }
    this.buf = b;
    this.pos = 0;
    this.run();
  }

  end() {
    if (!this.done) { throw new Error('the file ended before its JSON did (truncated, or not JSON)'); }
    return this.root;
  }

  err(p, msg) { return new Error('JSON parse error at byte ' + (this.base + p) + ': ' + msg); }

  run() {
    const b = this.buf, n = b.length;
    try {
      while (!this.done) {
        let p = this.pos;
        while (p < n && isWs(b[p])) { p++; }
        if (p >= n) { this.pos = p; return; }
        const top = this.stack.length ? this.stack[this.stack.length - 1] : null;
        const c = b[p];
        if (!top) { this.beginValue(b, p, null); continue; }
        if (top.kind === 0) {                                    // ---- object
          if (top.st === 0 || top.st === 3) {                    // key (or '}' straight after '{')
            if (c === 125 && top.st === 0) { this.pos = p + 1; this.close(); continue; }
            if (c !== 34) { throw this.err(p, 'expected a key'); }
            this.q = p; top.key = this.parseString(b); this.pos = this.q; top.st = 1;
          } else if (top.st === 1) {
            if (c !== 58) { throw this.err(p, "expected ':'"); }
            this.pos = p + 1; top.st = 2;
          } else if (top.st === 2) {
            this.beginValue(b, p, top);
          } else {                                               // st 4: ',' or '}'
            if (c === 44) { this.pos = p + 1; top.st = 3; }
            else if (c === 125) { this.pos = p + 1; this.close(); }
            else { throw this.err(p, "expected ',' or '}'"); }
          }
        } else {                                                 // ---- array
          if (top.st === 0 && c === 93) { this.pos = p + 1; this.close(); continue; }
          if (top.st === 0 || top.st === 2) {
            if (top.stream) {
              this.q = p; const v = this.parseValue(b); this.pos = this.q;
              top.count++; top.st = 4;
              this.onElement(v, this.stack);
            } else {
              this.beginValue(b, p, top);
            }
          } else {                                               // st 4: ',' or ']'
            if (c === 44) { this.pos = p + 1; top.st = 2; }
            else if (c === 93) { this.pos = p + 1; this.close(); }
            else { throw this.err(p, "expected ',' or ']'"); }
          }
        }
      }
    } catch (e) {
      if (e !== NEED_MORE) { throw e; }
    }
  }

  beginValue(b, p, parent) {
    const c = b[p];
    if (c === 123 || c === 91) {
      const path = parent ? parent.path.concat([parent.kind === 0 ? parent.key : parent.val.length]) : [];
      this.pos = p + 1;
      if (c === 123) { this.stack.push({ kind: 0, st: 0, val: {}, key: null, path: path }); }
      else { this.stack.push({ kind: 1, st: 0, val: [], path: path, stream: !!this.streamAt(path), count: 0 }); }
      return;
    }
    this.q = p;
    const v = this.parseScalar(b);
    this.pos = this.q;
    this.assign(v);
  }

  assign(v) {
    const top = this.stack.length ? this.stack[this.stack.length - 1] : null;
    if (!top) { this.root = v; this.done = true; return; }
    if (top.kind === 0) { top.val[top.key] = v; } else { top.val.push(v); }
    top.st = 4;
  }

  close() { const f = this.stack.pop(); this.assign(f.val); }

  // A whole value starting at this.q (streamed elements are parsed with this).
  parseValue(b) {
    const n = b.length;
    let p = this.q;
    while (p < n && isWs(b[p])) { p++; }
    if (p >= n) { throw NEED_MORE; }
    const c = b[p];
    if (c === 91) {
      const arr = [];
      p++;
      for (;;) {
        while (p < n && isWs(b[p])) { p++; }
        if (p >= n) { throw NEED_MORE; }
        if (b[p] === 93) { this.q = p + 1; return arr; }
        if (arr.length) { if (b[p] !== 44) { throw this.err(p, "expected ','"); } p++; }
        this.q = p; arr.push(this.parseValue(b)); p = this.q;
      }
    }
    if (c === 123) {
      const obj = {};
      let first = true;
      p++;
      for (;;) {
        while (p < n && isWs(b[p])) { p++; }
        if (p >= n) { throw NEED_MORE; }
        if (b[p] === 125) { this.q = p + 1; return obj; }
        if (!first) {
          if (b[p] !== 44) { throw this.err(p, "expected ','"); }
          p++;
          while (p < n && isWs(b[p])) { p++; }
          if (p >= n) { throw NEED_MORE; }
        }
        first = false;
        if (b[p] !== 34) { throw this.err(p, 'expected a key'); }
        this.q = p; const k = this.parseString(b); p = this.q;
        while (p < n && isWs(b[p])) { p++; }
        if (p >= n) { throw NEED_MORE; }
        if (b[p] !== 58) { throw this.err(p, "expected ':'"); }
        this.q = p + 1; obj[k] = this.parseValue(b); p = this.q;
      }
    }
    this.q = p;
    return this.parseScalar(b);
  }

  parseScalar(b) {
    const c = b[this.q];
    if (c === 34) { return this.parseString(b); }
    if (c === 116) { return this.parseWord(b, 'true', true); }
    if (c === 102) { return this.parseWord(b, 'false', false); }
    if (c === 110) { return this.parseWord(b, 'null', null); }
    return this.parseNumber(b);
  }

  parseWord(b, w, v) {
    const p = this.q;
    if (p + w.length > b.length) { throw NEED_MORE; }
    for (let i = 0; i < w.length; i++) { if (b[p + i] !== w.charCodeAt(i)) { throw this.err(p, 'bad literal'); } }
    this.q = p + w.length;
    return v;
  }

  parseString(b) {
    const n = b.length;
    const s = this.q + 1;
    let p = s, esc = false, ascii = true;
    for (;;) {
      if (p >= n) { throw NEED_MORE; }
      const c = b[p];
      if (c === 34) { break; }
      if (c === 92) { esc = true; p += 2; continue; }
      if (c >= 128) { ascii = false; }
      p++;
    }
    this.q = p + 1;
    let str = (ascii && p - s <= 64) ? String.fromCharCode.apply(null, b.subarray(s, p))
                                     : this.td.decode(b.subarray(s, p));
    if (esc) { str = JSON.parse('"' + str + '"'); }
    return str;
  }

  // Integers and integer-valued doubles ("25.0", which is how nlohmann writes the sidecar's sums) take
  // a digit loop; anything with a real fraction or an exponent goes through parseFloat. An integer too
  // large for a double (the 64-bit fingerprints in `meta`) comes back as a BigInt, exact.
  parseNumber(b) {
    const n = b.length;
    const start = this.q;
    let p = start, neg = false, v = 0;
    if (b[p] === 45) { neg = true; p++; }
    const d0 = p;
    while (p < n) { const d = b[p] - 48; if (d < 0 || d > 9) { break; } v = v * 10 + d; p++; }
    if (p >= n) { throw NEED_MORE; }
    if (p === d0) { throw this.err(p, 'bad number'); }
    const c = b[p];
    if (c === 46 || c === 101 || c === 69) {
      let q = p, zeroFrac = true;
      if (c === 46) {
        q++;
        while (q < n && b[q] >= 48 && b[q] <= 57) { if (b[q] !== 48) { zeroFrac = false; } q++; }
        if (q >= n) { throw NEED_MORE; }
      }
      if (b[q] === 101 || b[q] === 69) {
        zeroFrac = false;
        q++;
        if (q < n && (b[q] === 43 || b[q] === 45)) { q++; }
        while (q < n && b[q] >= 48 && b[q] <= 57) { q++; }
        if (q >= n) { throw NEED_MORE; }
      }
      this.q = q;
      if (zeroFrac && v <= Number.MAX_SAFE_INTEGER) { return neg ? -v : v; }
      return parseFloat(String.fromCharCode.apply(null, b.subarray(start, q)));
    }
    this.q = p;
    if (v > Number.MAX_SAFE_INTEGER) { return BigInt(String.fromCharCode.apply(null, b.subarray(start, p))); }
    return neg ? -v : v;
  }
}

// Feed an async iterable of Uint8Array chunks (or a ReadableStream) through a StreamJSON. Small chunks
// (DecompressionStream emits ~16 KB) are gathered into ~1 MB batches first: every push() copies its
// input into one contiguous buffer, so pushing tiny chunks pays that copy and the per-call overhead
// tens of thousands of times. An exception from a callback (including STOP, below) cancels the source.
const BATCH = 1 << 20;
async function pumpStream(source, parser, onBytes) {
  let pend = [], pendLen = 0;
  const flush = async () => {
    if (!pendLen) { return; }
    let u = pend[0];
    if (pend.length > 1) {
      u = new Uint8Array(pendLen);
      let o = 0;
      for (const c of pend) { u.set(c, o); o += c.length; }
    }
    pend = []; pendLen = 0;
    parser.push(u);
    if (onBytes) { await onBytes(u.length); }
  };
  const take = async (u) => {
    pend.push(u); pendLen += u.length;
    if (pendLen >= BATCH) { await flush(); }
  };
  if (source && typeof source.getReader === 'function') {
    const reader = source.getReader();
    try {
      for (;;) {
        const r = await reader.read();
        if (r.done) { break; }
        await take(r.value);
      }
      await flush();
    } catch (e) {
      try { await reader.cancel(); } catch (e2) { /* already closed */ }
      throw e;
    }
  } else {
    for await (const chunk of source) { await take(chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk)); }
    await flush();
  }
  return parser.end();
}
const STOP = { stop: true };   // throw from an onElement callback to stop reading early

// =====================================================================================================
// 2. Decklists
// =====================================================================================================
function decodeXml(s) {
  return s.replace(/&(#x[0-9a-fA-F]+|#[0-9]+|amp|lt|gt|quot|apos);/g, function (m, e) {
    if (e[0] === '#') { return String.fromCodePoint(e[1] === 'x' || e[1] === 'X' ? parseInt(e.slice(2), 16) : parseInt(e.slice(1), 10)); }
    return { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'" }[e];
  });
}

// Mainboard of a decklist, in file order, duplicates merged: [{name, count}]. Mirrors
// src/deck/DeckLoader.cpp -- Cockatrice .cod (every zone except side/sideboard is the mainboard) and
// plain text ("4 Name" / "4x Name", '#' and '/' comments, a sideboard header ends the main).
function parseDecklist(text, fileName) {
  const out = [], at = new Map();
  const add = (name, count) => {
    name = name.trim();
    if (!name || !(count > 0)) { return; }
    if (at.has(name)) { out[at.get(name)].count += count; }
    else { at.set(name, out.length); out.push({ name: name, count: count }); }
  };
  if (/<cockatrice_deck/i.test(text) || /\.cod$/i.test(fileName || '')) {
    const zoneRe = /<zone\b([^>]*)>([\s\S]*?)<\/zone>/gi;
    let z;
    while ((z = zoneRe.exec(text))) {
      const zn = (/\bname\s*=\s*"([^"]*)"/i.exec(z[1]) || [])[1] || '';
      if (/^(side|sideboard)$/i.test(zn)) { continue; }
      const cardRe = /<card\b([^>]*?)\/?>/gi;
      let c;
      while ((c = cardRe.exec(z[2]))) {
        const nm = (/\bname\s*=\s*"([^"]*)"/i.exec(c[1]) || [])[1];
        const num = (/\bnumber\s*=\s*"([^"]*)"/i.exec(c[1]) || [])[1];
        if (nm) { add(decodeXml(nm), num === undefined ? 1 : (parseInt(num, 10) || 1)); }
      }
    }
  } else {
    for (let line of text.split(/\r?\n/)) {
      line = line.trim();
      if (!line || line[0] === '/' || line[0] === '#') { continue; }
      const low = line.toLowerCase();
      if (low === 'sideboard' || low === 'sideboard:' || low.startsWith('sb:')) { break; }
      const m = /^(\d+)[xX]?\s+(.*)$/.exec(line);
      let name = m ? m[2] : line;
      const count = m ? parseInt(m[1], 10) : 1;
      const paren = name.indexOf(' (');
      if (paren >= 0) { name = name.slice(0, paren); }
      add(name, count);
    }
  }
  return out;
}

// FNV-1a 64 over UTF-8 bytes, chained -- the same hash as Fnv() in ExhaustiveKeep.cpp.
const FNV_OFFSET = 1469598103934665603n, FNV_PRIME = 1099511628211n, MASK64 = (1n << 64n) - 1n;
function fnv(str, h) {
  const bytes = new TextEncoder().encode(str);
  for (const c of bytes) { h = ((h ^ BigInt(c)) * FNV_PRIME) & MASK64; }
  return h;
}
// DeckFp(): FNV-1a over the sorted mainboard names, one per copy, each followed by ','. std::sort on
// std::string is a byte-wise comparison, hence the UTF-8 compare below rather than localeCompare.
function deckFp(cards) {
  const enc = new TextEncoder();
  const names = [];
  for (const c of cards) { for (let i = 0; i < c.count; i++) { names.push(c.name); } }
  const keyed = names.map(nm => ({ nm: nm, b: enc.encode(nm) }));
  keyed.sort((x, y) => {
    const n = Math.min(x.b.length, y.b.length);
    for (let i = 0; i < n; i++) { if (x.b[i] !== y.b[i]) { return x.b[i] - y.b[i]; } }
    return x.b.length - y.b.length;
  });
  let h = FNV_OFFSET;
  for (const k of keyed) { h = fnv(k.nm + ',', h); }
  return h;
}

// =====================================================================================================
// 3. Small maths
// =====================================================================================================
function comb(n, k) {
  if (k < 0 || k > n) { return 0; }
  k = Math.min(k, n - k);
  let r = 1;
  for (let i = 1; i <= k; i++) { r = r * (n - k + i) / i; }
  return Math.round(r);
}
// Dense solve of A x = b (A is d*d, modified in place) by Gauss-Jordan with partial pivoting --
// BottomSolve in ExhaustiveKeep.cpp, operation for operation.
function solveGJ(A, b, d) {
  for (let c = 0; c < d; c++) {
    let piv = c;
    for (let r = c + 1; r < d; r++) { if (Math.abs(A[r * d + c]) > Math.abs(A[piv * d + c])) { piv = r; } }
    if (Math.abs(A[piv * d + c]) < 1e-12) { continue; }
    if (piv !== c) {
      for (let k = 0; k < d; k++) { const t = A[c * d + k]; A[c * d + k] = A[piv * d + k]; A[piv * d + k] = t; }
      const t = b[c]; b[c] = b[piv]; b[piv] = t;
    }
    const pv = A[c * d + c];
    for (let r = 0; r < d; r++) {
      if (r === c) { continue; }
      const f = A[r * d + c] / pv;
      if (f === 0.0) { continue; }
      for (let k = c; k < d; k++) { A[r * d + k] -= f * A[c * d + k]; }
      b[r] -= f * b[c];
    }
  }
  const x = new Float64Array(d);
  for (let i = 0; i < d; i++) { if (Math.abs(A[i * d + i]) > 1e-12) { x[i] = b[i] / A[i * d + i]; } }
  return x;
}

function normCdf(z) {                 // Phi(z), via erf (Abramowitz-Stegun 7.1.26, |err| < 1.5e-7)
  const x = Math.abs(z) / Math.SQRT2;
  const t = 1 / (1 + 0.3275911 * x);
  const y = 1 - (((((1.061405429 * t - 1.453152027) * t) + 1.421413741) * t - 0.284496736) * t + 0.254829592) * t * Math.exp(-x * x);
  return z >= 0 ? 0.5 * (1 + y) : 0.5 * (1 - y);
}

// How sure the table is of a comparison: z = |difference| / combined standard error. The tiers are
// presentation only (they change no decision):
//   Clear     z >= 2   (>= ~98% that a fresh, larger sample would make the same call)
//   Likely    z >= 1   (>= ~84%)
//   Marginal  z <  1   (a coin-flip-ish call; the two options are close or barely sampled)
const TIERS = [{ name: 'Clear', z: 2 }, { name: 'Likely', z: 1 }, { name: 'Marginal', z: -Infinity }];
function confidence(diff, se) {
  const z = se > 0 ? Math.abs(diff) / se : (diff === 0 ? 0 : Infinity);
  const tier = TIERS.find(t => z >= t.z).name;
  return { z: z, p: normCdf(z), tier: tier };
}

// =====================================================================================================
// 4. The keep table
// =====================================================================================================
class KeepTable {
  // deckCards: [{name,count}] (parseDecklist); aliases: [{name, as, why}] (play profile).
  constructor(deckCards, aliases) {
    this.deckCards = deckCards || [];
    this.aliases = (aliases || []).filter(a => a && a.name && a.as && a.why);
    this.ready = false;
    this.cellsLoaded = 0;
    this.totalGames = 0;
    this.sizes = {};          // H -> {N, V, n, vc, present}
    this.dopt = null;         // [pd][m]
  }

  // Header (buckets + meta) -> bucket maps, counts and the rank tables. Called on the first entry.
  setup(buckets, meta) {
    if (!Array.isArray(buckets) || !meta) {
      throw new Error('not a raw keep sidecar (expected "buckets" and "meta" ahead of "sizes")');
    }
    this.buckets = buckets;
    this.meta = meta;
    const K = this.K = buckets.length;
    this.maxMull = meta.max_mull;
    this.R = meta.R;
    this.subTarget = meta.sub_target;
    // Adaptive bottoming (sub_target < R): the generator restricts the bottom argmin to refined cells,
    // count > sub_target (it passes bottom_floor = r0 = sub_target). Full bottoming (sub_target == R)
    // applies no filter. A sidecar too old to carry the stamp leaves this undefined; finish() infers a
    // candidate from the counts, and calibrate() settles it against the shipped table.
    this.stampedFloor = (typeof meta.sub_target === 'number' && typeof meta.R === 'number')
      ? (meta.sub_target < meta.R ? meta.sub_target : -1) : undefined;
    this.sePrior = typeof meta.se_prior === 'number' ? meta.se_prior : 8;

    this.bucketOf = new Map();
    buckets.forEach((members, b) => members.forEach(nm => this.bucketOf.set(nm, b)));
    this.aliasUsed = [];
    for (const a of this.aliases) {
      if (!this.bucketOf.has(a.name) && this.bucketOf.has(a.as)) {
        this.bucketOf.set(a.name, this.bucketOf.get(a.as));
        this.aliasUsed.push(a);
      }
    }
    this.count = new Array(K).fill(0);
    this.unbucketed = [];
    this.deckSize = 0;
    for (const c of this.deckCards) {
      this.deckSize += c.count;
      if (this.bucketOf.has(c.name)) { this.count[this.bucketOf.get(c.name)] += c.count; }
      else { this.unbucketed.push(c); }
    }
    // Was the table generated on THIS decklist? deck_fp is checked as written and with every alias
    // substituted (an alias exists precisely because a card was renamed after generation).
    this.deckMatch = 'unknown';
    if (typeof meta.deck_fp === 'bigint' || typeof meta.deck_fp === 'number') {
      const want = BigInt(meta.deck_fp);
      if (deckFp(this.deckCards) === want) { this.deckMatch = 'exact'; }
      else {
        const asMap = new Map(this.aliasUsed.map(a => [a.name, a.as]));
        const sub = this.deckCards.map(c => ({ name: asMap.get(c.name) || c.name, count: c.count }));
        this.deckMatch = (asMap.size && deckFp(sub) === want) ? 'alias' : 'mismatch';
      }
    }
    // Rank caps: the deck's copies per bucket (a composition can hold no more). When the decklist is
    // NOT the one the table was built on, its counts cannot bound the table's compositions, so rank
    // over the uncapped space instead (bigger, but correct for any table).
    this.cap = this.deckMatch === 'mismatch' ? new Array(K).fill(HAND) : this.count.map(c => Math.min(c, HAND));
    // NC[b*8+r] = number of compositions of size r over buckets b..K-1 within the caps.
    const NC = this.NC = new Float64Array((K + 1) * 8);
    NC[K * 8] = 1;
    for (let b = K - 1; b >= 0; b--) {
      for (let r = 0; r <= HAND; r++) {
        let s = 0;
        for (let x = 0; x <= Math.min(this.cap[b], r); x++) { s += NC[(b + 1) * 8 + r - x]; }
        NC[b * 8 + r] = s;
      }
    }
    // OFF[(b*8+r)*8+x] = how many compositions precede x copies of bucket b at remaining size r --
    // the lexicographic rank is the sum of these over the buckets, the order EnumComps walks in.
    const OFF = this.OFF = new Float64Array(K * 64);
    for (let b = 0; b < K; b++) {
      for (let r = 0; r <= HAND; r++) {
        let s = 0;
        for (let x = 0; x <= HAND; x++) {
          OFF[(b * 8 + r) * 8 + x] = s;
          if (x <= r) { s += NC[(b + 1) * 8 + r - x]; }
        }
      }
    }
  }

  rank(comp, H) {
    let r = H, idx = 0;
    for (let b = 0; b < this.K; b++) {
      const x = comp[b];
      if (x) {
        if (x > this.cap[b] || x > r) { return -1; }
        idx += this.OFF[(b * 8 + r) * 8 + x];
        r -= x;
      }
    }
    return r === 0 ? idx : -1;
  }

  sizeTable(H) {
    let t = this.sizes[H];
    if (!t) {
      const N = this.NC[H];                         // NC[0*8+H]
      t = this.sizes[H] = {
        H: H, N: N,
        V: new Float64Array(2 * N),                 // mean win turn [draw, play]
        n: new Uint32Array(2 * N),                  // rollouts behind it
        vc: new Float64Array(2 * N).fill(NaN),      // per-rollout variance (NaN: fewer than 2)
        present: new Uint8Array(N),
        vg: [0, 0],                                 // pooled per-rollout variance (finish())
      };
    }
    return t;
  }

  addEntry(e, H) {
    const idx = this.rank(e.comp, H);
    if (idx < 0) {
      throw new Error('a table composition does not fit this decklist (' + JSON.stringify(e.comp) + ' at H=' + H + ')');
    }
    const t = this.sizeTable(H);
    t.present[idx] = 1;
    for (let pd = 0; pd < 2; pd++) {
      const c = e.count[pd], s = e.sum[pd];
      const j = 2 * idx + pd;
      t.n[j] = c;
      t.V[j] = c ? s / c : 0.0;                     // V = sum/count, exactly as the generator derives it
      if (c > 1 && e.sumsq) { const mean = s / c; t.vc[j] = Math.max(0, e.sumsq[pd] / c - mean * mean); }
      this.totalGames += c;
    }
    this.cellsLoaded++;
  }

  // Stream a raw sidecar in. `source` is a ReadableStream or async iterable of DECOMPRESSED bytes.
  async load(source, onProgress) {
    const parser = new StreamJSON(
      path => path.length === 3 && path[0] === 'sizes' && path[2] === 'entries',
      (e, stack) => {
        if (!this.buckets) { const r = stack[0].val; this.setup(r.buckets, r.meta); }
        const H = stack[2].val.H;
        if (typeof H !== 'number') { throw new Error('raw sidecar: a size block lists its entries before its "H"'); }
        this.addEntry(e, H);
      });
    let bytes = 0;
    await pumpStream(source, parser, onProgress ? (k) => { bytes += k; return onProgress(bytes, this.cellsLoaded); } : null);
    if (!this.buckets) { throw new Error('raw sidecar holds no table entries'); }
    this.finish();
  }

  finish() {
    let minSub = Infinity, atMin = 0, subSides = 0;
    for (const H in this.sizes) {
      const t = this.sizes[H];
      for (let pd = 0; pd < 2; pd++) {
        let s = 0, k = 0;
        for (let i = 0; i < t.N; i++) { const v = t.vc[2 * i + pd]; if (v === v) { s += v; k++; } }
        t.vg[pd] = k ? s / k : 0;
      }
      if (+H === HAND) { continue; }
      for (let i = 0; i < t.N; i++) {
        if (!t.present[i]) { continue; }
        for (let pd = 0; pd < 2; pd++) {
          const c = t.n[2 * i + pd];
          subSides++;
          if (c < minSub) { minSub = c; atMin = 1; } else if (c === minSub) { atMin++; }
        }
      }
    }
    const t7 = this.sizes[HAND];
    if (!t7) { throw new Error('raw sidecar has no 7-card table'); }
    // An unstamped adaptive table shows itself as a block of sub-cells parked at one small floor count.
    this.inferredFloor = (minSub < this.R && subSides && atMin / subSides > 0.05) ? minSub : -1;
    this.rule = { floor: this.stampedFloor !== undefined ? this.stampedFloor : this.inferredFloor, shrink: false };
    this.ruleSource = this.stampedFloor !== undefined ? 'stamped' : 'inferred';
  }

  // Bottoming rules a shipped table may have been built with, most likely first.
  candidateRules() {
    const floors = [this.rule.floor];
    if (this.rule.floor !== -1) { floors.push(-1); }
    if (this.inferredFloor !== -1 && floors.indexOf(this.inferredFloor) < 0) { floors.push(this.inferredFloor); }
    const out = [];
    for (const shrink of [false, true]) { for (const floor of floors) { out.push({ floor: floor, shrink: shrink }); } }
    return out;
  }

  // Every composition of size H within the caps, in rank (= lexicographic) order, packed as its H
  // bucket indices in ascending order: Uint8Array(N*H).
  packComps(H) {
    const K = this.K, N = this.NC[H];
    const packed = new Uint8Array(N * H);
    const cur = new Array(K).fill(0);
    let idx = 0;
    const rec = (b, rem) => {
      if (b === K) {
        if (rem !== 0) { return; }
        let k = 0;
        for (let j = 0; j < K; j++) { for (let x = 0; x < cur[j]; x++) { packed[idx * H + k++] = j; } }
        idx++;
        return;
      }
      for (let x = 0; x <= Math.min(this.cap[b], rem); x++) { cur[b] = x; rec(b + 1, rem - x); }
      cur[b] = 0;
    };
    rec(0, H);
    return packed;
  }

  // ---- shrinkage refinement of the bottoming score ---------------------------------------------------
  // Some tables (FiveColour: a merge with MTG_KEEP_BOTTOM_SHRINK=1) rank bottoming candidates on a
  // shrunk score rather than the raw mean: a precision-weighted quadratic fit of the cell means on the
  // bucket counts (FitBottomPrior), then Z = Vhat + lam (V - Vhat), lam = tau^2 / (tau^2 + se^2) -- each
  // cell borrowing strength from every cell holding the same cards. This is BuildPolicyFromTables' Zt
  // with the same arithmetic in the same order, so the argmin it drives is the shipped one.
  rawSe(t, i, pd) { const c = t.n[2 * i + pd]; return c > 1 ? Math.sqrt(t.vc[2 * i + pd] / c) : 0; }

  // Sparse features of the packed composition at cell i: [1, counts..., pairwise products (i<=j)], in
  // BottomFeatSparse's order. Returns the feature count; indices/values land in fi/fv.
  feats(packed, H, i, quad, fi, fv) {
    const K = this.K;
    const bs = this._fb || (this._fb = new Int32Array(HAND)), cs = this._fc || (this._fc = new Int32Array(HAND));
    let nb = 0;
    for (let k = 0; k < H; k++) {
      const b = packed[i * H + k];
      if (nb && bs[nb - 1] === b) { cs[nb - 1]++; } else { bs[nb] = b; cs[nb] = 1; nb++; }
    }
    let n = 0;
    fi[n] = 0; fv[n] = 1.0; n++;
    for (let a = 0; a < nb; a++) { fi[n] = 1 + bs[a]; fv[n] = cs[a]; n++; }
    if (quad) {
      const base = 1 + K;
      for (let a = 0; a < nb; a++) {
        const ib = bs[a], row = ib * K - ib * (ib - 1) / 2;
        for (let c = a; c < nb; c++) { fi[n] = base + row + (bs[c] - ib); fv[n] = cs[a] * cs[c]; n++; }
      }
    }
    return n;
  }

  fitPrior(t, packed, H, pd) {
    const K = this.K;
    const p = { valid: false, quad: false, beta: null, tau: 0 };
    const rows = [];
    for (let i = 0; i < t.N; i++) { if (t.present[i] && t.n[2 * i + pd] > 1) { rows.push(i); } }
    const dq = 1 + K + K * (K + 1) / 2, da = 1 + K;
    if (rows.length >= 5 * dq) { p.quad = true; }
    else if (rows.length < 5 * da) { return p; }
    const d = p.quad ? dq : da;
    const A = new Float64Array(d * d), b = new Float64Array(d);
    const fi = new Int32Array(64), fv = new Float64Array(64);
    for (const r of rows) {
      const nf = this.feats(packed, H, r, p.quad, fi, fv);
      const w = t.n[2 * r + pd], y = t.V[2 * r + pd];
      for (let a = 0; a < nf; a++) {
        const wxi = w * fv[a], off = fi[a] * d;
        for (let c = 0; c < nf; c++) { A[off + fi[c]] += wxi * fv[c]; }
        b[fi[a]] += wxi * y;
      }
    }
    let tr = 0.0;
    for (let i = 0; i < d; i++) { tr += A[i * d + i]; }
    const ridge = Math.max(1e-9, (tr / d) * 1e-7);
    for (let i = 0; i < d; i++) { A[i * d + i] += ridge; }
    p.beta = solveGJ(A, b, d);
    let sw = 0.0, rv = 0.0, my = 0.0, msq = 0.0;
    for (const r of rows) {
      const nf = this.feats(packed, H, r, p.quad, fi, fv);
      let yh = 0.0;
      for (let a = 0; a < nf; a++) { yh += p.beta[fi[a]] * fv[a]; }
      const w = t.n[2 * r + pd], v = t.V[2 * r + pd], se = this.rawSe(t, r, pd);
      sw += w;
      rv += w * (v - yh) * (v - yh);
      my += w * v;
      msq += w * se * se;
    }
    if (sw <= 0.0) { return p; }
    const mu = my / sw;
    let vy = 0.0;
    for (const r of rows) { const w = t.n[2 * r + pd], v = t.V[2 * r + pd]; vy += w * (v - mu) * (v - mu); }
    const tauRaw = Math.sqrt(Math.max(0.0, vy / sw - msq / sw));
    p.tau = Math.sqrt(Math.max(0.0, rv / sw - msq / sw));
    p.valid = (p.tau > 0.0) && (tauRaw > 0.0) && (p.tau < tauRaw);
    return p;
  }

  async computeShrink(onProgress) {
    if (this.Z) { return; }
    const Z = {};
    const fi = new Int32Array(64), fv = new Float64Array(64);
    for (let m = 1; m <= this.maxMull; m++) {
      const H = HAND - m, t = this.sizes[H];
      if (!t) { continue; }
      const packed = this.packComps(H);
      const z = new Float64Array(2 * t.N);
      for (let pd = 0; pd < 2; pd++) {
        const pr = this.fitPrior(t, packed, H, pd);
        const t2 = pr.tau * pr.tau;
        for (let i = 0; i < t.N; i++) {
          const v = t.V[2 * i + pd], se = this.rawSe(t, i, pd);
          let s = v;
          if (pr.valid && se > 0.0) {
            const nf = this.feats(packed, H, i, pr.quad, fi, fv);
            let vh = 0.0;
            for (let a = 0; a < nf; a++) { vh += pr.beta[fi[a]] * fv[a]; }
            s = vh + (t2 / (t2 + se * se)) * (v - vh);
          }
          z[2 * i + pd] = s;
        }
        if (onProgress) { await onProgress((2 * (m - 1) + pd + 1) / (2 * this.maxMull)); }
      }
      Z[H] = z;
    }
    this.Z = Z;
  }

  // ---- matching the shipped table ---------------------------------------------------------------------
  // Read the first few thousand entries of the compiled table the engine plays from
  // (<stem>.keepmodel.exhaustive.profile.json[.gz]; a few MB of a file that can be gigabytes) and pick
  // the bottoming rule that reproduces them, while checking the keep flags. `source` is a stream of its
  // DECOMPRESSED bytes. Returns a report; this.rule is set to the chosen rule.
  async calibrate(source, opts) {
    opts = opts || {};
    const maxEntries = opts.maxEntries || 6000, maxBytes = opts.maxBytes || (16 << 20);
    const sample = [];
    let bucketsDiffer = false, bytes = 0;
    const parser = new StreamJSON(
      p => p.length === 2 && p[0] === 'exhaustive_keep' && p[1] === 'entries',
      (e, stack) => {
        if (!sample.length && JSON.stringify(stack[1].val.buckets) !== JSON.stringify(this.buckets)) { bucketsDiffer = true; throw STOP; }
        sample.push(e);
        if (sample.length >= maxEntries) { throw STOP; }
      });
    try {
      await pumpStream(source, parser, (k) => { bytes += k; if (bytes > maxBytes && sample.length) { throw STOP; } });
    } catch (e) { if (e !== STOP) { throw e; } }
    const rep = { sampled: sample.length, bucketsDiffer: bucketsDiffer, rule: this.rule, exact: false };
    if (bucketsDiffer) { rep.problem = 'the compiled table buckets the cards differently from this raw sidecar'; return rep; }
    if (!sample.length) { rep.problem = 'no exhaustive keep table entries found'; return rep; }
    const shippedM = Math.max(0, sample[0].keep.length / 2 - 1);
    rep.shippedMaxMull = shippedM;
    const M = Math.min(this.maxMull, shippedM);
    rep.keepMismatchByM = new Array(this.maxMull + 1).fill(0);
    let keepChecks = 0;
    const usable = [];
    for (const e of sample) {
      const idx = this.rank(e.comp, HAND);
      if (idx < 0 || !this.sizes[HAND].present[idx]) { rep.notInRaw = (rep.notInRaw || 0) + 1; continue; }
      usable.push(e);
      for (let m = 0; m <= M; m++) {
        for (let pd = 0; pd < 2; pd++) {
          keepChecks++;
          if ((e.keep[m * 2 + pd] ? true : false) !== this.decideComp(e.comp, m, pd).keep) { rep.keepMismatchByM[m]++; }
        }
      }
    }
    rep.keepChecks = keepChecks;
    const tryRule = (rule) => {
      let bad = 0, tot = 0;
      for (const e of usable) {
        for (let m = 1; m <= M; m++) {
          for (let pd = 0; pd < 2; pd++) {
            const want = e.bottom_keep && e.bottom_keep[m * 2 + pd];
            if (!want) { continue; }
            tot++;
            const got = this.decideComp(e.comp, m, pd, rule).bottomSub;
            for (let b = 0; b < this.K; b++) { if (want[b] !== got[b]) { bad++; break; } }
          }
        }
      }
      return { bad: bad, tot: tot };
    };
    rep.tried = [];
    let best = null;
    for (const rule of this.candidateRules()) {
      if (rule.shrink) { await this.computeShrink(opts.onShrinkProgress); }
      const r = tryRule(rule);
      rep.tried.push({ rule: rule, bad: r.bad, tot: r.tot });
      if (!best || r.bad < best.bad) { best = { rule: rule, bad: r.bad, tot: r.tot }; }
      if (r.bad === 0) { break; }
    }
    this.rule = best.rule;
    this.ruleSource = 'calibrated';
    rep.rule = best.rule;
    rep.bottomMismatch = best.bad;
    rep.bottomChecks = best.tot;
    rep.exact = best.bad === 0 && rep.keepMismatchByM.every(x => x === 0) && !rep.notInRaw;
    // Not reproducible from the raw sidecar alone (a deeper shipped table, keep flags or bottoms that no
    // rule matches): read the compiled table whole and take every decision from it (useShippedTable).
    rep.needsShipped = !rep.exact || shippedM > this.maxMull;
    this.calibration = rep;
    return rep;
  }

  // Standard error of one cell's mean, with the per-rollout variance shrunk toward its table's pooled
  // value by se_prior pseudo-rollouts -- the generator's own noise model (it is what its adaptive
  // sampling stopped on), and what keeps a 3-rollout cell from claiming a variance of zero.
  se(t, idx, pd) {
    const j = 2 * idx + pd, c = t.n[j];
    if (!c) { return Infinity; }
    const vc = t.vc[j] === t.vc[j] ? t.vc[j] : t.vg[pd];
    const vs = (c * vc + this.sePrior * t.vg[pd]) / (c + this.sePrior);
    return Math.sqrt(Math.max(0, vs) / c);
  }

  // Every (7-m)-card subcomposition of hand composition h, in EnumComps order, as a rank into
  // sizes[7-m]: fn(idx, sub) where `sub` is the live K-vector (copy it to keep it).
  forEachSub(h, size, fn) {
    const nz = [];
    for (let b = 0; b < this.K; b++) { if (h[b]) { nz.push(b); } }
    const tail = new Array(nz.length + 1).fill(0);           // copies available from nz[i..]
    for (let i = nz.length - 1; i >= 0; i--) { tail[i] = tail[i + 1] + h[nz[i]]; }
    const sub = new Array(this.K).fill(0);
    const OFF = this.OFF;
    const rec = (i, rem, idx) => {
      if (i === nz.length) { if (rem === 0) { fn(idx, sub); } return; }
      const b = nz[i];
      const hi = Math.min(h[b], rem);
      const lo = Math.max(0, rem - tail[i + 1]);
      for (let x = lo; x <= hi; x++) {
        sub[b] = x;
        rec(i + 1, rem - x, idx + (x ? OFF[(b * 8 + rem) * 8 + x] : 0));
      }
      sub[b] = 0;
    };
    rec(0, size, 0);
  }

  // Every 7-card composition in rank (= lexicographic) order, packed as its 7 bucket indices, with its
  // multivariate-hypergeometric draw probability (HandWeights).
  handsPacked() {
    const N7 = this.sizes[HAND].N;
    const packed = this.packComps(HAND);
    const P = new Float64Array(N7);
    const denom = comb(this.deckSize, HAND);
    for (let i = 0; i < N7; i++) {
      let w = 1, q = 0;
      while (q < HAND) {
        const b = packed[i * HAND + q];
        let c = 0;
        while (q < HAND && packed[i * HAND + q] === b) { c++; q++; }
        w *= comb(this.count[b], c);
      }
      P[i] = w / denom;
    }
    return { packed: packed, P: P };
  }

  // KeepVal(h, m) for every 7-card hand at mulligan level m, both sides: the best mean win turn over
  // the hand's (7-m)-card sub-hands and the cell attaining it -- the first strict minimum in EnumComps
  // order, walked with an odometer (no recursion: this is the hot loop of the whole load).
  async keepValLevel(m, packed, tick) {
    const t7 = this.sizes[HAND], N7 = t7.N, ts = this.sizes[HAND - m];
    if (!ts) { throw new Error('raw sidecar has no ' + (HAND - m) + '-card table'); }
    const V = ts.V, present = ts.present, size = HAND - m, OFF = this.OFF;
    const out = { b: [new Float64Array(N7), new Float64Array(N7)], a: [new Int32Array(N7), new Int32Array(N7)] };
    // Odometer state: the hand's distinct buckets (nb, nh), copies available from level i on (tail),
    // and per level the remaining size, the partial rank, the current choice and its upper bound.
    const nb = new Int32Array(HAND), nh = new Int32Array(HAND), tail = new Int32Array(HAND + 1);
    const rem = new Int32Array(HAND), acc = new Float64Array(HAND), xs = new Int32Array(HAND), hi = new Int32Array(HAND);
    for (let i = 0; i < N7; i++) {
      if (!t7.present[i]) { out.b[0][i] = out.b[1][i] = 1e9; out.a[0][i] = out.a[1][i] = -1; continue; }
      let k = 0;
      for (let q = 0; q < HAND; q++) {
        const b = packed[i * HAND + q];
        if (k && nb[k - 1] === b) { nh[k - 1]++; } else { nb[k] = b; nh[k] = 1; k++; }
      }
      tail[k] = 0;
      for (let q = k - 1; q >= 0; q--) { tail[q] = tail[q + 1] + nh[q]; }
      let b0 = 1e9, b1 = 1e9, a0 = -1, a1 = -1;
      let lv = 0;
      rem[0] = size; acc[0] = 0;
      xs[0] = Math.max(0, size - tail[1]) - 1; hi[0] = Math.min(nh[0], size);
      while (lv >= 0) {
        const x = ++xs[lv];
        if (x > hi[lv]) { lv--; continue; }
        const a = x ? acc[lv] + OFF[(nb[lv] * 8 + rem[lv]) * 8 + x] : acc[lv];
        if (lv === k - 1) {
          if (present[a]) {
            const v0 = V[2 * a], v1 = V[2 * a + 1];
            if (v0 < b0) { b0 = v0; a0 = a; }
            if (v1 < b1) { b1 = v1; a1 = a; }
          }
          continue;
        }
        const r = rem[lv] - x;
        lv++;
        rem[lv] = r; acc[lv] = a;
        xs[lv] = Math.max(0, r - tail[lv + 1]) - 1; hi[lv] = Math.min(nh[lv], r);
      }
      out.b[0][i] = b0; out.b[1][i] = b1; out.a[0][i] = a0; out.a[1][i] = a1;
      if (tick && (i & 4095) === 4095) { await tick(i / N7); }
    }
    return out;
  }

  // Backward induction (ComputeDopt): Dopt[pd][m] = expected win turn of the optimal keep policy from
  // mulligan m. The 7-card hands are visited in lexicographic order -- the order the generator sums in
  // -- so every partial sum, and therefore every keep flag, is bit-identical to the shipped table.
  // Also carries a standard error for each Dopt (cells treated as independent; it is small).
  async computeDopt(onProgress) {
    const M = this.maxMull, t7 = this.sizes[HAND], N7 = t7.N;
    const { packed, P } = this.handsPacked();
    const dopt = [new Array(M + 1).fill(0), new Array(M + 1).fill(0)];
    const dvar = [new Array(M + 1).fill(0), new Array(M + 1).fill(0)];
    let lastYield = Date.now();
    for (let m = M; m >= 0; m--) {
      const step = M - m;
      const L = await this.keepValLevel(m, packed, onProgress ? async (f) => {
        if (Date.now() - lastYield > 40) { await onProgress((step + f) / (M + 1)); lastYield = Date.now(); }
      } : null);
      const ts = this.sizes[HAND - m];
      for (let pd = 0; pd < 2; pd++) {
        const b = L.b[pd], a = L.a[pd], thr = m < M ? dopt[pd][m + 1] : 0;
        let s = 0, v = 0, mullMass = 0;
        for (let i = 0; i < N7; i++) {
          if (!t7.present[i]) { continue; }
          const p = P[i];
          if (m === M) {
            s += p * b[i];
            if (a[i] >= 0) { const e = this.se(ts, a[i], pd); v += p * p * e * e; }
          } else {
            s += p * Math.min(b[i], thr);
            if (b[i] <= thr) { if (a[i] >= 0) { const e = this.se(ts, a[i], pd); v += p * p * e * e; } } else { mullMass += p; }
          }
        }
        dopt[pd][m] = s;
        dvar[pd][m] = v + (m < M ? mullMass * mullMass * dvar[pd][m + 1] : 0);
      }
    }
    this.dopt = dopt;
    this.doptSe = [dvar[0].map(Math.sqrt), dvar[1].map(Math.sqrt)];
    this.levels = M;
    this.ready = true;
  }

  // ---- when this raw sidecar does not reproduce the shipped table -------------------------------------
  // The case it exists for: Anti-Lifegain's table was deepened from 3 to 6 mulligans by a run that read
  // the committed 3-level raw sidecar as its warm start, and that run's own output (the 3-, 2- and 1-card
  // tables) was never committed. calibrate() notices (shipped keep flags differ, or the shipped table is
  // deeper than the raw); this then reads the WHOLE compiled table once and keeps its decisions --
  // 14 keep bits and 14 bottom targets per hand -- so every call the page shows is the engine's own.
  //
  // It also recovers the one number the lost tables fed into the levels the raw DOES cover: the value
  // of mulliganing past its last level, Dopt[M+1]. The shipped keep flags at level M bracket it
  // (keep iff KeepVal <= Dopt[M+1]); every value in the bracket implies a backward-induction chain
  // Dopt[M..0], and the chain must reproduce the shipped flags at every lower level too. The page uses
  // the middle of the values that reproduce all of them, and reports how far the feasible range spreads.
  async useShippedTable(source, onProgress) {
    const t7 = this.sizes[HAND], N7 = t7.N, K = this.K;
    let S = -1, flags = null, bottoms = null, entries = 0, missing = 0;
    const parser = new StreamJSON(
      p => p.length === 2 && p[0] === 'exhaustive_keep' && p[1] === 'entries',
      (e) => {
        if (S < 0) {
          S = e.keep.length / 2 - 1;
          flags = new Uint16Array(N7);
          bottoms = new Uint32Array(N7 * (S + 1) * 2).fill(0xFFFFFFFF);
        }
        entries++;
        const idx = this.rank(e.comp, HAND);
        if (idx < 0 || !t7.present[idx]) { missing++; return; }
        let f = 0;
        for (let j = 0; j < 2 * (S + 1); j++) { if (e.keep[j]) { f |= 1 << j; } }
        flags[idx] = f;
        if (e.bottom_keep) {
          for (let m = 0; m <= S; m++) {
            for (let pd = 0; pd < 2; pd++) {
              const row = e.bottom_keep[m * 2 + pd];
              if (row && row.length === K) { bottoms[(idx * (S + 1) + m) * 2 + pd] = this.rank(row, HAND - m); }
            }
          }
        }
      });
    await pumpStream(source, parser, onProgress ? async () => { await onProgress(); } : null);
    if (S < 0) { throw new Error('the compiled table holds no entries'); }
    this.shipped = { S: S, flags: flags, bottoms: bottoms, entries: entries, missing: missing };
    this.levels = S;
    this.recovery = S > this.maxMull ? await this.recoverTerminal() : { needed: false };
    return this.recovery;
  }

  shippedKeep(i7, m, pd) { return ((this.shipped.flags[i7] >> (m * 2 + pd)) & 1) === 1; }
  shippedBottomRank(i7, m, pd) { return this.shipped.bottoms[(i7 * (this.shipped.S + 1) + m) * 2 + pd]; }

  // Rank -> composition (the inverse of rank()).
  unrank(idx, H) {
    const comp = new Array(this.K).fill(0);
    let r = H;
    for (let b = 0; b < this.K && r > 0; b++) {
      let x = Math.min(this.cap[b], r);
      while (x > 0 && this.OFF[(b * 8 + r) * 8 + x] > idx) { x--; }
      comp[b] = x;
      idx -= this.OFF[(b * 8 + r) * 8 + x];
      r -= x;
    }
    return comp;
  }

  async recoverTerminal() {
    const M = this.maxMull, t7 = this.sizes[HAND], N7 = t7.N;
    const { packed, P } = this.handsPacked();
    const KV = [];
    for (let m = 0; m <= M; m++) { KV[m] = await this.keepValLevel(m, packed, null); }
    const GRID = 101;
    const rep = { needed: true, ok: true, D: [0, 0], spread: [null, null], bracket: [null, null] };
    const chains = [null, null];
    for (let pd = 0; pd < 2; pd++) {
      // need[m] = [lo, hi): Dopt[m+1] must be >= every kept hand's KeepVal and < every mulliganed one's.
      const need = [];
      for (let m = 0; m <= M; m++) {
        let lo = -Infinity, hi = Infinity;
        const b = KV[m].b[pd];
        for (let i = 0; i < N7; i++) {
          if (!t7.present[i]) { continue; }
          if (this.shippedKeep(i, m, pd)) { if (b[i] > lo) { lo = b[i]; } } else if (b[i] < hi) { hi = b[i]; }
        }
        need.push([lo, hi]);
      }
      const [lo, hi] = need[M];
      rep.bracket[pd] = [lo, hi];
      if (!(lo < hi) || !isFinite(lo) || !isFinite(hi)) { rep.ok = false; continue; }
      const chainAt = (D) => {
        const c = new Array(M + 2).fill(0);
        c[M + 1] = D;
        for (let m = M; m >= 0; m--) {
          const b = KV[m].b[pd], thr = c[m + 1];
          let s = 0;
          for (let i = 0; i < N7; i++) { if (t7.present[i]) { s += P[i] * Math.min(b[i], thr); } }
          c[m] = s;
        }
        return c;
      };
      const feasible = [];
      for (let g = 0; g < GRID; g++) {
        const D = lo + (hi - lo) * (g + 0.5) / GRID;
        const c = chainAt(D);
        let okAll = true;
        for (let m = 0; m < M && okAll; m++) { if (!(c[m + 1] >= need[m][0] && c[m + 1] < need[m][1])) { okAll = false; } }
        if (okAll) { feasible.push(c); }
      }
      if (!feasible.length) { rep.ok = false; continue; }
      const mid = feasible[Math.floor(feasible.length / 2)];
      chains[pd] = mid;
      rep.D[pd] = mid[M + 1];
      // How far each level's value moves across everything that still reproduces the engine.
      const step = (hi - lo) / GRID / 2;
      rep.spread[pd] = mid.map((_, k) => {
        let a = Infinity, z = -Infinity;
        for (const c of feasible) { if (c[k] < a) { a = c[k]; } if (c[k] > z) { z = c[k]; } }
        return (z - a) / 2 + (k === M + 1 ? step : 0);
      });
    }
    if (rep.ok) {
      this.dopt = [chains[0], chains[1]];
      this.doptSe = [this.doptSe[0].concat([0]), this.doptSe[1].concat([0])];
      this.doptSpread = rep.spread;
    }
    return rep;
  }

  // ---- queries --------------------------------------------------------------------------------------

  compOf(names) {
    const comp = new Array(this.K).fill(0), missing = [];
    for (const nm of names) {
      if (this.bucketOf.has(nm)) { comp[this.bucketOf.get(nm)]++; } else { missing.push(nm); }
    }
    return { comp: comp, missing: missing };
  }

  // The decision for a 7-card composition, exactly as the shipped table encodes it (test/
  // mulligan_app_check.js compares these to the compiled profile): the keep flag (KeepVal against the
  // mulligan value) and the bottoming target, whose argmin runs on the rule's score (raw mean, or the
  // shrunk score) over the cells the rule admits -- with BuildPolicyFromTables' fallback to the
  // unfiltered argmin when no candidate clears the floor. Past the raw sidecar's depth (shipped-table
  // mode only) there is nothing to compute from: it returns the engine's own decision, noStats.
  decideComp(h, m, pd, rule) {
    rule = rule || this.rule;
    if (this.shipped && m > this.maxMull) {
      const i7 = this.rank(h, HAND), r = this.shippedBottomRank(i7, m, pd);
      return { keep: this.shippedKeep(i7, m, pd), keepVal: NaN, keepArg: -1, bottomArg: r,
               bottomSub: r === 0xFFFFFFFF ? null : this.unrank(r, HAND - m), noStats: true };
    }
    const M = this.maxMull, mm = Math.min(m, M), L = this.levels === undefined ? M : this.levels;
    const ts = this.sizes[HAND - mm];
    const zs = (rule.shrink && mm >= 1) ? this.Z[HAND - mm] : ts.V;
    const floor = rule.floor;
    let best = 1e9, arg = -1;
    let bAny = 1e9, aAny = -1, subAny = null, bRef = 1e9, aRef = -1, subRef = null;
    this.forEachSub(h, HAND - mm, (j, sub) => {
      if (!ts.present[j]) { return; }
      const v = ts.V[2 * j + pd];
      if (v < best) { best = v; arg = j; }
      const z = zs[2 * j + pd];
      if (z < bAny) { bAny = z; aAny = j; subAny = sub.slice(); }
      if ((floor < 0 || ts.n[2 * j + pd] > floor) && z < bRef) { bRef = z; aRef = j; subRef = sub.slice(); }
    });
    const thr = this.dopt[pd][mm + 1];
    const keep = (mm === L) || (thr !== undefined ? best <= thr
                                                  : (this.shipped ? this.shippedKeep(this.rank(h, HAND), mm, pd) : true));
    return {
      keep: keep, keepVal: best, keepArg: arg,
      bottomArg: aRef >= 0 ? aRef : aAny,
      bottomSub: aRef >= 0 ? subRef : subAny,
    };
  }

  // Everything the page shows for one hand. names: the 7 card names, in hand order.
  evaluate(names, m, onPlay) {
    const pd = onPlay ? 1 : 0, L = this.levels === undefined ? this.maxMull : this.levels;
    const { comp, missing } = this.compOf(names);
    const res = { names: names.slice(), m: m, onPlay: !!onPlay, maxMull: L, statsLevels: this.maxMull, missing: missing, comp: comp };
    if (names.length !== HAND) { res.error = 'A hand is 7 cards (this one has ' + names.length + ').'; return res; }
    if (missing.length) { res.error = 'The keep table has no bucket for: ' + Array.from(new Set(missing)).join(', ') + '. It predates this decklist, so it cannot speak for this hand (the engine falls back to its static keep rule).'; return res; }
    if (m > L) { res.error = 'This table only covers up to ' + L + ' mulligans.'; return res; }
    const t7 = this.sizes[HAND];
    const i7 = this.rank(comp, HAND);
    if (i7 < 0 || !t7.present[i7]) { res.error = 'This hand is not in the table.'; return res; }
    const sh = !!this.shipped;
    const keepAt = (mm, side) => sh ? this.shippedKeep(i7, mm, side) : this.decideComp(comp, mm, side).keep;
    res.forced = (m === L) || (HAND - m <= 1);
    res.keep = keepAt(m, pd);
    res.handGames = { play: t7.n[2 * i7 + 1], draw: t7.n[2 * i7] };

    if (sh && m > this.maxMull) {
      // Beyond the raw sidecar: the engine's decision and bottoming, with no statistics behind them.
      res.noStats = true;
      if (m >= 1) {
        const r = this.shippedBottomRank(i7, m, pd);
        if (r !== 0xFFFFFFFF) {
          const keepSub = this.unrank(r, HAND - m), removed = comp.map((x, b) => x - keepSub[b]);
          res.bottom = { options: null, chosen: { keepSub: keepSub, removed: removed, chosen: true }, cards: this.bottomCards(names, removed) };
        }
      }
    } else {
      const d = this.decideComp(comp, m, pd);
      const ts = this.sizes[HAND - m];
      res.keepValue = { v: d.keepVal, se: this.se(ts, d.keepArg, pd), n: ts.n[2 * d.keepArg + pd] };
      if (!res.forced && this.dopt[pd][m + 1] !== undefined) {
        const mv = this.dopt[pd][m + 1], mse = this.doptSe[pd][m + 1] || 0;
        const spread = this.doptSpread ? this.doptSpread[pd][m + 1] : 0;
        res.mullValue = { v: mv, se: mse, recovered: !!this.doptSpread, spread: spread };
        res.delta = mv - d.keepVal;                       // > 0: keeping is that many turns faster
        res.conf = confidence(res.delta, Math.sqrt(res.keepValue.se ** 2 + mse ** 2 + spread * spread / 3));
      }
      // Bottoming options (m >= 1): every distinct way to put m cards back, ranked as the table ranks them.
      if (m >= 1) {
        const rule = this.rule, opts = [];
        const zs = rule.shrink ? this.Z[HAND - m] : ts.V;
        let order = 0;
        this.forEachSub(comp, HAND - m, (j, sub) => {
          if (!ts.present[j]) { return; }
          opts.push({
            idx: j, order: order++, keepSub: sub.slice(),
            removed: comp.map((x, b) => x - sub[b]),
            v: ts.V[2 * j + pd], score: zs[2 * j + pd], se: this.se(ts, j, pd), n: ts.n[2 * j + pd],
            eligible: rule.floor < 0 || ts.n[2 * j + pd] > rule.floor,
          });
        });
        opts.sort((a, b) => (a.score - b.score) || (a.order - b.order));
        const pick = sh ? this.shippedBottomRank(i7, m, pd) : d.bottomArg;
        const chosen = opts.find(o => o.idx === pick) || opts.find(o => o.idx === d.bottomArg);
        chosen.chosen = true;
        const runner = opts.find(o => o !== chosen && o.eligible) || opts.find(o => o !== chosen);
        res.bottom = { options: opts, chosen: chosen, runnerUp: runner || null, floor: rule.floor, shrink: rule.shrink };
        if (runner) {
          res.bottom.delta = runner.score - chosen.score;
          res.bottom.conf = confidence(res.bottom.delta, Math.sqrt(chosen.se ** 2 + runner.se ** 2));
        }
        res.bottom.cards = this.bottomCards(names, chosen.removed);
      }
      if (!sh && this.calibration && this.calibration.keepMismatchByM && this.calibration.keepMismatchByM[m]) {
        res.unreliable = 'At ' + m + ' mulligan' + (m === 1 ? '' : 's') + ' this raw sidecar does not reproduce the keep table the engine plays ('
          + this.calibration.keepMismatchByM[m] + ' of the sampled decisions differ), so the engine may decide this hand differently.';
      }
      if (sh && this.recovery && this.recovery.needed && !this.recovery.ok) {
        res.unreliable = 'The call is the engine\'s, but the value of mulliganing could not be recovered for this deck, so the numbers here may not explain it.';
      }
    }

    // The same 7 cards at every mulligan count, and on the other side of the coin.
    const deltaAt = (mm, side) => {
      if (mm >= L || (sh && mm > this.maxMull)) { return null; }
      const thr = this.dopt[side][mm + 1];
      return thr === undefined ? null : thr - this.decideComp(comp, mm, side).keepVal;
    };
    res.ladder = [];
    for (let mm = 0; mm <= L; mm++) { res.ladder.push({ m: mm, keep: keepAt(mm, pd), forced: mm === L, delta: deltaAt(mm, pd) }); }
    res.other = { keep: keepAt(m, 1 - pd), forced: res.forced, delta: res.forced ? null : deltaAt(m, 1 - pd) };
    return res;
  }

  // Which physical cards the engine would bottom for a bucket-level removal: walking the hand in order,
  // the first card whose bucket is still over its target (AIEngine::BottomCards). Cards that share that
  // bucket are equivalent for the table, so each pick also lists the hand's other names in its bucket.
  bottomCards(names, removed) {
    const left = removed.slice(), out = [];
    names.forEach((nm, i) => {
      const b = this.bucketOf.get(nm);
      if (left[b] > 0) {
        left[b]--;
        const alts = Array.from(new Set(names.filter(x => x !== nm && this.bucketOf.get(x) === b)));
        out.push({ name: nm, handIndex: i, bucket: b, equivalents: alts });
      }
    });
    return out;
  }

  // Members of a card's bucket other than itself (equivalent for the table), restricted to the deck.
  equivalents(name) {
    const b = this.bucketOf.get(name);
    if (b === undefined) { return []; }
    const inDeck = new Set(this.deckCards.map(c => c.name));
    return this.buckets[b].filter(x => x !== name && inDeck.has(x));
  }

  summary() {
    const m = this.meta || {};
    return {
      K: this.K, maxMull: this.maxMull, R: this.R, subTarget: this.subTarget,
      cells: this.cellsLoaded, totalGames: this.totalGames, hands7: this.sizes[HAND] ? this.sizes[HAND].N : 0,
      depth: m.depth, budgetMs: m.budget_ms, maxTurns: m.max_turns, commit: m.commit,
      deckMatch: this.deckMatch, aliasUsed: this.aliasUsed, unbucketed: this.unbucketed,
      doptPlay: this.dopt ? this.dopt[1][0] : null, doptDraw: this.dopt ? this.dopt[0][0] : null,
      rule: this.rule, ruleSource: this.ruleSource, calibration: this.calibration || null,
      levels: this.levels === undefined ? this.maxMull : this.levels, shipped: !!this.shipped, recovery: this.recovery || null,
    };
  }
}

// Deal a random 7 from a decklist ([{name,count}]); rand() in [0,1) (default Math.random).
function dealRandom(deckCards, rand) {
  rand = rand || Math.random;
  const lib = [];
  for (const c of deckCards) { for (let i = 0; i < c.count; i++) { lib.push(c.name); } }
  for (let i = 0; i < HAND && i < lib.length; i++) {
    const j = i + Math.floor(rand() * (lib.length - i));
    const t = lib[i]; lib[i] = lib[j]; lib[j] = t;
  }
  return lib.slice(0, HAND);
}

// ---- finding decks in a folder ----------------------------------------------------------------------
// Folders a deck scan never descends into (picking the repo root instead of decks/ must stay cheap).
const SKIP_DIRS = new Set(['.git', 'build', 'node_modules', 'logs', 'references', '.claude', 'tools', 'src', 'test', 'docs', 'scripts']);

// Groups a flat file list ([{path, name, ...}], '/'-separated paths) into decks. A deck is a decklist
// whose stem has a raw keep sidecar beside it; the compiled table and the play profile are optional.
// A list inside another deck's folder (decks/Angels/v1-thune4-chancery/) is an older list of that deck.
// Each entry's own objects are passed through as list / raw / compiled / play. The page and
// build_deck_list.js both use this, so the generated deck list and a live folder scan agree on keys.
function discoverDecks(entries) {
  const byDir = new Map();
  for (const e of entries) {
    const i = e.path.lastIndexOf('/');
    const dir = i < 0 ? '' : e.path.slice(0, i);
    if (!byDir.has(dir)) { byDir.set(dir, []); }
    byDir.get(dir).push(e);
  }
  const isList = (name) => /\.(cod|txt)$/i.test(name) && !/^(readme|notes?|changelog)/i.test(name);
  const decks = [];
  for (const [dir, files] of byDir) {
    const lower = new Map(files.map(f => [f.name.toLowerCase(), f]));
    for (const f of files) {
      if (!isList(f.name)) { continue; }
      const stem = f.name.replace(/\.(cod|txt)$/i, ''), s = stem.toLowerCase();
      const raw = lower.get(s + '.keepmodel.exhaustive.raw.json.gz') || lower.get(s + '.keepmodel.exhaustive.raw.json');
      if (!raw) { continue; }
      const compiled = lower.get(s + '.keepmodel.exhaustive.profile.json.gz') || lower.get(s + '.keepmodel.exhaustive.profile.json');
      const play = lower.get(s + '.profile.json');
      const parts = dir.split('/').filter(Boolean);
      const folder = parts.length ? parts[parts.length - 1] : stem;
      const parentFiles = byDir.get(dir.slice(0, Math.max(0, dir.lastIndexOf('/')))) || [];
      const older = parts.length >= 2 && (/^v\d+[-_]/i.test(folder) || parentFiles.some(p => isList(p.name)));
      const base = stem.toLowerCase() === folder.toLowerCase() ? folder : folder + ' (' + stem + ')';
      const label = older ? parts[parts.length - 2] + ' · ' + folder : base;
      decks.push({ key: (older ? parts[parts.length - 2] + '/' : '') + folder + '/' + stem, label: label, archived: older,
                   list: f, raw: raw, compiled: compiled, play: play });
    }
  }
  decks.sort((a, b) => (a.archived - b.archived) || a.label.localeCompare(b.label));
  return decks;
}

return {
  HAND: HAND, StreamJSON: StreamJSON, pumpStream: pumpStream, parseDecklist: parseDecklist,
  deckFp: deckFp, KeepTable: KeepTable, dealRandom: dealRandom, confidence: confidence, TIERS: TIERS,
  SKIP_DIRS: SKIP_DIRS, discoverDecks: discoverDecks,
};
});
