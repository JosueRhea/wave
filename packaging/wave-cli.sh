#!/bin/sh
# `wave [--line N] [--column N] [file-or-folder]` — the CLI half of Wave.app.
#
# Ships inside the bundle at Contents/Resources/bin/wave and is symlinked onto
# PATH by `make install-cli`, so step one is walking that symlink back to the
# bundle this copy belongs to. A moved/renamed .app therefore still works, and
# two installed copies never launch each other.
#
# It execs the bundled binary instead of `open -a Wave` for two reasons: the
# process inherits the shell's cwd, so `wave src/main.c` resolves the way every
# other command does, and it does not depend on the app answering Apple Events
# (GPUI implements application:openURLs: only).
#
# The editor is detached and the prompt comes straight back, like `code .`.
# Each invocation is its own process — Wave has no IPC to hand a path to an
# already-running window, so `wave .` twice gives you two windows.

set -e

self=$0
# `readlink -f` is GNU; walk the links by hand so this works on stock macOS.
while [ -L "$self" ]; do
    link=$(readlink "$self")
    case $link in
        /*) self=$link ;;
        *) self=$(dirname "$self")/$link ;;
    esac
done
dir=$(cd "$(dirname "$self")" && pwd)

# Contents/Resources/bin/wave -> Contents/MacOS/wave
bin=$dir/../../MacOS/wave
if [ ! -x "$bin" ]; then
    for app in /Applications/Wave.app "$HOME/Applications/Wave.app"; do
        if [ -x "$app/Contents/MacOS/wave" ]; then
            bin=$app/Contents/MacOS/wave
            break
        fi
    done
fi
if [ ! -x "$bin" ]; then
    echo "wave: no Wave.app found (looked beside $self, in /Applications and ~/Applications)" >&2
    exit 1
fi

# Detached, but not silently: a bad command line makes the binary print usage
# and exit 2 immediately, and swallowing that would leave `wave --typo` looking
# like it worked. stderr is parked in a file just long enough to catch that.
err=$(mktemp -t wave-cli)
nohup "$bin" "$@" >/dev/null 2>"$err" &
pid=$!
sleep 0.2

if kill -0 "$pid" 2>/dev/null; then
    # Still running: it is the editor now, and the terminal is free.
    rm -f "$err"
    exit 0
fi

wait "$pid" 2>/dev/null || status=$?
cat "$err" >&2
rm -f "$err"
exit "${status:-0}"
