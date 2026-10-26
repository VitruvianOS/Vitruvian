# Contributing to Vitruvian

Hey, thanks for wanting to help out. Vitruvian (sometimes called V\OS) is a big project and there's always something to do — whether that's writing code, fixing docs, testing on weird hardware, or just filing good bug reports. We appreciate all of it.

## Getting started

1. Fork the repo and clone it locally.
2. Get a working build following the [build instructions](https://wiki.v-os.dev/docs/getting-started/building/). This is the first hurdle — if something doesn't build for you, that's already a useful bug to report.
3. Look through the [open issues](https://github.com/VitruvianOS/Vitruvian/issues) for something that interests you, or open a new issue if you want to propose a change before diving in.

## Code

A few things we care about:

- **Style matters.** We follow Haiku coding conventions (tabs, not spaces; Allman braces; `fMemberVar` naming; return type on its own line; `NULL` not `nullptr`). The [coding guidelines](https://wiki.v-os.dev/docs/development/coding-guidelines/) have the full rundown. Code that doesn't match the surrounding style stands out — and not in a good way.
- **One thing per commit.** Don't bundle unrelated changes. It makes review harder and bisection painful down the road.
- **Explain the *why*.** Commit messages should say why a change is needed, not just what was changed. "Fix crash" is weak; "Guard against null connector in SetMode when hot-unplugging" is better.
- **Test before you PR.** Open a pull request against `master` and tell us what you changed, how you tested it, and anything you're unsure about.

## Bug reports

Open an issue on [GitHub](https://github.com/VitruvianOS/Vitruvian/issues). The more detail, the better:

- What happened, and what you expected to happen
- Steps to reproduce (the more reliable, the better)
- Logs — check `*.out` files and console output
- Your hardware or VM setup

Vague "it doesn't work" reports are hard to act on. Even a screenshot of the console helps.

## Testing on hardware

This is honestly one of the most valuable things you can do. Real hardware exposes bugs that QEMU never will. If something breaks on your machine, file an issue. If you have spare hardware you'd be willing to lend or donate for testing, drop us a message on [Telegram](https://t.me/vitruvian_official_chat) first so we can figure out logistics.

## Documentation

Docs live in the [vos-wiki](https://github.com/VitruvianOS/vos-wiki) repo. Typos, stale info, missing pages — all fair game. Open a PR there.

## Where to find us

- **Telegram**: https://t.me/vitruvian_official_chat — this is where most of the day-to-day discussion happens. If you're thinking about a change, it's often worth bringing it up here before writing code.
- **Mailing list**: https://www.freelists.org/list/vitruvian — lower traffic, good for longer-form discussion.

## License

By contributing to Vitruvian, you agree that your contributions will be licensed under the same license as the component you are modifying (GPL or MIT, depending on the subsystem).

Additionally, by contributing to Vitruvian you accept that your work may be relicensed under either the GPL or MIT license at the project's discretion. This covers situations such as cross-project contributions, licensing opportunities, compatibility with other open-source projects, and similar needs. No individual developer may relicense another developer's contribution without that developer's explicit consent. You retain copyright to your own contributions; this clause simply gives the project the flexibility to relicense as circumstances require.
