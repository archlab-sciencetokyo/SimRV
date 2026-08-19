# Security and Support Policy

SimRV is maintained on a best-effort basis as an academic research and education project. The
supported release host is Linux x86-64 with the compiler versions listed in the release manifest.
Other hosts, toolchains, guest profiles, and optional integrations may work but are not release
qualified.

SimRV executes untrusted guest instructions inside a complex native process and is not designed or
audited as a security boundary. Do not use it to isolate hostile code or protect sensitive data.

Report suspected vulnerabilities privately to the repository maintainers using GitHub's private
security advisory facility. Include the SimRV revision, host/compiler details, guest configuration,
and a minimal reproducer when possible. Correctness and ordinary crash reports belong in the public
issue tracker.
