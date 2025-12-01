# Security Policy

## Supported Versions

We release patches for security vulnerabilities in the following versions:

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

The KDOS team takes security vulnerabilities seriously. We appreciate your efforts to responsibly disclose your findings.

### How to Report

**Please do NOT report security vulnerabilities through public GitHub issues.**

Instead, please report them via email to: [YOUR-EMAIL]@[DOMAIN]

Please include the following information in your report:
- Type of vulnerability
- Full paths of source file(s) related to the vulnerability
- Location of the affected source code (tag/branch/commit or direct URL)
- Step-by-step instructions to reproduce the issue
- Proof-of-concept or exploit code (if possible)
- Impact of the vulnerability, including how an attacker might exploit it

### What to Expect

- **Acknowledgment**: We will acknowledge receipt of your vulnerability report within 48 hours
- **Initial Assessment**: We will provide an initial assessment within 5 business days
- **Updates**: We will keep you informed about the progress of fixing the vulnerability
- **Resolution**: Once fixed, we will notify you and publicly disclose the vulnerability (with credit to you, if desired)

### Safe Harbor

We consider security research conducted in good faith and in compliance with this policy to be:
- Authorized concerning any applicable anti-hacking laws
- Exempt from the DMCA
- Lawful, helpful, and appreciated

We will not pursue legal action against researchers who:
- Make a good faith effort to avoid privacy violations, data destruction, and service interruption
- Only use exploits to the extent necessary to confirm a vulnerability
- Do not exploit the vulnerability beyond the proof-of-concept
- Report vulnerabilities promptly
- Keep vulnerability details confidential until we've addressed them

## Security Considerations for KDOS Users

### Best Practices

When using KDOS in your embedded systems:

1. **Stack Sizing**: Carefully size task stacks to prevent overflow
2. **Message Queue Sizing**: Size message queues appropriately to prevent overflow
3. **Interrupt Handling**: Disable interrupts when accessing shared variables from both tasks and ISRs
4. **Input Validation**: Always validate data received in messages
5. **Watchdog Timers**: Consider implementing watchdog timers to detect hung tasks

### Known Limitations

KDOS is designed for cooperative multitasking:
- Tasks must voluntarily yield control
- A misbehaving task can monopolize the CPU
- No memory protection between tasks
- Shared variable access requires manual protection

### Security Updates

We will announce security updates through:
- GitHub Security Advisories
- Release notes
- The project README

## Contact

For questions about this policy, please open an issue with the `question` label or contact the maintainers directly.
