# Prompt-pack A/B sweep (~15h)

Eval: `tools/l2_battery/prompt_packs/cases.json` (6 checklist cases).

Packs: `tools/l2_battery/prompt_packs/*.json`  
Playbook: `playbook.json` (baseline + 12 variants).

```bash
./tools/l2_prompt_sweep.sh start    # autonomous
./tools/l2_prompt_sweep.sh status
./tools/l2_prompt_sweep.sh resume
```

Artifacts: `.tuide/ai/l2_prompt_sweep/`  
Promoted: `tools/l2_battery/prompt_packs/promoted/` + `DEFAULT_PACK`.

Harness reads `L2_PROMPT_PACK=/path/to/pack.json` on `l2_harness_cli run`.
