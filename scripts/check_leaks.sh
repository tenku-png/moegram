#!/usr/bin/env bash
# Fails if new changes add the user name (so also home paths) or words from .release-forbidden.
# Usage: scripts/check_leaks.sh            staged changes and commits not on origin yet
#        scripts/check_leaks.sh --staged   staged changes only (pre-commit hook)
#        scripts/check_leaks.sh --pre-push refs from stdin (pre-push hook)
#        scripts/check_leaks.sh <range>    e.g. origin/main..HEAD
set -euo pipefail

root=$(git rev-parse --show-toplevel)
super=$(git rev-parse --show-superproject-working-tree)
user=${USER:-$(id -un)}

words=("$user")
for dir in "$root" "$super"; do
	if [ -n "$dir" ] && [ -f "$dir/.release-forbidden" ]; then
		while IFS= read -r word; do
			if [ -n "$word" ]; then
				words+=("$word")
			fi
		done < "$dir/.release-forbidden"
		break
	fi
done

pattern_args=()
for word in "${words[@]}"; do
	pattern_args+=(-e "$word")
done

# WHY: upstream Telegram code has protocol words that can match a user name; lines copied from it are fine.
upstream=""
if git rev-parse -q --verify upstream/dev >/dev/null; then
	upstream=upstream/dev
fi

failed=0
check_lines() {
	local label=$1 line text found=0
	while IFS= read -r line; do
		text=${line#+}
		if [ -n "$upstream" ] && git grep -qF -- "$text" "$upstream" 2>/dev/null; then
			continue
		fi
		echo "leak: $label" >&2
		echo "  $line" >&2
		found=1
	done < <(grep -iF "${pattern_args[@]}" || true)
	return "$found"
}

check_range() {
	local range=$1
	git log --format='%h %an <%ae> | %cn <%ce>%n%B' $range \
		| check_lines "commit info in $range" || failed=1
	git log -p --format= $range | { grep '^+' || true; } \
		| check_lines "added lines in $range" || failed=1
}

check_staged() {
	{ git var GIT_AUTHOR_IDENT; git var GIT_COMMITTER_IDENT; } \
		| check_lines "author of the next commit" || failed=1
	git diff --cached -U0 | { grep '^+' || true; } \
		| check_lines "staged changes" || failed=1
}

default_range() {
	if git rev-parse -q --verify '@{u}' >/dev/null; then
		echo '@{u}..HEAD'
	else
		echo 'HEAD --not --remotes=origin'
	fi
}

case "${1:-}" in
--staged)
	check_staged
	;;
--pre-push)
	zero=0000000000000000000000000000000000000000
	while read -r _ local_sha _ remote_sha; do
		if [ "$local_sha" = "$zero" ]; then
			continue
		elif [ "$remote_sha" = "$zero" ]; then
			check_range "$local_sha --not --remotes=origin"
		else
			check_range "$remote_sha..$local_sha"
		fi
	done
	;;
"")
	check_staged
	check_range "$(default_range)"
	;;
*)
	check_range "$1"
	;;
esac

if [ "$failed" -ne 0 ]; then
	echo "found private words, fix them before committing or pushing" >&2
	exit 1
fi
echo "no leaks"
