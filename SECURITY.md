# Security policy

This policy covers all ARBS repositories: [arbs](https://github.com/ARBS-project/arbs),
[arbs-h](https://github.com/ARBS-project/arbs-h), [arbs-w](https://github.com/ARBS-project/arbs-w) and
[arbs-a](https://github.com/ARBS-project/arbs-a).

## Reporting a vulnerability

Please report privately through GitHub: open the **Security** tab of the affected repository and choose
**Report a vulnerability**. If that button is not available, open an issue titled "Security contact request"
**without any details**, and a maintainer will arrange a private channel.
Do not post exploit details, IP addresses, pairing PINs, tokens or logs containing them in public issues.

## Upstream security fixes

ARBS forks follow their upstreams (Vibepollo, moonlight-qt, moonlight-android, moonlight-common-c). Upstream changes are
reviewed about once a month and whenever an upstream security advisory or release note mentions security.
Direct security fixes are planned to be applied within two weeks; security-related dependency or hardening changes go into
the next regular build. ARBS is experimental, has no binary releases yet and comes without warranty (GPL-3.0).
