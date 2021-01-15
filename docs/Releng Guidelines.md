# Release Engineering guidelines
## Release Engineering flow
Releases are somewhat synchronized with Firefox releases because they release security patches each cycle that we need to adopt and publish ourselves in Pale moon within a reasonable time frame. That currently means a release approximately every 4 weeks.
To make releases go smooth and without unnecessary stress, we should be following the following rough 1-week game plan with each release:
- Firefox release date: **Code freeze.** (Tuesday)  
  From this date, high-risk patches and feature merges should not be landed until after the cycle's version bump lands on merge day.
- **Security updates** Gain access to sec bugs and port applicable bugs across to UXP/Pale Moon (landed on master of each respectively) (Tuesday-Thursday/Friday)
- **Merge day** (Friday)  
  Merge master branches into release, make release branches release-ready, and bump the version on master to start the next cycle.
  Write draft release notes.
- **Build** Release Candidates (RC) (Friday-Saturday)  
  Build signed binaries on various platforms and make them available for final acceptance tests.
- **Testing** phase (RC) (Friday-Monday)  
  Test built binaries for any showstoppers. Implement spot-fixes or backout problematic code changes that are too big to tackle on short notice. Build new binaries (RC2, 3, etc.) as-needed.
- **Finalize** releases (Monday)
  - Distribute final binaries to release mirrors
  - Signal external mirrors
  - Write final release notes
  - Update web front-end in staging
- **Publish** (Tuesday)  
  - Put web pages live, enable new downloads.
  - Publish announcements on forum and social media

## Rough calendar layout
With releases being tied to Firefox releases due to sec and them being set on specific calendar dates, our own published releases will be pretty much set for that reason to a calendar also.
Rough layout for first half of 2021:

- 29.0 - Feb 2nd
- 29.1 - March 2nd
- 29.2 - March 30th
- 29.3 - April 27th
- 29.4 - May 25th
- 29.5 - June 22nd
- 29.6 - July 20th
