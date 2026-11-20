#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install or play Kirby from a third-party launcher without host Python in releases."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

from installer.external_launch import MARKER, NotSetUp, play, prepare, selection
from installer.setup_layout import Bundle

# The bridge's own exit codes, apart from the game's (0 to 2) and signals (128 and up), so a
# launcher can tell the player what to do instead of reporting a crash.
NOT_SET_UP = 20
BRIDGE_FAILED = 21


def find_bundle(appdir=None):
    if appdir or getattr(sys, 'frozen', False):
        parents = Path(sys.executable).resolve().parents
        base = Path(appdir).resolve() if appdir else next(
            (path for path in (*parents[:1], *parents[2:3]) if (path/'project').is_dir()), None)
        if base is None:
            raise ValueError('Keep Kirby Bridge with its complete application folder')
        if os.environ.get('APPIMAGE'):
            command = (str(Path(os.environ['APPIMAGE']).absolute()),)
        else:
            gui = base/('Kirby-Setup.exe' if os.name == 'nt' else 'tools/setup/kirby-setup')
            command = (str(gui), '--appdir', str(base))
        return Bundle(base/'project', base/'tools/runtime', base/'host-libs',
                      (base/'revision').read_text().strip(), command, packaged=True)
    project = Path(__file__).resolve().parents[1]
    revision = subprocess.check_output(['git', '-C', str(project), 'rev-parse', 'HEAD'], text=True).strip()
    return Bundle(project, project/'tools/runtime', project/'local/host-libs', revision,
                  (sys.executable, str(project/'tools/setup_gui.py')))


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--prepare', action='store_true', help='Build and verify without opening a window')
    action.add_argument('--play', action='store_true', help='Play the selected verified installation directly')
    parser.add_argument('--external-launcher', action='store_true', help='Play, or open setup on first use')
    parser.add_argument('--bridge-help', action='help', help='Show bridge options')
    parser.add_argument('--appdir', type=Path)
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--game', type=Path, help='Owned USA RK5E01 Rev 0 disc image or complete DATA folder')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--fullscreen', action='store_true')
    mode.add_argument('--windowed', action='store_true')
    args = parser.parse_args(arguments)
    if args.prepare and (args.game is None or args.destination is None):
        parser.error('--prepare requires --game and --destination')
    try:
        bundle = find_bundle(args.appdir)
        base = Path(os.environ['APPIMAGE']).absolute().parent if os.environ.get('APPIMAGE') else bundle.project.parent
        destination = (args.destination or base/'Game').expanduser().absolute()
        if args.prepare:
            prepare(bundle, destination, args.game)
            return 0
        extra = ['--fullscreen'] if args.fullscreen else ['--windowed'] if args.windowed else []
        if args.play:
            return play(destination, extra)
        if (destination/MARKER).is_file():
            root,_,_=selection(destination)
            if root.name == bundle.revision[:12]:
                return play(destination, extra)
        return subprocess.call([*bundle.command, '--destination', str(destination), '--managed'])
    except NotSetUp as error:
        print('Kirby Bridge: '+str(error), file=sys.stderr)
        return NOT_SET_UP
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print('Kirby Bridge: '+str(error), file=sys.stderr)
        return BRIDGE_FAILED


if __name__ == '__main__':
    sys.exit(main())
