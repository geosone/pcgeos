#!/usr/bin/env python3
"""geostree.py -- copy a GEOS tree between the two host layouts.

Every GEOS file starts with the 256-byte GeosFileHeader of DOS16 GEOS on
every host (Port32/hostfs), so a tree differs between hosts only in its
host names:

  dos   GEOS files and directories whose GEOS name is no DOS name have
        generated 8.3 names (first eight characters made legal, .000,
        .001 ...); such a directory has a @DIRNAME.000 file with its GEOS
        name (DOS16 GEOS, dos32);
  long  the GEOS name is the host name (Linux and the other hosts).

The kernel reads either layout on every host (it finds GEOS files by the
names in their headers), so this is needed only to put a tree on a DOS
disk (todos) or to give the files of a DOS tree their long names on the
host (tolong):

  geostree.py todos [--lfn] SRC DST   long (or mixed) -> dos
  geostree.py tolong SRC DST         dos (or mixed)  -> long
  geostree.py selftest

DST must not exist.  Native files (no header) keep their names; for todos
they must be DOS names already.  --lfn is for a DOS with long file names
(Windows 9x, DOSLFN; the MSLF driver of DOS16 GEOS): GEOS files still get
8.3 names, but native files and directories may keep long DOS names.
Dates and attributes are kept.
"""
import os
import shutil
import sys
import tempfile

GFH_SIZE = 256
SIGNATURE = bytes([ord('G') | 0x80, ord('E'), ord('A') | 0x80, ord('S')])
NAME_OFF, NAME_SIZE = 4, 36
TYPE_OFF = 40
GFT_DIRECTORY = 4
DIRNAME = "@DIRNAME.000"
BAD_DOS = set('"*+,./:;<=>?[\\]|')
BAD_DOS_LONG = set('"*/:<>?\\|')


class TreeError(Exception):
    pass


def read_header(path):
    try:
        with open(path, "rb") as f:
            hdr = f.read(GFH_SIZE)
    except OSError:
        return None
    if len(hdr) == GFH_SIZE and hdr[:4] == SIGNATURE:
        return hdr
    return None


def geos_name(hdr):
    raw = hdr[NAME_OFF:NAME_OFF + NAME_SIZE].split(b"\0", 1)[0]
    return raw.decode("latin-1")


def dir_header(name):
    hdr = bytearray(GFH_SIZE)
    hdr[:4] = SIGNATURE
    enc = name.encode("latin-1")[:NAME_SIZE - 1]
    hdr[NAME_OFF:NAME_OFF + len(enc)] = enc
    hdr[TYPE_OFF:TYPE_OFF + 2] = GFT_DIRECTORY.to_bytes(2, "little")
    hdr[52:54] = (1).to_bytes(2, "little")        # protocol 1.0
    return bytes(hdr)


def dos_form(name):
    """the 8.3 form of a DOS name (upper case), else None"""
    base, dot, ext = name.partition(".")
    if not base or len(base) > 8 or len(ext) > 3 or (dot and not ext) \
            or "." in ext:
        return None
    for c in base + ext:
        if ord(c) <= 32 or ord(c) >= 0x7f or c in BAD_DOS:
            return None
    return name.upper()


def dos_long_form(name):
    """a long DOS name as it is, else None"""
    if not name or len(name) > 32 or name[-1] in ". ":
        return None
    for c in name:
        if ord(c) < 32 or c in BAD_DOS_LONG:
            return None
    return name


def generate(name, taken):
    """DOSVirtGenerateDosName"""
    base = "".join("_" if (ord(c) <= 32 or ord(c) >= 0x7f or c in BAD_DOS)
                   else c.upper() for c in name[:8])
    for n in range(1000):
        cand = "%s.%03d" % (base, n)
        if cand not in taken:
            return cand
    raise TreeError("no DOS name left for %r" % name)


def entries(src):
    """(host name, GEOS name, is dir, is GEOS) of a directory"""
    out = []
    for e in sorted(os.listdir(src)):
        if e.upper() == DIRNAME:
            continue
        p = os.path.join(src, e)
        if os.path.isdir(p):
            hdr = read_header(os.path.join(p, DIRNAME))
            if hdr is None:
                for d in os.listdir(p):
                    if d.upper() == DIRNAME:
                        hdr = read_header(os.path.join(p, d))
            out.append((e, geos_name(hdr) if hdr else e, True, hdr is not None))
        else:
            hdr = read_header(p)
            out.append((e, geos_name(hdr) if hdr else e, False,
                        hdr is not None))
    return out


def extra_dirname(src):
    """a @DIRNAME.000 holding more than its header (links) is kept"""
    for d in os.listdir(src):
        if d.upper() == DIRNAME:
            p = os.path.join(src, d)
            if os.path.getsize(p) > GFH_SIZE:
                return p
    return None


def copy_todos(src, dst, report, lfn=False):
    os.mkdir(dst)
    taken = set()
    plan = []
    # names that stay first, so they keep them: native files, and
    # directories without @DIRNAME.000 named as on DOS
    for host, name, isdir, geos in entries(src):
        form = dos_form(host) or (lfn and dos_long_form(host)) or None
        if isdir and not geos and form and form.upper() == host.upper():
            plan.append((host, name, isdir, geos, form))
            taken.add(form.upper())
        elif not isdir and not geos:
            if not form:
                raise TreeError("native file %s has no DOS name"
                                % os.path.join(src, host))
            if form.upper() in taken:
                raise TreeError("two files named %s in %s" % (form, src))
            plan.append((host, name, isdir, geos, form))
            taken.add(form.upper())
        else:
            plan.append((host, name, isdir, geos, None))
    for i, (host, name, isdir, geos, form) in enumerate(plan):
        if form is None:
            if isdir and (dos_form(name) == name or
                          (lfn and dos_long_form(name) == name and
                           name.upper() not in taken)):
                form = name     # a DOS name exactly: no @DIRNAME.000
            else:
                form = generate(name, taken)
            taken.add(form.upper())
            plan[i] = (host, name, isdir, geos, form)
    for host, name, isdir, geos, form in plan:
        s = os.path.join(src, host)
        d = os.path.join(dst, form)
        if isdir:
            copy_todos(s, d, report, lfn)
            keep = extra_dirname(s)
            if keep:
                shutil.copy2(keep, os.path.join(d, DIRNAME))
            elif form != name:
                with open(os.path.join(d, DIRNAME), "wb") as f:
                    f.write(dir_header(name))
            shutil.copystat(s, d)
        else:
            shutil.copy2(s, d)
        if host != form:
            report("%s -> %s" % (os.path.join(src, host), d))


def copy_tolong(src, dst, report):
    os.mkdir(dst)
    seen = {}
    for host, name, isdir, geos in entries(src):
        if "/" in name or name in (".", "..") or not name:
            raise TreeError("GEOS name %r of %s cannot be a host name"
                            % (name, os.path.join(src, host)))
        if name in seen:
            raise TreeError("%s and %s are both %r in %s"
                            % (seen[name], host, name, src))
        seen[name] = host
        s = os.path.join(src, host)
        d = os.path.join(dst, name)
        if isdir:
            copy_tolong(s, d, report)
            keep = extra_dirname(s)
            if keep:
                shutil.copy2(keep, os.path.join(d, DIRNAME))
            shutil.copystat(s, d)
        else:
            shutil.copy2(s, d)
        if host != name:
            report("%s -> %s" % (s, d))


def selftest():
    def geos_file(path, name, data=b"data"):
        hdr = bytearray(dir_header(name))
        hdr[TYPE_OFF:TYPE_OFF + 2] = (3).to_bytes(2, "little")
        with open(path, "wb") as f:
            f.write(bytes(hdr) + data)

    with tempfile.TemporaryDirectory() as tmp:
        a = os.path.join(tmp, "a")
        os.makedirs(os.path.join(a, "My Folder", "Inner Dir"))
        os.makedirs(os.path.join(a, "PLAIN"))
        geos_file(os.path.join(a, "Long Document Name"), "Long Document Name")
        geos_file(os.path.join(a, "Long Document 2"), "Long Document 2", b"2")
        geos_file(os.path.join(a, "My Folder", "x.vm"), "x.vm")
        with open(os.path.join(a, "NATIVE.TXT"), "wb") as f:
            f.write(b"native")
        with open(os.path.join(a, "PLAIN", "README"), "wb") as f:
            f.write(b"r")
        d = os.path.join(tmp, "d")
        copy_todos(a, d, lambda m: None)
        expect = {"LONG_DOC.000", "LONG_DOC.001", "MY_FOLDE.000",
                  "NATIVE.TXT", "PLAIN"}
        assert set(os.listdir(d)) == expect, os.listdir(d)
        assert set(os.listdir(os.path.join(d, "MY_FOLDE.000"))) == \
            {DIRNAME, "INNER_DI.000", "X_VM.000"}
        assert geos_name(read_header(os.path.join(d, "MY_FOLDE.000",
                                                  DIRNAME))) == "My Folder"
        assert not os.path.exists(os.path.join(d, "PLAIN", DIRNAME))
        b = os.path.join(tmp, "b")
        copy_tolong(d, b, lambda m: None)
        for root, dirs, files in os.walk(a):
            rel = os.path.relpath(root, a)
            other = os.path.join(b, rel)
            assert set(os.listdir(other)) == set(dirs) | set(files), \
                (rel, os.listdir(other))
            for fn in files:
                with open(os.path.join(root, fn), "rb") as x, \
                        open(os.path.join(other, fn), "rb") as y:
                    assert x.read() == y.read(), fn
        # a native file without a DOS name cannot go to DOS ...
        with open(os.path.join(a, "long native.txt"), "wb") as f:
            f.write(b"n")
        try:
            copy_todos(a, os.path.join(tmp, "e"), lambda m: None)
            raise AssertionError("long native name taken")
        except TreeError:
            pass
        # ... but to a DOS with long file names, as directories do
        lf = os.path.join(tmp, "l")
        copy_todos(a, lf, lambda m: None, lfn=True)
        assert set(os.listdir(lf)) == {
            "LONG_DOC.000", "LONG_DOC.001", "My Folder", "NATIVE.TXT",
            "PLAIN", "long native.txt"}, os.listdir(lf)
        assert set(os.listdir(os.path.join(lf, "My Folder"))) == \
            {"Inner Dir", "X_VM.000"}
        b2 = os.path.join(tmp, "b2")
        copy_tolong(lf, b2, lambda m: None)
        assert set(os.listdir(b2)) == set(os.listdir(a))
    print("ok")


def main(argv):
    if len(argv) == 2 and argv[1] == "selftest":
        selftest()
        return 0
    lfn = "--lfn" in argv[2:3]
    if lfn:
        argv = argv[:2] + argv[3:]
    if len(argv) != 4 or argv[1] not in ("todos", "tolong") or \
            (lfn and argv[1] != "todos"):
        sys.stderr.write(__doc__)
        return 2
    src, dst = argv[2], argv[3]
    if os.path.exists(dst):
        sys.stderr.write("geostree: %s exists\n" % dst)
        return 2
    try:
        if argv[1] == "todos":
            copy_todos(src, dst, print, lfn)
        else:
            copy_tolong(src, dst, print)
    except TreeError as e:
        if os.path.exists(dst):
            shutil.rmtree(dst)
        sys.stderr.write("geostree: %s\n" % e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
