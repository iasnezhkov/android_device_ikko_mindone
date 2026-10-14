#!/usr/bin/env bash
set -uo pipefail
usage() { echo "usage: apply-patches.sh check|apply [--root LINEAGE_ROOT] [--dry-run]" >&2; exit 2; }
[ "${BASH_VERSINFO[0]}" -ge 4 ] || { echo "Bash 4 or newer is required" >&2; exit 2; }
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd) || exit 2
SOURCE_ROOT=$SCRIPT_DIR/../../..
[ $# -ge 1 ] || usage
CMD=$1
shift
case "$CMD" in check|apply) ;; *) usage ;; esac
DRYRUN=0
while [ $# -gt 0 ]; do
  case "$1" in
    --root) [ $# -ge 2 ] || usage; SOURCE_ROOT=$2; shift 2 ;;
    --dry-run) [ "$CMD" = apply ] || usage; DRYRUN=1; shift ;;
    *) usage ;;
  esac
done
export GIT_OPTIONAL_LOCKS=0
cd "$SOURCE_ROOT" || exit 2
PATCHDIR=$SCRIPT_DIR/patches
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

PY="$WORK/codediff.py"
cat > "$PY" <<'PYEOF'
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile

C_SUFFIXES = {'.c', '.h', '.cc', '.cpp', '.cxx', '.hpp', '.hh', '.java', '.kt', '.kts', '.bp', '.dts', '.dtsi', '.aidl', '.hal', '.proto'}
HASH_SUFFIXES = {'.py', '.sh', '.bash', '.rc', '.mk', '.prop'}


def code_lines(data, path):
    text = data.decode('utf-8')
    if '\0' in text:
        raise ValueError('binary source')
    suffix = Path(path).suffix
    if suffix in C_SUFFIXES:
        marker, opening, closing = '//', '/*', '*/'
    elif suffix in HASH_SUFFIXES or Path(path).name in {'Makefile', 'Kbuild'}:
        marker, opening, closing = '#', None, None
        if suffix in {'.sh', '.bash', '.mk'} or Path(path).name in {'Makefile', 'Kbuild'}:
            if re.search(r'<<|^\s*(?:define|endef)\b', text, re.M):
                raise ValueError('ambiguous shell or make multiline text')
    elif suffix == '.xml':
        if '<![CDATA[' in text:
            raise ValueError('ambiguous XML literal text')
        marker, opening, closing = None, '<!--', '-->'
    else:
        raise ValueError('unsupported comment syntax')
    result = []
    quote = None
    block = False
    raw_end = None
    for line in text.splitlines(keepends=True):
        has_code = quote is not None or raw_end is not None
        index = 0
        while index < len(line):
            if raw_end is not None:
                has_code = True
                end = line.find(raw_end, index)
                if end < 0:
                    index = len(line)
                else:
                    index = end + len(raw_end)
                    raw_end = None
                continue
            if block:
                end = line.find(closing, index)
                if end < 0:
                    index = len(line)
                else:
                    index = end + len(closing)
                    block = False
                continue
            if quote is not None:
                has_code = True
                if line.startswith(quote, index):
                    index += len(quote)
                    quote = None
                elif line[index] == '\\':
                    index += 2
                else:
                    index += 1
                continue
            if marker and line.startswith(marker, index):
                if marker == '#' and (line.startswith('#!') or suffix == '.py' and re.search(r'coding[:=]', line)):
                    has_code = True
                if line.rstrip('\r\n').endswith('\\'):
                    raise ValueError('continued comment line')
                break
            if opening and line.startswith(opening, index):
                block = True
                index += len(opening)
                continue
            raw = re.match(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(', line[index:]) if suffix in C_SUFFIXES else None
            if raw:
                has_code = True
                raw_end = ')' + raw.group(1) + '"'
                index += raw.end()
                continue
            char = line[index]
            if char in {'"', "'", '`'}:
                has_code = True
                quote = char * 3 if (suffix == '.py' or char == '"' and suffix in {'.java', '.kt', '.kts'}) and line.startswith(char * 3, index) else char
                index += len(quote)
            else:
                if not char.isspace():
                    has_code = True
                index += 1
        if has_code or not line.strip() and not block:
            result.append(line)
    if quote is not None or block or raw_end is not None:
        raise ValueError('unterminated or ambiguous source text')
    return result


def main():
    project, upstream, filelist = sys.argv[1:]
    project = str(Path(project).resolve())
    patches = Path(filelist).read_text().splitlines()
    if not patches or any(not patch for patch in patches):
        raise ValueError('empty patch series')
    with tempfile.TemporaryDirectory(prefix='rom-patch-index-') as directory:
        environment = dict(os.environ, GIT_INDEX_FILE=str(Path(directory) / 'index'))
        def git(args, expected=False):
            return subprocess.run(['git', '-C', project, *args], env=environment if expected else None, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30).stdout
        base = git(['rev-parse', '--verify', upstream + '^{commit}']).decode().strip()
        git(['read-tree', base], True)
        for patch in patches:
            git(['apply', '--cached', '--', patch], True)
        changed = set()
        expected_changed = set()
        for args, expected in [(['diff', '--cached', '--no-renames', '--name-only', '-z', base], True), (['diff', '--no-renames', '--name-only', '-z', base], False), (['diff', '--cached', '--no-renames', '--name-only', '-z', base], False)]:
            names = {os.fsdecode(path) for path in git(args, expected).split(b'\0') if path}
            changed.update(names)
            if expected:
                expected_changed.update(names)
        paths = sorted(changed)
        def entries(expected):
            result = {}
            if not paths:
                return result
            for item in git(['ls-files', '--stage', '-z', '--', *paths], expected).split(b'\0'):
                if not item:
                    continue
                meta, path = item.split(b'\t', 1)
                mode, oid, stage = meta.decode().split()
                if stage != '0':
                    raise ValueError('unmerged tracked file')
                result[os.fsdecode(path)] = (mode, oid)
            return result
        wanted, tracked = entries(True), entries(False)
        comments = False
        drift = False
        for path in paths:
            expected = wanted.get(path)
            actual = tracked.get(path)
            file = Path(project) / path
            exists = file.is_symlink() or file.exists()
            if expected is None or actual is None:
                if expected is None and (exists or actual is not None and path not in expected_changed) or expected is not None and actual is None:
                    print('  DIFFERS: tracked path ' + repr(path))
                    drift = True
                continue
            info = file.lstat() if exists else None
            mode = None if info is None else '120000' if stat.S_ISLNK(info.st_mode) else '100755' if stat.S_ISREG(info.st_mode) and info.st_mode & 0o111 else '100644' if stat.S_ISREG(info.st_mode) else None
            if mode != expected[0]:
                print('  DIFFERS: mode or type ' + repr(path))
                drift = True
                continue
            wanted_bytes = git(['cat-file', 'blob', expected[1]], True)
            actual_bytes = os.fsencode(os.readlink(file)) if mode == '120000' else file.read_bytes()
            if wanted_bytes == actual_bytes:
                print('  same source: ' + path)
                continue
            try:
                equal = mode in {'100644', '100755'} and code_lines(wanted_bytes, path) == code_lines(actual_bytes, path)
            except (ValueError, UnicodeError):
                equal = False
            if equal:
                comments = True
                print('  same code: ' + path)
            else:
                drift = True
                print('  DIFFERS: source content ' + repr(path))
        return 1 if drift else 2 if comments else 0


try:
    sys.exit(main())
except Exception as error:
    print('  ERROR: cannot compose or compare patch series: ' + str(error))
    if isinstance(error, subprocess.CalledProcessError):
        print(error.stderr.decode('utf-8', errors='replace').strip())
    sys.exit(3)

PYEOF

if ! python3 .repo/repo/repo list -p > "$WORK/projects"; then
  echo "ERROR: cannot enumerate repo projects" >&2
  exit 2
fi
mapfile -t ALLPROJ < "$WORK/projects"
[ "${#ALLPROJ[@]}" -gt 0 ] || { echo "ERROR: repo has no projects" >&2; exit 2; }

resolve_project() {
  local prefix=$1
  local best="" bestlen=-1
  local p key
  for p in "${ALLPROJ[@]}"; do
    key=${p//\//_}
    if [ "$key" = "$prefix" ] || [[ "$prefix" == "${key}_"* ]]; then
      if [ ${#key} -gt "$bestlen" ]; then
        best=$p
        bestlen=${#key}
      fi
    fi
  done
  [ -n "$best" ] || return 1
  printf '%s\n' "$best"
}

mkdir -p "$WORK/plist"
mapfile -t PATCHFILES < <(for patch in "$PATCHDIR"/*.patch; do
  [ -f "$patch" ] && printf '%s\n' "${patch##*/}"
done | LC_ALL=C sort)
UNRESOLVED=0
declare -A SEEN_PREFIX
for f in "${PATCHFILES[@]}"; do
  if [[ "$f" =~ ^(.+)-([0-9][0-9][0-9][0-9])-.+\.patch$ ]]; then
    prefix=${BASH_REMATCH[1]}
  else
    echo "ERROR: unrecognized patch filename: $f"
    UNRESOLVED=1
    continue
  fi
  if [ -z "${SEEN_PREFIX[$prefix]:-}" ]; then
    SEEN_PREFIX[$prefix]=1
    if proj=$(resolve_project "$prefix"); then
      echo "$proj" > "$WORK/plist/${prefix}.proj"
    else
      echo "UNRESOLVED patch group '$prefix': no guest project matches it (checked ${#ALLPROJ[@]} projects)"
      UNRESOLVED=1
    fi
  fi
  if [ -f "$WORK/plist/${prefix}.proj" ]; then
    echo "$PATCHDIR/$f" >> "$WORK/plist/${prefix}.files"
  fi
done

[ "${#PATCHFILES[@]}" -gt 0 ] || { echo "ERROR: no patch files" >&2; exit 2; }
[ "$UNRESOLVED" = 0 ] || exit 2

declare -A PROJFILES_PATH
for pf in "$WORK"/plist/*.proj; do
  [ -e "$pf" ] || continue
  proj=$(cat "$pf")
  key=${proj//\//_S_}
  src="${pf%.proj}.files"
  dst="$WORK/proj_files_${key}"
  cat "$src" >> "$dst"
  PROJFILES_PATH[$proj]=$dst
done

declare -A INSCOPE
for proj in "${!PROJFILES_PATH[@]}"; do INSCOPE[$proj]=1; done

if [ "$CMD" = check ]; then
  if ! python3 .repo/repo/repo forall -c '
u=""
for c in m/lineage-23.2 origin/lineage-23.2 github/lineage-23.2; do
  git rev-parse --verify -q "$c" >/dev/null 2>&1 && { u=$c; break; }
done
ahead=0
[ -n "$u" ] && ahead=$(git rev-list --count "$u"..HEAD 2>/dev/null || echo 0)
dirty=$(git status --porcelain --untracked-files=no | wc -l | tr -d " ")
if [ "$ahead" -gt 0 ] || [ "$dirty" -gt 0 ]; then
  echo "$REPO_PATH"
fi
' 2>"$WORK/reposcan.err" > "$WORK/reposcan.list"; then
    cat "$WORK/reposcan.err" >&2
    echo "ERROR: cannot inspect repo projects" >&2
    exit 2
  fi
  while IFS= read -r proj; do
    [ -n "$proj" ] && INSCOPE[$proj]=1
  done < "$WORK/reposcan.list"
fi

EVAL_STATUS=""
EVAL_UPSTREAM=""
EVAL_UPSTREAM_SHA=""

evaluate_project() {
  local proj=$1
  local filelist="${PROJFILES_PATH[$proj]:-}"
  EVAL_STATUS=error
  EVAL_UPSTREAM=""
  EVAL_UPSTREAM_SHA=""
  echo "--- $proj ---"
  if [ ! -e "$proj/.git" ]; then
    echo "  ERROR: not a git project (path missing or no .git)"
    return
  fi
  local gitdir operation
  gitdir=$(git -C "$proj" rev-parse --absolute-git-dir) || return
  for operation in rebase-apply rebase-merge MERGE_HEAD CHERRY_PICK_HEAD REVERT_HEAD; do
    if [ -e "$gitdir/$operation" ]; then
      echo "  ERROR: unfinished Git operation: $operation"
      return
    fi
  done
  local upstream="" c
  for c in m/lineage-23.2 origin/lineage-23.2 github/lineage-23.2; do
    if git -C "$proj" rev-parse --verify -q "$c" >/dev/null 2>&1; then upstream=$c; break; fi
  done
  if [ -z "$upstream" ]; then
    echo "  ERROR: no upstream ref (tried m/origin/github lineage-23.2)"
    return
  fi
  EVAL_UPSTREAM=$upstream
  EVAL_UPSTREAM_SHA=$(git -C "$proj" rev-parse "$upstream")

  local orig_rej
  orig_rej=$(find "$proj" -path "$proj/.git" -prune -o \( -name '*.orig' -o -name '*.rej' \) -print)

  local local_commit_ids
  local_commit_ids=$(git -C "$proj" format-patch --stdout "$upstream"..HEAD 2>/dev/null | git -C "$proj" patch-id --stable 2>/dev/null | awk '{print $1}')
  local uncommitted_diff
  uncommitted_diff=$(git -C "$proj" diff HEAD)
  local uncommitted_id=""
  if [ -n "$uncommitted_diff" ]; then
    uncommitted_id=$(printf '%s\n' "$uncommitted_diff" | git -C "$proj" patch-id --stable 2>/dev/null | awk '{print $1}')
  fi
  local local_ids
  local_ids=$(printf '%s\n%s\n' "$local_commit_ids" "$uncommitted_id" | sed '/^$/d')

  local n_patches=0
  if [ -n "$filelist" ]; then
    n_patches=$(wc -l < "$filelist" | tr -d ' ')
  fi

  if [ -n "$orig_rej" ]; then
    echo "  DRIFT: stray .orig/.rej files present:"
    printf '%s\n' "$orig_rej" | sed 's/^/    /'
    EVAL_STATUS=drift
  fi

  if [ -z "$filelist" ]; then
    echo "  DRIFT: local changes with no patch file for this project"
    echo "    upstream: $upstream ($EVAL_UPSTREAM_SHA)"
    if [ -n "$local_commit_ids" ]; then
      echo "    commits ahead:"
      git -C "$proj" log --oneline "$upstream"..HEAD | sed 's/^/      /'
    fi
    if [ -n "$uncommitted_diff" ]; then
      echo "    uncommitted tracked edits:"
      git -C "$proj" diff --stat | sed 's/^/      /'
    fi
    EVAL_STATUS=drift
    return
  fi

  local pyout pyrc
  pyout=$(python3 "$PY" "$proj" "$EVAL_UPSTREAM_SHA" "$filelist")
  pyrc=$?
  if [ "$pyrc" -ne 0 ] && [ "$pyrc" -ne 1 ] && [ "$pyrc" -ne 2 ]; then
    printf '%s\n' "$pyout"
    [ "$EVAL_STATUS" = drift ] || EVAL_STATUS=error
    return
  fi

  if [ -z "$local_ids" ] && [ -n "$filelist" ]; then
    echo "  NOT PATCHED: $n_patches patch file(s) in patches/, 0 commits ahead of $upstream, no uncommitted edits"
    [ "$EVAL_STATUS" = drift ] || EVAL_STATUS=notpatched
    return
  fi


  if [ "$pyrc" -eq 0 ]; then
    echo "  in sync: final source matches $n_patches composed patch(es), upstream $upstream ($EVAL_UPSTREAM_SHA)"
    [ "$EVAL_STATUS" = drift ] || EVAL_STATUS=insync
  elif [ "$pyrc" -eq 2 ]; then
    echo "  comments differ (acceptable): final source order and code match"
    [ "$EVAL_STATUS" = drift ] || EVAL_STATUS=comments
  else
    echo "  DRIFT: composed patch series and tracked source differ"
    EVAL_STATUS=drift
  fi
  printf '%s\n' "$pyout" | sed 's/^/    /'

}

if [ "$CMD" = check ]; then
  echo "=== rom-patches.sh check: $(date '+%Y-%m-%d %H:%M:%S') ==="
  EXIT=0
  COUNT_INSYNC=0; COUNT_COMMENTS=0; COUNT_DRIFT=0; COUNT_NOTPATCHED=0; COUNT_ERROR=0
  mapfile -t PROJS < <(printf '%s\n' "${!INSCOPE[@]}" | sort)
  for proj in "${PROJS[@]}"; do
    evaluate_project "$proj"
    case "$EVAL_STATUS" in
      insync) COUNT_INSYNC=$((COUNT_INSYNC+1)) ;;
      comments) COUNT_COMMENTS=$((COUNT_COMMENTS+1)) ;;
      drift) COUNT_DRIFT=$((COUNT_DRIFT+1)); EXIT=1 ;;
      notpatched) COUNT_NOTPATCHED=$((COUNT_NOTPATCHED+1)); EXIT=1 ;;
      error) COUNT_ERROR=$((COUNT_ERROR+1)); EXIT=1 ;;
    esac
  done
  [ "$UNRESOLVED" = 1 ] && EXIT=1
  echo
  echo "=== summary: in sync $COUNT_INSYNC, comments differ $COUNT_COMMENTS, DRIFT $COUNT_DRIFT, not patched $COUNT_NOTPATCHED, error $COUNT_ERROR ==="
  exit $EXIT
fi

if [ "$CMD" = apply ]; then
  echo "=== rom-patches.sh apply${DRYRUN:+ (dry-run pending)}: $(date '+%Y-%m-%d %H:%M:%S') ==="
  [ "$DRYRUN" = 1 ] && echo "(--dry-run: nothing below is actually touched)"
  EXIT=0
  mapfile -t PROJS < <(printf '%s\n' "${!PROJFILES_PATH[@]}" | sort)
  for proj in "${PROJS[@]}"; do
    evaluate_project "$proj"
    if [ "$EVAL_STATUS" = notpatched ] && [ -n "$(git -C "$proj" status --porcelain --untracked-files=all)" ]; then
      echo "  ERROR: initial apply requires a clean worktree, including untracked files"
      EVAL_STATUS=error
    fi
    if [ "$EVAL_STATUS" = drift ] || [ "$EVAL_STATUS" = error ]; then
      echo "REFUSED: all projects must pass preflight before any apply"
      EXIT=1
    fi
  done
  [ "$EXIT" = 0 ] || exit "$EXIT"
  for proj in "${PROJS[@]}"; do
    evaluate_project "$proj"
    filelist="${PROJFILES_PATH[$proj]}"
    n_patches=$(wc -l < "$filelist" | tr -d ' ')
    if [ "$EVAL_STATUS" = drift ] || [ "$EVAL_STATUS" = error ]; then
      echo "  REFUSED: apply needs a clean project (see above)"
      EXIT=1
      continue
    fi
    if [ "$EVAL_STATUS" = insync ] || [ "$EVAL_STATUS" = comments ]; then
      echo "  unchanged: patch series is already present"
      continue
    fi
    fullsha=$EVAL_UPSTREAM_SHA
    if [ "$DRYRUN" = 1 ]; then
      echo "  would detach at $EVAL_UPSTREAM ($EVAL_UPSTREAM_SHA) and git am --3way $n_patches patch(es):"
      while IFS= read -r patch; do printf '    %s\n' "${patch##*/}"; done < "$filelist"
      continue
    fi
    if ! git -C "$proj" checkout --detach "$fullsha" > "$WORK/co.log" 2>&1; then
      echo "  FAILED: git checkout --detach $fullsha"
      sed 's/^/    /' "$WORK/co.log"
      EXIT=1
      continue
    fi
    mapfile -t project_patches < "$filelist"
    if ! git -C "$proj" am --3way -- "${project_patches[@]}" > "$WORK/am.log" 2>&1; then
      echo "  FAILED: git am --3way"
      sed 's/^/    /' "$WORK/am.log"
      git -C "$proj" am --abort >/dev/null 2>&1
      EXIT=1
      continue
    fi
    newhead=$(git -C "$proj" rev-parse --short HEAD)
    echo "  applied $n_patches patch(es), HEAD now $newhead:"
    git -C "$proj" log --oneline -n "$n_patches" | sed 's/^/    /'
  done
  echo
  echo "=== apply done, exit $EXIT ==="
  exit $EXIT
fi
