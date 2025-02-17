---
name: Release 
about: Checklist to make a release
title: "[RELEASE] v1.0."
labels: ''
assignees: ''

---

# Release Process
The goldilock development and release process follow the typical [git-flow](https://nvie.com/posts/a-successful-git-branching-model/) model.

To perform a new release, the following is required :

- [ ] Merge fixes and features for the release in `develop`
- [ ] Create a commit in `develop` that increments based on semver [`version_in_development`](https://github.com/tipi-build/goldilock/blob/main/.github/workflows/ci.yaml#L9)
- [ ] Create a pull-request from the [`develop` branch targetting the `main` branch](https://github.com/tipi-build/goldilock/compare/main...develop)
- [ ] The PR ci.yaml will Create [a draft release](https://github.com/tipi-build/goldilock/releases)
  - [ ] Check that all CI platform builds and tests properly
  - [ ] Make the draft release public by tagging it
  - [ ] Create integration PR within [hfc](https://github.com/tipi-build/hfc)
  - [ ] Follow and finalize [a full HFC Release](https://github.com/tipi-build/hfc/blob/main/.github/PULL_REQUEST_TEMPLATE/HFC-RELEASING.md)
- [ ] Once HFC with the new goldilock is tested
  - [ ] goldilock pre-release can be made official
  - [ ] Fast-forward release into main `git checkout main && git pull origin main --ff-only && git pull origin develop --ff-only`