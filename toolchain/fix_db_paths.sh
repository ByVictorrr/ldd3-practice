#!/usr/bin/env bash
set -euo pipefail
# usage: fix_db_paths.sh <input_json> <REMOTE_PROJ_ROOT> <LOCAL_PROJ_ROOT> [REMOTE_KHDR] [LOCAL_KHDR]
# example:
#   fix_db_paths.sh ../compile_commands.json \
#     /home/ml-gpu/clion/ldd3-practice \
#     "C:/Users/delaplai/git/cpp/ldd3-practice" \
#     /usr/src/linux-headers-$(uname -r) \
#     "C:/Users/delaplai/git/cpp/ldd3-practice/toolchain/kheaders"

in="${1:?usage: fix_db_paths.sh <input_json> <REMOTE_PROJ_ROOT> <LOCAL_PROJ_ROOT> [REMOTE_KHDR] [LOCAL_KHDR]}"
REMOTE_ROOT="${2:?remote project root}"
LOCAL_ROOT_RAW="${3:?local project root (Windows ok)}"
REMOTE_KHDR="${4:-/usr/src/linux-headers-$(uname -r)}"
LOCAL_KHDR_RAW="${5:-}"

# normalize Windows paths to forward slashes (clangd accepts them)
LOCAL_ROOT="${LOCAL_ROOT_RAW//\\//}"
LOCAL_KHDR="${LOCAL_KHDR_RAW//\\//}"

out="${in%.json}.local.json"

command -v jq >/dev/null || { echo "jq not installed"; exit 1; }

jq --arg rr "$REMOTE_ROOT" --arg lr "$LOCAL_ROOT" \
   --arg rk "$REMOTE_KHDR" --arg lk "$LOCAL_KHDR" '
  [ .[] | select(type=="object") ]                                           # only objects
  # drop generated junk
  | map(select(.file? | test("\\.mod\\.c$|CMakeCCompilerId\\.c$") | not))
  | map(select(.file? | test("/cmake-build[^/]*/|/CMakeFiles/") | not))
  # rewrite fields and any strings in arguments/command
  | map(
      .file = (
        if (.file? // "" | startswith($rr)) then
          $lr + ((.file // "") | ltrimstr($rr))
        else .file end
      ) |
      .directory = (
        if   ((.directory? // "") | startswith($rk)) and ($lk|length)>0 then
          $lk + ((.directory // "") | ltrimstr($rk))
        elif ((.directory? // "") | startswith($rr)) then
          $lr + ((.directory // "") | ltrimstr($rr))
        else .directory end
      ) |
      .output = (
        if (.output? // "" | startswith($rr)) then
          $lr + ((.output // "") | ltrimstr($rr))
        else .output end
      ) |
      ( if has("arguments") and (.arguments|type)=="array" then
          .arguments = ( .arguments
            | map( if type=="string"
                   then (gsub($rr; $lr) | (if ($lk|length)>0 then gsub($rk; $lk) else . end))
                   else . end ))
        else . end ) |
      ( if has("command") and (.command|type)=="string" then
          .command = ( .command
            | gsub($rr; $lr)
            | (if ($lk|length)>0 then gsub($rk; $lk) else . end))
        else . end )
    )
' "$in" > "$out"

echo "[ok] wrote $out"
