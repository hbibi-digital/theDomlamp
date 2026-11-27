#!/bin/sh
# For the maintainer. This repository lives on Gitea (remote "origin"); GitHub (remote "github") is its
# public mirror and the place where pull requests arrive. A pull request is merged here, not on GitHub:
#
#   tools/pr.sh 12           fetch pull request 12 into the branch pr-12 and show what it changes
#   tools/pr.sh 12 merge     merge pr-12 into main with a merge commit and run the firmware test
#
# Nothing is pushed. After "merge": look at the result, then `git push origin main`; Gitea passes it on to
# GitHub, which then marks the pull request as merged by itself (it recognises the commits; that is why
# this is a merge commit and not a squash or a rebase). Never press "Merge" on GitHub: the mirror would
# then meet commits Gitea does not have.
set -eu
n=${1:?number of the pull request}
case $n in *[!0-9]*) echo "not a number: $n" >&2; exit 1 ;; esac
cd "$(git rev-parse --show-toplevel)"
git remote get-url github >/dev/null 2>&1 || { echo 'no remote "github": git remote add github https://github.com/<owner>/theDomlamp.git' >&2; exit 1; }

if [ "${2:-}" = merge ]; then
  [ -z "$(git status --porcelain)" ] || { echo "the working tree has changes: commit or stash them first" >&2; exit 1; }
  git rev-parse --verify -q "pr-$n" >/dev/null || { echo "no branch pr-$n: run tools/pr.sh $n first" >&2; exit 1; }
  git switch main
  git pull --ff-only origin main
  git merge --no-ff "pr-$n" -m "Merge pull request #$n"
  sh firmware/test/run.sh
  echo "merged into main, not pushed. When it is right: git push origin main"
  exit 0
fi

git fetch github "+pull/$n/head:pr-$n"
git fetch origin main
echo "--- pull request #$n: commits not in main"
git log --oneline "origin/main..pr-$n"
echo "--- files changed"
git diff --stat "origin/main...pr-$n"
echo "--- look closer: git diff origin/main...pr-$n     merge: tools/pr.sh $n merge"
