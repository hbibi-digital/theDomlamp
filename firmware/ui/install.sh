#!/bin/sh
# Puts the lamp's own look on a flashed lamp: colours (skin.css), four presets, and its names.
#   ./install.sh <address of the lamp>          e.g. ./install.sh 192.168.1.50
# Overwrites the presets on the lamp. The names only matter on a lamp flashed before the build carried them.
set -eu
host=${1:?address of the lamp}
here=$(cd "$(dirname "$0")" && pwd)
curl -sf -m 20 -F "data=@$here/skin.css;filename=/skin.css" "http://$host/upload" >/dev/null && echo "skin.css on the lamp"
curl -sf -m 20 -F "data=@$here/presets.json;filename=/presets.json" "http://$host/upload" >/dev/null && echo "presets on the lamp"
curl -sf -m 20 -X POST -H 'Content-Type: application/json' \
  -d '{"id":{"name":"Domlamp","mdns":"domlamp"}}' "http://$host/json/cfg" >/dev/null && echo "names set"
echo "restart the lamp for the presets and the name to show everywhere"
