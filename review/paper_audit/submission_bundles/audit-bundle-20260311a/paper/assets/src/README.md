# Manual Paper Asset Sources

This directory is the canonical source-managed home for non-experiment paper assets.

Each asset should live in its own subdirectory with an `asset.json` metadata file, for example:

- `paper/assets/src/system-arch/asset.json`
- `paper/assets/src/mermaid/asset.json`

Recommended workflow:

1. add or update the real source file under the relevant subdirectory
2. run:

```bash
bash scripts/workflow.sh paper-assets-sync
```

3. verify `paper/assets/asset_status.json`
4. run:

```bash
bash scripts/workflow.sh paper-preflight
```

Current helper behavior is intentionally conservative:

- it can directly synchronize export-ready sources already in the destination format
- it does not fabricate exports for missing `drawio`, `svg`, or `mmd` sources
- assets remain `blocked` until a real source and synchronized paper-facing output both exist
