# Articles: feeding the shelf

The Articles shelf reads any OPDS feed that offers direct EPUB downloads. This page
describes what the reader expects from a feed, and one way to build a feed from pages you
read on a phone.

## What the reader does with an article feed

On **Sync articles** the reader, for each OPDS server marked as an article feed:

1. Fetches the server URL as one OPDS feed, with the server's username and password or
   custom headers. The URL must be the feed that lists the EPUBs. A catalog root that only
   links to other feeds gives nothing to download. Feeds that need an interactive login are
   not supported here.
2. Takes the entries that have a direct EPUB acquisition link. Purchases, links to a
   publication document, and protected (LCP) books are skipped.
3. Skips entries whose `id` it has downloaded before. An entry without an `id` is known by
   its download link. The history is kept in `/.crosspoint/articles.txt` and holds the last
   300 downloads, so an article you deleted does not come back while it is in that history.
4. Downloads the new entries into `/Articles`, oldest first, named `Title - Author.epub`.
   Names are cut to 100 bytes; a second entry with the same name gets ` (2)` before
   `.epub`. An entry that fails to download, or that is not an EPUB, is tried again on the
   next sync. Three failures in a row end the sync early.

The shelf shows the entry's author as the second line of each row. A feed that puts the
source site there (for example `example.com`) gives a shelf that reads well.

Only the first page of the feed is read, up to about 60 entries. A feed should list its
newest entries first.

## A minimal feed

An OPDS 1.2 acquisition feed is an Atom document. This is enough:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <id>urn:example:read-later</id>
  <title>Read Later</title>
  <updated>2026-01-01T00:00:00Z</updated>
  <entry>
    <title>An article title</title>
    <id>urn:uuid:6f1c2d0e-0000-4000-8000-000000000001</id>
    <updated>2026-01-01T00:00:00Z</updated>
    <author><name>example.com</name></author>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip"
          href="/books/6f1c2d0e.epub"/>
  </entry>
</feed>
```

Each entry needs a stable `id`: the reader uses it to remember what it already has.

## Capturing articles from a phone

Sites with a login or a paywall usually cannot be fetched by a server, because the server
is not signed in as you. A page that is already open in your phone's browser has no such
problem: the full text is there. The approach that works is to send that text, not the URL:

1. On the phone, a share-sheet shortcut reads the article from the browser's reader view
   and posts its HTML to a small service you run.
2. The service wraps the HTML in an EPUB and adds it to an OPDS feed.
3. The reader syncs that feed.

The service can run on anything that is always on: a Raspberry Pi, a NAS, or a small VPS.
The reader has to be able to reach it over Wi-Fi when you sync, so a service on your home
network syncs at home.

Other tools that publish OPDS feeds can be used too, as long as you give the reader the
URL of a feed that lists the EPUBs directly. This has only been tested with the kind of
small service described above.
