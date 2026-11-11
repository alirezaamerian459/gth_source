# ADR-0004: Share the JB catalog through FTP and the LAN proxy

## Status

Accepted for the current JB/FTP integration.

## Context

The JB page discovers jailbreak-style game folders (`hacked` and `p30day`) on local storage, but those results were previously visible only in the desktop UI. The FTP host exposes one physical shared folder, while the LAN Console Proxy serves files from the normal indexed library. A console therefore could not browse or receive files from the JB catalog through either existing path.

## Decision

Create one application-level read-only JB catalog and use it in both paths:

- the hosted FTP server adds an explicit `/JB Library` virtual subtree when the FTP tab's **Include JB Library (read-only)** option is enabled;
- the LAN Console Proxy consults the catalog after the existing indexed-library lookup and serves exact matching JB package/archive filenames for plain HTTP requests;
- the configured FTP shared folder, normal uploads/downloads, normal library lookup, and origin fallback remain unchanged;
- FTP writes into the virtual JB subtree are rejected, and the catalog never moves, modifies, or deletes game files.

The catalog is application-owned and injected through `ServiceContainer`; the core FTP server receives only sanitized virtual-to-physical mappings, preserving module boundaries.

## Consequences

- FTP users can browse/download JB games while keeping the existing shared folder available.
- Console proxy clients can receive matching JB files without manual FTP browsing.
- The first FTP share start or first matching proxy request may perform a JB scan; subsequent lookups use the cached catalog with periodic refresh.
- HTTPS requests remain opaque to the proxy and continue through the existing tunnel behavior.
