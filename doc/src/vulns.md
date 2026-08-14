# Vulnerability JSON Format

As described in the [Overview](overview.md), BRACELET requires a small JSON blob
describing the vulnerabilities (i.e., CVEs) that the user wishes to analyze. The
following example is taken from [Example 1](example1.md):

```json
{
  "vulnerabilities": [
    {
        "cve-id": "CVE-2022-37434",
        "cve-description": "inflate.c via a large gzip header extra field",
        "package-name": "zlib",
        "package-version": "<=1.2.12",
        "cwe-id": "122",
        "cwe-name": "Heap-based Buffer Overflow",
        "affected-function": "inflateGetHeader",
        "affected-file": "inflate.c"
    }
  ]
}
```
