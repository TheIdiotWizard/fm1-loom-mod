#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the LOOM-8 site for GitHub Pages (https://charlesvestal.github.io/fm1-loom/):

  index.html                    what LOOM-8 is, and the ways in: try, install, download, manual, source
  manual/index.html             docs/MANUAL.md as a page (tools/make_manual.py), its screenshots in img/
  img/                          screenshots (docs/img)
  emu/                          LOOM-8 in the browser (app/build/emu, from ./build.sh --web)
  words/                        the screen reader's words files (app/web/words, from app/words.tsv)
  install/index.html            the web installer (fm1-base's web/installer.html, with fm1pkg.js, fm1ota.js and
                                the package's metadata inlined; Chrome or Edge, Web MIDI)
  backup/index.html             backup and restore in the browser (fm1-base's web/backup/index.html: the projects and
                                the user presets)
  firmware/loom-VERSION.fwsc    the package the installer writes; also the download

  tools/make_pages.py app/build/loom-0.1.fwsc 0.1 OUT_DIR

The package must be a release build (./build.sh --release X.Y): its identity, FM-1_10XXYYZZ, is what
the installer checks the download against and what the FM-1 reports after the install."""
import html
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "base" / "tools"))
import make_site as base_site  # noqa: E402  (fm1-base's: the installer, the backup page, the package format)
from make_site import product_of, strip_module  # noqa: E402

REPO = "https://github.com/charlesvestal/fm1-loom"
TRACKS = ["#ff5caa", "#28ccc0", "#f4c83c", "#6898ff", "#ff8840", "#6ed86e", "#b288ff", "#d6b088"]

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LOOM-8</title>
<meta name="description" content="A pocket groovebox for the M-VAVE FM-1: eight tracks, each in its own colour, a 64-step sequencer with tricks and locks, weave, throws, a conductor and a song.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:wght@400;600&family=Barlow+Semi+Condensed:wght@600;700&display=swap">
<style>
:root { --ground: #f4f3f1; --ink: #1c1c20; --muted: #66666e; --rule: #dcdbd8; --card: #ffffff; --accent: #c8306f; color-scheme: light; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --ground: #000; --ink: #ebebee; --muted: #9a9aa2; --rule: #2e2e34; --card: #141418; --accent: #ff5caa; color-scheme: dark; }
}
:root[data-theme="dark"] { --ground: #000; --ink: #ebebee; --muted: #9a9aa2; --rule: #2e2e34; --card: #141418; --accent: #ff5caa; color-scheme: dark; }
body { background: var(--ground); color: var(--ink); margin: 0; padding-inline: 16px; padding-block: 48px 64px;
       font: 400 17px/1.6 "Barlow", "Helvetica Neue", Arial, sans-serif; }
main { max-width: 42rem; margin: 0 auto; }
h1 { font: 700 3rem/1 "Barlow Semi Condensed", "Barlow", sans-serif; margin: 0 0 .7rem; }
h2 { font: 700 1.4rem "Barlow Semi Condensed", "Barlow", sans-serif; margin: 2.2rem 0 .6rem; }
.threads { display: flex; gap: 6px; margin: 0 0 1.4rem; }
.threads span { flex: 1; height: 6px; border-radius: 3px; }
.lede { font-size: 1.15rem; margin: 0 0 1.6rem; max-width: 36rem; }
.shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin: 0 0 1.8rem; }
.shots img { width: 100%; height: auto; border-radius: 14px; image-rendering: pixelated; border: 1px solid var(--rule); }
.status { border: 1px solid var(--rule); background: var(--card); border-radius: 14px; padding: 12px 16px; margin: 0 0 1.6rem; }
.ways { display: grid; gap: 12px; margin: 0 0 1rem; }
.ways a { display: block; text-decoration: none; color: var(--ink); background: var(--card); border: 1px solid var(--rule);
          border-radius: 14px; padding: 14px 18px; }
.ways a:hover, .ways a:focus-visible { border-color: var(--accent); outline: none; }
.ways strong { font: 600 1.25rem "Barlow Semi Condensed", "Barlow", sans-serif; color: var(--accent); display: block; }
.ways span { color: var(--muted); font-size: .95rem; }
ul.feat { padding-left: 1.2rem; }
ul.feat li { margin: 0 0 .5rem; }
p.small { color: var(--muted); font-size: .9rem; }
a { color: var(--accent); }
code { font-size: .9em; }
</style>
</head>
<body>
<main>
<h1>LOOM-8</h1>
<div class="threads" aria-hidden="true">__THREADS__</div>
<p class="lede">A pocket groovebox for the M-VAVE FM-1. Eight tracks, each in its own colour, on eight synth
engines and a drum kit; a 64-step sequencer with tricks and parameter locks; and weave, which crosses two
patterns into a third.</p>
<div class="shots">
  <img src="img/screen-beat-playing.png" width="240" height="240" alt="The Steps page on T1, the kit, playing: kick and snare in pink, the playhead lit">
  <img src="img/screen-arrange.png" width="240" height="240" alt="Arrange: every track's pattern in its own colour, the scene number large at the right">
  <img src="img/screen-weave.png" width="240" height="240" alt="The Steps list with WEAVE WITH COPIED picked, its way set to rhythm">
</div>
<div class="status"><strong>Version __VERSION__.</strong> It installs and
uninstalls the way every fm1-base firmware does, and the installer can put M-VAVE's own firmware back. Installing is at your own risk.</div>
<nav class="ways" aria-label="Get LOOM-8">
  <a href="emu/"><strong>Try it in the browser</strong><span>The same code the FM-1 runs, with sound. No FM-1 needed.</span></a>
  <a href="manual/"><strong>Read the manual</strong><span>A ten-minute quick start, every page and gesture, with screenshots.</span></a>
  <a href="install/"><strong>Install</strong><span>From Chrome or Edge, with the FM-1 connected by USB. Nothing to install on the computer.</span></a>
  <a href="backup/"><strong>Backup</strong><span>Save your projects and presets to a file, and load them back later.</span></a>
  <a href="firmware/__PKG__"><strong>Download __PKG__</strong><span>For the command-line installer: <code>python3 base/tools/fm1_install.py __PKG__</code></span></a>
  <a href="__REPO__"><strong>Source</strong><span>GitHub, GPL-3.0. Built on fm1-base and felucca-bsp, from Felucca by Leo Kuroshita (Hügelton Instruments).</span></a>
</nav>

<h2>What's in it</h2>
<ul class="feat">
  <li><b>Eight tracks</b>, each with eight patterns of up to 64 steps: weft, stack, wavetable, orbit, tines, organ, sync and swarm synths, or the kit, eleven drum voices on the black keys.</li>
  <li><b>Tricks</b> on any step: pulse, hold, multiply, ramps, bends, jumps, skips and more; <b>locks</b> for any knob on any step.</li>
  <li><b>Weave</b> a pattern with a copied one: bars or steps in turn, or one's rhythm playing the other's notes.</li>
  <li><b>Players</b>: arp, strum (one key plays a recorded chord) and hold.</li>
  <li><b>The conductor</b> moves the tracks that follow it through a key, a scale and a progression.</li>
  <li><b>Throws</b>: twelve momentary effects a hand, held from the keys, and recorded if you like.</li>
  <li>Two <b>send effects</b> (the reverb with the PlayStation's own reverbs among its modes), a mixer with EQ, a <b>tape</b> and a compressor on the master, sixteen <b>scenes</b> and a <b>song</b>.</li>
  <li>A <b>screen reader</b>: the FM-1 says its screen through <a href="https://charlesvestal.github.io/fm1-reader/">FM-1 Screen Reader</a>.</li>
</ul>
<p class="small">To go back to the official firmware, use the installer's "Back to the stock firmware", or
M-VAVE's updater, M-UPGRADE, from <a href="https://www.m-vave.com/download">m-vave.com/download</a>. LOOM-8's
way of working owes a great deal to teenage engineering's OP-XY. M-VAVE, FM-1 and OP-XY are trademarks of their
owners; LOOM-8 is not affiliated with any of them.</p>
</main>
</body>
</html>
"""


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_10\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"loom-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "firmware", "backup"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    page = (SRC / "base" / "web" / "installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "base" / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "base" / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"base/web/installer.html must contain {mark} once")
    conf = base_site.read_conf(SRC / "app" / "fm1.toml")
    credits = conf.get("credits", "").replace("Built on fm1-base and felucca-bsp, from Felucca by Leo Kuroshita (Hügelton "
                                              "Instruments). ", "")        # (the page says that part itself)
    for key, val in (("NAME", "LOOM-8"), ("REPO", REPO), ("REPO_SHORT", REPO.replace("https://", "")),
                     ("LICENSE", conf.get("licence", "GPL-3.0-only")), ("CREDITS", " " + credits if credits else "")):
        page = page.replace("{{" + key + "}}", val)
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    tools = base_site.tool_pages(SRC / "app", conf, {"NAME": "LOOM-8"})
    (out / "backup" / "index.html").write_text(tools["backup/index.html"], encoding="utf-8")
    shutil.copytree(SRC / "docs" / "img", out / "img")
    threads = "".join(f'<span style="background:{c}"></span>' for c in TRACKS)
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name)
                                    .replace("__REPO__", REPO).replace("__THREADS__", threads), encoding="utf-8")
    (out / "manual").mkdir()
    subprocess.run([sys.executable, str(SRC / "tools" / "make_manual.py"), str(SRC / "docs" / "MANUAL.md"),
                    str(out / "manual" / "index.html")], check=True)
    for f in (out / "manual" / "index.html",):    # the manual's pictures are the site's img/
        f.write_text(f.read_text(encoding="utf-8").replace('src="img/', 'src="../img/'), encoding="utf-8")
    emu = SRC / "app" / "build" / "emu"
    if not (emu / "app.wasm").exists():
        raise SystemExit("no app/build/emu/app.wasm: run ./build.sh --web (needs Emscripten)")
    shutil.copytree(emu, out / "emu")
    # the screen reader's words files (fm1-reader): every one ever made, for the devices that point at them
    shutil.copytree(SRC / "app" / "web" / "words", out / "words")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, manual/, install/ ({product}), backup/, emu/, words/, firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
