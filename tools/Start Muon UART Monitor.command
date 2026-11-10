#!/bin/zsh

SCRIPT_DIR="${0:A:h}"
cd "$SCRIPT_DIR" || exit 1

if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is required. Install it from https://www.python.org/downloads/macos/"
    echo
    read "?Press Return to close."
    exit 1
fi

echo "Starting the gLOWCOST wired flight monitor..."
echo "Keep this Terminal window open while using the dashboard."
echo
python3 "$SCRIPT_DIR/uart_web_monitor.py" --open-browser
status=$?

if (( status != 0 )); then
    echo
    echo "The monitor stopped with an error. Check that the USB-UART cable is connected"
    echo "and that screen, miniterm, or another serial program is not using the port."
    read "?Press Return to close."
fi

exit $status
