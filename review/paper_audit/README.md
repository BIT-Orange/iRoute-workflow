# Paper Audit Notes

Use this folder for paper-structure reviews, reproducibility checks, figure provenance notes, and bibliography cleanup tasks.

Stage-1 context:

- `paper/main.tex` is now the canonical paper source
- `paper.tex` remains only as a deprecated compatibility copy
- `ns-3/src/ndnSIM/paper/paper.tex` remains only as a deprecated legacy source
- The missing figure assets listed in `MIGRATION_NOTES.md` are known blockers for a clean paper compile

Release dossier outputs:

- `paper_release_dossier.json`: machine-readable release snapshot
- `paper_release_dossier.md`: human-readable release snapshot

Regenerate them with:

```bash
bash scripts/workflow.sh release-dossier
```
