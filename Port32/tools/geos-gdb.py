# geos-gdb.py -- gdb sees the geodes GEOS loads.
#
#   (gdb) source Port32/tools/geos-gdb.py      (or run Port32/tools/geos-gdb.sh)
#
# Geodes are loaded by GHAL's own loader (Ghal/Elf/ghal_module_elf.c),
# not by the host's, so gdb does not hear of them.  The loader calls
# ghal_module_debug_loaded(name, base) for each one (and
# ghal_module_debug_unloaded(base)): this script stops there, finds the
# geode's debug file (<name>.debug beside the .geo in the build tree, see
# cmake/GeosGeode.cmake) and hands it to gdb at the load address.
#
# The build tree is the directory with CMakeCache.txt above the program,
# or $GEOS_BUILD_DIR.  "geodes" lists what is loaded.
import os

import gdb

_debug_files = None
_loaded = {}                    # base -> (name, debug file or None)


def _build_dir():
    d = os.environ.get('GEOS_BUILD_DIR')
    if d:
        return d
    prog = gdb.current_progspace().filename
    d = os.path.dirname(os.path.abspath(prog)) if prog else os.getcwd()
    while d != '/':
        if os.path.exists(os.path.join(d, 'CMakeCache.txt')):
            return d
        d = os.path.dirname(d)
    return None


def _find(name):
    global _debug_files
    if _debug_files is None:
        _debug_files = {}
        top = _build_dir()
        if top:
            for d, _, files in os.walk(top):
                for f in files:
                    if f.endswith('.geo.debug'):
                        # the newest, should an old build have left one
                        path = os.path.join(d, f)
                        old = _debug_files.get(f[:-len('.debug')])
                        if not old or (os.path.getmtime(path) >
                                       os.path.getmtime(old)):
                            _debug_files[f[:-len('.debug')]] = path
    return _debug_files.get(name)


class _Loaded(gdb.Breakpoint):
    def stop(self):
        frame = gdb.selected_frame()
        name = frame.read_var('name').string()
        base = int(frame.read_var('base'))
        path = _find(name)
        _loaded[base] = (name, path)
        if path:
            gdb.execute('add-symbol-file %s -o %#x' % (path, base),
                        to_string=True)
        return False


class _Unloaded(gdb.Breakpoint):
    def stop(self):
        base = int(gdb.selected_frame().read_var('base'))
        name, path = _loaded.pop(base, (None, None))
        if path:
            try:
                gdb.execute('remove-symbol-file %s' % path, to_string=True)
            except gdb.error:
                pass
        return False


class _Geodes(gdb.Command):
    """List the geodes GEOS has loaded, with their load addresses."""

    def __init__(self):
        super().__init__('geodes', gdb.COMMAND_STATUS)

    def invoke(self, arg, from_tty):
        for base in sorted(_loaded):
            name, path = _loaded[base]
            print('%#018x  %-16s %s' % (base, name or '(no name)',
                                        path or '(no debug file)'))


gdb.execute('set confirm off')
gdb.execute('set breakpoint pending on')
_Loaded('ghal_module_debug_loaded', internal=True)
_Unloaded('ghal_module_debug_unloaded', internal=True)
_Geodes()
