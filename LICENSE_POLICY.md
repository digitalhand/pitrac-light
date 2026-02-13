# License Policy

This repository currently contains a mix of source provenance and licenses.

## Source-Of-Truth Rules

1. The effective license for a file is the file-level SPDX header.
2. If a file has no SPDX header, treat that as a compliance gap and add one.
3. Existing third-party notices must be preserved.

## License Documents

- MIT license: `LICENSE`
- GPL-2.0-only text: `LICENSES/GPL-2.0-only.txt`
- BSD-2-Clause text: `LICENSES/BSD-2-Clause.txt`
- Repository notice: `NOTICE`

## New Code Policy (Digital Hand)

For brand-new files authored in this repository:

- Use `SPDX-License-Identifier: MIT`.
- Use Digital Hand copyright notice.
- Header template:

```cpp
/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */
```

## Derived / Upstream-Origin Code Policy

For files derived from upstream/original PiTrac code:

- Keep existing upstream SPDX and copyright notices.
- Add Digital Hand copyright lines for substantial new contributions.
- Do not remove upstream notices without explicit legal approval.

Example:

```cpp
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 * Copyright (c) 2026, Digital Hand LLC.
 */
```

## Third-Party Imported Code

- Keep original SPDX/license headers exactly as provided.
- Do not replace with MIT headers.
- Keep required attribution/license files in-tree.

## Practical Workflow

1. Determine whether the file is brand-new or derived.
2. Apply the matching header template.
3. Keep SPDX and copyright line ordering consistent.
4. Run a quick audit before release:

```bash
rg -n "SPDX-License-Identifier|Copyright" -S src pitrac-cli
```

## Notes

- Refactor percentage does not automatically relicense inherited code.
- Fork status also does not change notice obligations.
