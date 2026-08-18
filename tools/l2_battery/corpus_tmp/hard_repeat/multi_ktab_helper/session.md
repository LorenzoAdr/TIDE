# L2 session

## Instruction

query: En src/ui/main_layout.hpp: añade static constexpr int kPs = 12; tras kAi en ConsolePanelTabs, y en ai_tab_active acepta también kPs.

instruction: Explore then done next=edit; emit Search/Replace hunks; runtime compiles.

Fase inicial: **explore**. Preferir `action=plan` en el **primer** paso con 4–8 targets `path:Symbol`/`path:line` (evitar path bare). Máx. ~8 tools sueltos antes del primer plan. Tras pack cubierto: extras con `tools` batch (máx. 4); luego `{"action":"done","summary":"…","next":"edit"}` o `edit` directo. Truncado ≠ bloqueo de edit si no hay gaps Instruction.

## Ranked map




query: En src/ui/main_layout.hpp: añade static constexpr int kPs = 12; tras kAi en ConsolePanelTabs, y en ai_tab_active acepta también kPs.

note: git boost tracked=9583 untracked=1; PageRank (669 nodos con aristas); query_hits=1608; two_stage=1; embed_sig=1; embed_body=0; cand_in=161; n=161; phase_a_ms=4544; phase_b_ms=0; total_ms=5323; priors=1; lex_prefilter=1; src=lexical_shortlist; code_edit=1; ranked_map=1; entries=161

## Ranked entries

1. src/ui/console_panel.cpp:1736  [score=5185595] — `tick_terminal_shell`
2. src/ui/console_panel.cpp:2062  [score=3733836] — `render_shell_terminal`
3. src/ui/main_layout.hpp:104  [score=3315291] — `ConsolePanelTabs`
4. src/ui/main_layout.hpp:328  [score=3308665] — `console_panel_tab_active`
5. src/ui/console_panel.cpp:963  [score=3167842] — `terminal_tab_active`
6. src/ui/console_panel.cpp:1046  [score=2948552] — `console_tab_from_digit`
7. src/ui/console_panel.cpp:1015  [score=2948540] — `console_tab_press_id`
8. src/ui/console_panel.hpp:40  [score=2868141] — `cycle_console_tab`
9. src/ui/console_panel.cpp:946  [score=2678546] — `shell_terminal_input_active`
10. src/ui/console_panel.hpp:1  [score=2547323] — `console_panel`
11. src/ui/panel.hpp:1  [score=2532287] — `panel`
12. src/ui/ui_wake.hpp:47  [score=2370591] — `wake_console_panel`
13. src/ui/ui_wake.hpp:57  [score=2370587] — `wake_console_panel_stream`
14. src/terminal/shell_session.cpp:421  [score=2243558] — `on_pty_bytes`
15. src/terminal/shell_session.cpp:407  [score=2243554] — `feed_pty_bytes_locked`
16. src/ui/panel.hpp:33  [score=2226695] — `terminal_width_or_default`
17. src/ui/panel.hpp:37  [score=2226692] — `terminal_height_or_default`
18. src/ui/terminal_ui_channel.hpp:22  [score=2222741] — `on_pty_output`
19. src/ui/terminal_ui_channel.hpp:16  [score=2222739] — `on_pty_output`
20. src/ui/console_panel.hpp:26  [score=2066941] — `MakeConsolePanel`
21. src/ui/console_panel.hpp:21  [score=2064530] — `UiCommand`
22. src/ui/console_panel.hpp:23  [score=2062800] — `MainLayoutState`
23. src/terminal/shell_session.hpp:83  [score=2043567] — `on_pty_bytes`
24. src/terminal/shell_session.hpp:82  [score=2043563] — `feed_pty_bytes_locked`
25. src/terminal/app_session.cpp:386  [score=2037478] — `on_pty_bytes`
26. src/terminal/app_session.cpp:372  [score=2037475] — `feed_pty_bytes_locked`
27. src/ui/console_panel.hpp:19  [score=1984190] — `tuide`
28. src/terminal/raw_pty_screen.cpp:1  [score=1952129] — `raw_pty_screen`
29. src/terminal/app_session.hpp:73  [score=1837479] — `on_pty_bytes`
30. src/terminal/app_session.hpp:72  [score=1837477] — `feed_pty_bytes_locked`
31. src/ui/diagnostics_panel.cpp:1  [score=1817194] — `diagnostics_panel`
32. src/ui/terminal_keyboard.cpp:1  [score=1772377] — `terminal_keyboard`
33. src/ui/diagnostics_panel.cpp:395  [score=1766961] — `clamp_scroll_viewport`
34. src/ui/diagnostics_panel.cpp:402  [score=1766952] — `ensure_selection_visible`
35. src/ui/diagnostics_panel.cpp:202  [score=1766946] — `visible_problem_count`
36. src/terminal/raw_pty_screen.hpp:1  [score=1752137] — `raw_pty_screen`
37. src/ui/terminal_display.cpp:1  [score=1652378] — `terminal_display`
38. src/ui/diagnostics_panel.hpp:1  [score=1617180] — `diagnostics_panel`
39. src/ui/outline_panel.cpp:1  [score=1577190] — `outline_panel`
40. src/ui/terminal_keyboard.hpp:1  [score=1572375] — `terminal_keyboard`
41. src/terminal/raw_pty_screen.cpp:92  [score=1552158] — `RawPtyScreen`
42. src/terminal/raw_pty_screen.hpp:11  [score=1511354] — `TerminalStyledSpan`
43. src/ui/terminal_display.hpp:1  [score=1452371] — `terminal_display`
44. src/terminal/raw_pty_screen.hpp:34  [score=1430210] — `cursor_col`
45. src/terminal/raw_pty_screen.hpp:33  [score=1430205] — `cols`
46. src/terminal/raw_pty_screen.cpp:114  [score=1430116] — `resize`
47. src/terminal/raw_pty_screen.cpp:104  [score=1430110] — `reset`
48. src/terminal/raw_pty_screen.cpp:482  [score=1430110] — `text`
49. src/terminal/raw_pty_screen.cpp:32  [score=1430058] — `ansi_256_color`
50. src/ui/outline_panel.hpp:1  [score=1377188] — `outline_panel`
51. src/terminal/raw_pty_screen.hpp:26  [score=1352187] — `RawPtyScreen`
52. src/terminal/raw_pty_screen.hpp:24  [score=1350478] — `RawPtyScreen`
53. src/ui/settings_modal.cpp:60  [score=1307043] — `switch_top_level_tab`
54. src/ui/settings_modal.cpp:1467  [score=1307042] — `switch_top_level_tab`
55. src/terminal/shell_session.cpp:1  [score=1268910] — `shell_session`
56. src/ui/binary_symbols_panel.cpp:781  [score=1260406] — `scan_shell_output_for_linker_errors`
57. src/util/shell_args.cpp:188  [score=1260221] — `apply_path_tab_completion`
58. src/toolpacks/export_portable.cpp:22  [score=1220348] — `shell_quote`
59. src/ui/search_panel.cpp:1  [score=1217188] — `search_panel`
60. src/ui/editor_panel.cpp:1664  [score=1184649] — `tabular_max_allowed_scroll`
61. src/ui/editor_panel.cpp:3189  [score=1184648] — `tabular_max_allowed_scroll`
62. src/ui/editor_panel.cpp:1657  [score=1184646] — `tabular_max_scroll_col`
63. src/ui/editor_panel.cpp:3361  [score=1184644] — `tabular_max_scroll_col`
64. src/ui/editor_panel.cpp:3185  [score=1184639] — `tabular_data_visible_lines`
65. src/ui/editor_panel.cpp:1663  [score=1184638] — `tabular_data_visible_lines`
66. src/ui/press_ids.hpp:174  [score=1170585] — `is_console_tab_hover`
67. src/app/editor_tabs.cpp:1  [score=1170051] — `editor_tabs`
68. src/ui/packet_monitor_panel.cpp:1  [score=1157192] — `packet_monitor_panel`
69. src/ui/watches_panel.cpp:844  [score=1140238] — `handle_breakpoints_tab_keys`
70. src/ui/watches_panel.cpp:475  [score=1140229] — `switch_tab_from_mouse`
71. src/ui/git_panel.cpp:412  [score=1140211] — `MakeCompactTabButton`
72. src/ui/watches_panel.cpp:453  [score=1140151] — `switch_tab`
73. src/ui/git_panel.cpp:916  [score=1140143] — `select_tab`
74. src/terminal/shell_session.hpp:1  [score=1068917] — `shell_session`
75. src/util/shell_utils.cpp:1  [score=1067491] — `shell_utils`
76. src/util/shell_args.cpp:1  [score=1067487] — `shell_args`
77. src/ui/binary_symbols_panel.hpp:27  [score=1060400] — `scan_shell_output_for_linker_errors`
78. src/util/shell_args.hpp:14  [score=1060218] — `apply_path_tab_completion`
79. src/ui/source_panel.cpp:1  [score=1037182] — `source_panel`
80. src/util/tabular_file.cpp:1  [score=1031080] — `tabular_file`
81. src/ui/search_panel.hpp:1  [score=1017186] — `search_panel`
82. src/ui/terminal_ui_channel.hpp:1  [score=1002373] — `terminal_ui_channel`
83. src/ui/binary_symbols_panel.cpp:1  [score=987204] — `binary_symbols_panel`
84. src/ui/core_analyzer_panel.cpp:1  [score=987195] — `core_analyzer_panel`
85. src/ui/right_sidebar_panel.cpp:1  [score=987194] — `right_sidebar_panel`
86. src/ui/call_hierarchy_panel.cpp:1  [score=987186] — `call_hierarchy_panel`
87. src/ui/file_tree_panel.cpp:1  [score=987186] — `file_tree_panel`
88. src/ui/file_preview_panel.cpp:1  [score=987184] — `file_preview_panel`
89. src/ui/performance_panel.cpp:1  [score=987176] — `performance_panel`
90. src/util/docker_shell.cpp:1  [score=987168] — `docker_shell`
91. src/ui/git_panel.cpp:1  [score=987107] — `git_panel`
92. src/editor/helix/helix_textobjects.cpp:1  [score=979180] — `helix_textobjects`
93. src/app/editor_tabs.hpp:1  [score=970045] — `editor_tabs`
94. src/ui/packet_monitor_panel.hpp:1  [score=957189] — `packet_monitor_panel`
95. src/ui/editor_tab_bar.cpp:1  [score=950672] — `editor_tab_bar`
96. src/toolpacks/export_portable.cpp:1  [score=950645] — `export_portable`
97. src/ui/watches_panel.cpp:125  [score=936946] — `watch_inject_active`
98. tests/tabular_file_test.cpp:1  [score=910858] — `tabular_file_test`
99. src/util/shell_args.hpp:1  [score=867493] — `shell_args`
100. src/util/shell_utils.hpp:1  [score=867490] — `shell_utils`
101. src/util/ui_panel_render_cache.hpp:1  [score=867189] — `ui_panel_render_cache`
102. src/ui/watches_panel.hpp:1  [score=867185] — `watches_panel`
103. src/ui/editor_panel.hpp:1  [score=867184] — `editor_panel`
104. src/terminal/shell_session.cpp:85  [score=864750] — `ShellSession`
105. src/terminal/shell_session.cpp:87  [score=864748] — `ShellSession`
106. src/terminal/pty_input.cpp:5  [score=839720] — `tuide`
107. src/terminal/app_session.cpp:33  [score=837480] — `has_live_pty`
108. src/terminal/app_session.cpp:110  [score=837468] — `open_host_pty`
109. src/ui/source_panel.hpp:1  [score=837179] — `source_panel`
110. src/util/tabular_file.hpp:1  [score=831072] — `tabular_file`
111. tests/raw_pty_screen_test.cpp:1  [score=830452] — `raw_pty_screen_test`
112. src/util/tabular_file.cpp:146  [score=828103] — `TabularFileStore`
113. src/util/tabular_file.cpp:148  [score=828098] — `TabularFileStore`
114. src/util/docker_shell.cpp:14  [score=803066] — `terminal_mount_mappings`
115. src/ui/binary_symbols_panel.hpp:1  [score=787194] — `binary_symbols_panel`
116. src/ui/right_sidebar_panel.hpp:1  [score=787192] — `right_sidebar_panel`
117. src/ui/file_preview_panel.hpp:1  [score=787184] — `file_preview_panel`
118. src/ui/file_tree_panel.hpp:1  [score=787183] — `file_tree_panel`
119. src/ui/core_analyzer_panel.hpp:1  [score=787182] — `core_analyzer_panel`
120. src/ui/call_hierarchy_panel.hpp:1  [score=787175] — `call_hierarchy_panel`
121. src/ui/performance_panel.hpp:1  [score=787174] — `performance_panel`
122. src/util/docker_shell.hpp:1  [score=787161] — `docker_shell`
123. src/ui/git_panel.hpp:1  [score=787111] — `git_panel`
124. src/editor/helix/helix_textobjects.hpp:1  [score=779175] — `helix_textobjects`
125. src/app/editor_tabs.cpp:8  [score=770053] — `compute_visible_tab_range`
126. src/app/editor_tabs.cpp:30  [score=770042] — `format_editor_tab_overflow_button`
127. src/ui/editor_tab_bar.cpp:213  [score=769986] — `make_tabs_overflow_modal`
128. src/ui/editor_tab_bar.cpp:384  [score=769968] — `handle_tabs_overflow_keys`
129. src/app/editor_tabs.cpp:6  [score=767666] — `tuide`
130. src/ui/editor_tab_bar.hpp:1  [score=750667] — `editor_tab_bar`
131. src/toolpacks/export_portable.hpp:1  [score=750643] — `export_portable`
132. src/terminal/shell_session.hpp:19  [score=747267] — `ShellLaunchConfig`
133. src/symbols/completion_snippet.cpp:14  [score=722930] — `TabStop`
134. src/terminal/shell_session.cpp:36  [score=722786] — `write_terminal_init_script`
135. src/app/editor_tabs.hpp:25  [score=710751] — `TabVisibleRange`
136. src/terminal/shell_session.hpp:32  [score=667267] — `ShellSession`
137. src/terminal/shell_session.hpp:35  [score=664775] — `ShellSession`
138. src/app/editor_tabs.hpp:12  [score=648263] — `EditorTab`
139. src/app/workspace_model.hpp:35  [score=644301] — `open_tabs_mru`
140. src/app/workspace_model.hpp:69  [score=644300] — `reload_stale_tabs_from_disk`
141. src/app/workspace_model.hpp:76  [score=644296] — `refresh_git_diff_tabs_for_path`
142. src/app/workspace_model.hpp:36  [score=644287] — `open_tabs_mru_excluding_active`
143. src/terminal/pty_input.hpp:8  [score=639717] — `tuide`
144. src/terminal/app_session.hpp:44  [score=637480] — `has_live_pty`
145. src/terminal/app_session.hpp:36  [score=637462] — `open_host_pty`
146. src/util/tabular_file.hpp:17  [score=630579] — `TabularFileStore`
147. src/util/tabular_file.hpp:19  [score=628083] — `TabularFileStore`
148. src/util/tabular_file.hpp:20  [score=628082] — `TabularFileStore`
149. src/app/editor_tabs.hpp:40  [score=570051] — `compute_visible_tab_range`
150. src/app/editor_tabs.hpp:42  [score=570034] — `format_editor_tab_overflow_button`
151. src/ui/editor_tab_bar.hpp:46  [score=569984] — `make_tabs_overflow_modal`
152. src/ui/editor_tab_bar.hpp:59  [score=569962] — `handle_tabs_overflow_keys`
153. src/app/editor_tabs.hpp:10  [score=567662] — `tuide`
154. src/terminal/pty_input.cpp:1  [score=42416] — `pty_input`
155. src/terminal/pty_input.hpp:1  [score=-157584] — `pty_input`
156. src/ui/watches_panel.cpp:120  [score=-263049] — `watch_input_active`
157. src/ui/watches_panel.cpp:130  [score=-263052] — `breakpoint_hw_input_active`
158. src/terminal/pty_input.cpp:28  [score=-357905] — `event_to_pty_bytes`
159. src/terminal/pty_input.cpp:11  [score=-477446] — `looks_like_terminal_mouse_report`
160. src/terminal/pty_input.hpp:10  [score=-557898] — `event_to_pty_bytes`
161. tools/build-portable.sh:1  [score=-1669512] — `build-portable`

<!-- map compacted: full detail for 0 hot stem(s); other entries are name-only -->
## Observations

(vacío — L2 pide plan/tools)
### turn 1 — plan

Explorando targets para añadir kPs

target_count: 2  fragments_ok: 2/2  outlines_ok: 1/1  truncated: 0  pack_chars: 4011/9000  auto_refetch: 0
Archivo: `.tuide/ai/l2/pack.md`. El siguiente prompt usa Instruction+pack (mapa fuera). Emite `done next=edit`, `edit`, o amplía con otro `plan`/`tools`.

### turn 2 — plan

Explorando targets para añadir kPs

target_count: 2  fragments_ok: 2/2  outlines_ok: 1/1  truncated: 0  pack_chars: 4011/9000  auto_refetch: 0
Archivo: `.tuide/ai/l2/pack.md`. El siguiente prompt usa Instruction+pack (mapa fuera). Emite `done next=edit`, `edit`, o amplía con otro `plan`/`tools`.

_nudge:_ Llevas 2 `plan` seguidos con pack cubierto. Emite `done next=edit` o `edit` (no más `plan` sin tools/`get_code_of` nuevos).

### turn 3 — ready_to_edit

código localizado…

Fase **edit**: emite `action=edit` con hunks Search/Replace. Máx. ~2 tools de refetch; luego edit obligatorio. Si el pack marcó [TRUNCATED] en la zona a editar, refetch antes.

## span sugerido (pack)

```
src/ui/main_layout.hpp:104-118 (ConsolePanelTabs)
symbol_span: 104-118
struct ConsolePanelTabs {
  static constexpr int kTerminal = 0;
  static constexpr int kApp = 1;
  static constexpr int kDebug = 2;
  static constexpr int kPerformance = 3;
  static constexpr int kProblems = 4;
  static constexpr int kSearch = 5;
  static constexpr int kCallHierarchy = 6;
  static constexpr int kGit = 7;
  static constexpr int kCoreAnalyzer = 8;
  static constexpr int kBinarySymbols = 9;
  static constexpr int kPacketMonitor = 10;
  static constexpr int kAi = 11;
  int selected_tab = kTerminal;
```

### turn 4 — clarify_pushback (1/3)

Clarify prematuro rechazado. Motivo del modelo: loop: demasiadas respuestas JSON inválidas; ¿reformulas el cambio?

No cierres aún. Emite `action=plan` (más targets) o `action=tools`/`tool` para pedir **más código** (otros stems del ## Ranked map: `get_code_of path:Symbol`, `file_outline`, `search`). Solo tras explorar más puedes usar `done next=clarify` de nuevo (quedan 2 pushbacks).

