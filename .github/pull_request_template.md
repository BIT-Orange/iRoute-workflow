## Summary

Describe the change in a few lines.

## Change Type

- [ ] Code cleanup or refactor only
- [ ] Experiment runner or promotion change
- [ ] Evidence / aggregate / figure provenance change
- [ ] Paper text or claim-status change
- [ ] Dataset / results migration change

## Claim Impact

- [ ] No claim impact
- [ ] Claim wording or positioning changed
- [ ] Claim status changed in `review/claims/`

Claim IDs touched:

## Canonical Evidence

Use `N/A` only if this PR is genuinely code-only.

- Promoted run IDs:
- Aggregate CSVs:
- Figure provenance files:

## Rerun Requirements

- [ ] No rerun required
- [ ] Exploratory rerun required
- [ ] Paper-grade rerun required

If a rerun was required, list the exact command or wrapper used:

## Validation

Commands run:

```bash
bash scripts/workflow.sh merge-gate
```

Add any extra commands you ran:

## Checklist

- [ ] I used canonical `dataset/` and `results/` paths, or documented legacy compatibility explicitly.
- [ ] I did not relabel exploratory or historical outputs as `paper_grade` without fresh canonical evidence.
- [ ] If evidence changed, I recorded run IDs, aggregates, and figure provenance above.
- [ ] If claim status changed, I updated `review/claims/claims_map.json` and `review/claims/CLAIM_STATUS.md`.
- [ ] If paper-facing evidence changed, I updated any required docs or migration notes.
- [ ] I noted any remaining rerun debt or blocked claim debt in the PR description.
