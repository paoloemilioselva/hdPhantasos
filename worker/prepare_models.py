"""Download everything the worker's models need, ahead of time, and check it:
run once, so the first picture in usdview does not wait for 37 GB.

    <venv python> prepare_models.py [--cache <dir>] [--models klein4b,matrixgame]
                                    [--quick] [--check]

setup_worker.py runs it after making the venv when given --models; the
repository's prepare_phantasos.bat/.sh run that.

Interrupted runs are safe to repeat:
- a file Hugging Face was still downloading (*.incomplete) is resumed;
- every file is then checked against the Hub: its size, and its SHA-256
  (skip the hashing with --quick). A file that does not match, such as one
  cut short by a full disk or a killed copy, is deleted and downloaded again;
- Matrix-Game's code is fetched into a .partial folder and renamed only when
  complete, so a half-made checkout is thrown away and fetched again.

--check downloads nothing: it says whether every model is in place, exactly
as the worker finds them with downloads off, and exits 1 if one is not.
"""

import argparse
import fnmatch
import hashlib
import os
import shutil
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)

MODELS = ("klein4b", "matrixgame")
# Download sizes, for the free-space check before downloading.
SIZES_GB = {"klein4b": 16.0, "matrixgame": 21.5}
REPOSITORIES = {"klein4b": ["black-forest-labs--FLUX.2-klein-4B"],
                "matrixgame": ["Skywork--Matrix-Game-4.0", "alibaba-pai--Wan2.2-Fun-5B-Control"]}


def say(message):
    print(f"[prepare] {message}", flush=True)


def folder_size(path):
    total = 0
    for root, _, files in os.walk(path):
        for name in files:
            try:
                total += os.path.getsize(os.path.join(root, name))
            except OSError:
                pass
    return total


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(16 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def remove_cached(path):
    """A file in the Hugging Face cache: the snapshot entry and, where it is
    a link, the blob it points to."""
    blob = os.path.realpath(path)
    for target in {path, blob}:
        try:
            os.remove(target)
        except OSError:
            pass


def verify(repo, revision, files, quick):
    """Checks local files against the Hub's record of them at `revision`.
    `files` maps repository paths to local paths. Returns those that do not
    match."""
    from huggingface_hub import HfApi
    info = HfApi().model_info(repo, revision=revision, files_metadata=True)
    record = {sibling.rfilename: sibling for sibling in info.siblings}
    bad = []
    for name, path in sorted(files.items()):
        sibling = record.get(name)
        if sibling is None:
            continue
        size = os.path.getsize(path) if os.path.isfile(path) else -1
        if sibling.size is not None and size != sibling.size:
            say(f"  {name}: {size} bytes, expected {sibling.size}")
            bad.append(name)
            continue
        lfs = sibling.lfs  # None for small files kept in git, not LFS
        expected = getattr(lfs, "sha256", None) or (lfs.get("sha256") if isinstance(lfs, dict) else None) \
            if lfs is not None else None
        if expected and not quick:
            started = time.time()
            actual = sha256(path)
            if actual != expected:
                say(f"  {name}: SHA-256 does not match")
                bad.append(name)
            elif size > 1e8:
                say(f"  {name}: {size / 1e9:.1f} GB, SHA-256 checked in {time.time() - started:.0f} s")
    if not quick:
        hashed = sum(1 for name in files if record.get(name) is not None and record[name].lfs is not None)
        say(f"  {repo}: {len(files)} files checked, {hashed} by SHA-256, {len(bad)} to fetch again")
    return bad


def repair(repo, revision, files, quick):
    """Verify, and download again whatever does not match (twice at most)."""
    from huggingface_hub import hf_hub_download
    checking = dict(files)
    for attempt in range(2):
        bad = verify(repo, revision, checking, quick)
        if not bad:
            return True
        for name in bad:
            say(f"downloading {name} again")
            remove_cached(files[name])
            files[name] = hf_hub_download(repo, name, revision=revision, force_download=True)
        # Only what was fetched again needs checking again.
        checking = {name: files[name] for name in bad}
    return not verify(repo, revision, checking, quick)


def prepare_klein(check, quick):
    from phantasos_worker.backends import Klein
    snapshot = Klein.fetch(not check, say)
    if check:
        return True
    revision = os.path.basename(snapshot)  # the snapshot folder is named by commit
    from huggingface_hub import HfApi
    info = HfApi().model_info(Klein.repo, revision=revision)
    names = [s.rfilename for s in info.siblings
             if any(fnmatch.fnmatch(s.rfilename, pattern) for pattern in Klein.patterns)]
    files = {name: os.path.join(snapshot, *name.split("/")) for name in names}
    return repair(Klein.repo, revision, files, quick)


def prepare_matrix_game(cache, check, quick):
    from phantasos_worker import matrix_game
    matrix_game.fetch_source(cache, not check, say)
    paths = matrix_game.fetch_weights(not check, say)
    if check:
        return True
    ok = True
    by_repository = {}
    for key, ((repo, revision), name) in matrix_game.WEIGHTS.items():
        by_repository.setdefault((repo, revision), {})[name] = paths[key]
    for (repo, revision), files in by_repository.items():
        ok = repair(repo, revision, files, quick) and ok
    return ok


def main():
    from setup_worker import default_cache
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cache", default=default_cache())
    parser.add_argument("--models", default="all", help="klein4b, matrixgame, or all (comma separated)")
    parser.add_argument("--quick", action="store_true", help="check sizes only, not SHA-256")
    parser.add_argument("--check", action="store_true", help="download nothing; exit 1 if anything is missing")
    args = parser.parse_args()
    models = list(MODELS) if args.models == "all" else [m.strip() for m in args.models.split(",") if m.strip()]
    unknown = [m for m in models if m not in MODELS]
    if unknown:
        parser.error(f"unknown models {unknown}; known: {', '.join(MODELS)}")

    os.environ["HF_HOME"] = os.path.join(args.cache, "hf")
    os.environ["HDPHANTASOS_CACHE"] = args.cache
    os.environ.pop("HF_HUB_DISABLE_PROGRESS_BARS", None)  # progress bars are the point here
    # Windows without Developer Mode has no symlinks: the cache copies
    # instead, which works; the warning would bury the progress.
    os.environ.setdefault("HF_HUB_DISABLE_SYMLINKS_WARNING", "1")
    if args.check:
        os.environ["HF_HUB_OFFLINE"] = "1"
    os.makedirs(args.cache, exist_ok=True)

    if not args.check:
        hub = os.path.join(args.cache, "hf", "hub")
        needed = sum(max(0.0, SIZES_GB[m] - sum(folder_size(os.path.join(hub, "models--" + r))
                                                 for r in REPOSITORIES[m]) / 1e9) for m in models)
        free = shutil.disk_usage(args.cache).free / 1e9
        say(f"cache: {args.cache} ({free:.0f} GB free, about {needed:.0f} GB still to download)")
        if free < needed + 2:
            say(f"not enough space: set HDPHANTASOS_CACHE to a disk with {needed + 2:.0f} GB free")
            return 1

    failed = []
    for model in models:
        say(f"--- {model}")
        started = time.time()
        try:
            ok = prepare_klein(args.check, args.quick) if model == "klein4b" else \
                prepare_matrix_game(args.cache, args.check, args.quick)
        except Exception as error:  # noqa: BLE001 - one model failing does not stop the other
            say(f"{model}: {error}")
            ok = False
        say(f"{model}: {'ready' if ok else 'NOT READY'} ({time.time() - started:.0f} s)")
        if not ok:
            failed.append(model)
    if failed:
        say(f"not ready: {', '.join(failed)}. Run again to resume.")
        return 1
    say(f"everything is in place in {args.cache}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
