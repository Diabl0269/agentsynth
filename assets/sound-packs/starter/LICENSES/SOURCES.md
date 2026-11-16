# Sources for the Starter instruments pack

All recordings are public domain (CC0 1.0). The SFZ files, trimming, loops, level matching and FLAC encoding were made for AgentSynth; the original libraries ship their own SFZ and scripts, which are not included. These three instruments are the same files as in the downloadable Core instruments pack.

Licence texts: `VSCO-2-CE-LICENSE-CC0.txt`, `Virtuosity-Drums-LICENSE-CC0.txt`.

## French horn (`french-horn`)

- Source pack: VSCO 2 Community Edition (Versilian Studios)
- URL: https://github.com/sgossner/VSCO-2-CE
- Source folder(s): `Brass/F Horn/sus`
- Files used (18): MOHorn_sus_A#1_v1_1.wav, MOHorn_sus_A#1_v3_1.wav, MOHorn_sus_A2_v1_1.wav, MOHorn_sus_A2_v3_1.wav, MOHorn_sus_C1_v1_1.wav, MOHorn_sus_C1_v3_1.wav, MOHorn_sus_C3_v1_1.wav, MOHorn_sus_C3_v3_1.wav, MOHorn_sus_D#1_v1_1.wav, MOHorn_sus_D#1_v3_1.wav, MOHorn_sus_D2_v1_1.wav, MOHorn_sus_D2_v3_1.wav, MOHorn_sus_D4_v1_1.wav, MOHorn_sus_F2_v1_1.wav, MOHorn_sus_F2_v3_1.wav, MOHorn_sus_F4_v1_1.wav, MOHorn_sus_G1_v1_1.wav, MOHorn_sus_G1_v3_1.wav
- Changes: sustain articulation, 2 layers (single layer where the source has one), mono, sustain loops with a baked crossfade, per-note level matching, 16-bit FLAC.

## Plucked double bass (`double-bass-plucked`)

- Source pack: VSCO 2 Community Edition (Versilian Studios)
- URL: https://github.com/sgossner/VSCO-2-CE
- Source folder(s): `Strings/Solo Contrabass/Pizz`
- Files used (10): BKCtbss_Pizz_A#0_v1_rr1.wav, BKCtbss_Pizz_A1_v1_rr1.wav, BKCtbss_Pizz_B2_v1_rr1.wav, BKCtbss_Pizz_C#2_v1_rr1.wav, BKCtbss_Pizz_D1_v1_rr1.wav, BKCtbss_Pizz_E0_v1_rr1.wav, BKCtbss_Pizz_E2_v1_rr1.wav, BKCtbss_Pizz_F#1_v1_rr1.wav, BKCtbss_Pizz_G#2_v1_rr1.wav, BKCtbss_Pizz_G0_v1_rr1.wav
- Changes: pizzicato, softer layer (v1) and round robin 1 only, mono, silence trimmed, cut at 3 s with a 300 ms fade, one gain for the whole instrument, 16-bit FLAC.

## Acoustic drum kit (`acoustic-kit`)

- Source pack: Virtuosity Drums (Versilian Studios)
- URL: https://github.com/sfzinstruments/virtuosity_drums
- Source folder(s): `repo/Samples/mid/crash`, `repo/Samples/mid/hh`, `repo/Samples/mid/htom`, `repo/Samples/mid/kick`, `repo/Samples/mid/ltom`, `repo/Samples/mid/ride`, `repo/Samples/mid/snare`
- Files used (38): mid_crash_crash_vl2_rr1.flac, mid_crash_crash_vl3_rr1.flac, mid_crash_sizzle_vl2_rr1.flac, mid_crash_sizzle_vl3_rr1.flac, mid_hh_closed_vl2_rr1.flac, mid_hh_closed_vl2_rr2.flac, mid_hh_closed_vl3_rr1.flac, mid_hh_closed_vl3_rr2.flac, mid_hh_closed_vl4_rr1.flac, mid_hh_closed_vl4_rr2.flac, mid_hh_open_vl2_rr1.flac, mid_hh_open_vl4_rr1.flac, mid_hh_pedal_vl2_rr1.flac, mid_hh_pedal_vl3_rr1.flac, mid_htom_center_vl14.flac, mid_htom_center_vl8.flac, mid_kick_snon_vl2_rr1.flac, mid_kick_snon_vl2_rr2.flac, mid_kick_snon_vl3_rr1.flac, mid_kick_snon_vl3_rr2.flac, mid_kick_snon_vl4_rr1.flac, mid_kick_snon_vl4_rr2.flac, mid_ltom_center_vl14.flac, mid_ltom_center_vl8.flac, mid_ride_bell_vl2_rr1.flac, mid_ride_bell_vl3_rr1.flac, mid_ride_ride_vl2_rr1.flac, mid_ride_ride_vl3_rr1.flac, mid_snare_center_vl11.flac, mid_snare_center_vl23.flac, mid_snare_center_vl34.flac, mid_snare_crossstick_vl14.flac, mid_snare_crossstick_vl8.flac, mid_snare_offcenter_vl11.flac, mid_snare_offcenter_vl23.flac, mid_snare_offcenter_vl34.flac, mid_snare_rimshot_vl11.flac, mid_snare_rimshot_vl6.flac
- Changes: mid-microphone mix only (stereo kept); GM key map written by us; toms 43/45/47/50 reuse the low/high tom recordings pitched with tune (cents), 35 maps to the same kick recordings as 36; 2 round robins for kick, snare, closed hat (snare rr2 is the off-center hit), 2 or 3 velocity layers; start silence trimmed; tails cut at 0.8 s (pedal hat) to 4 s (cymbals) with a 200 ms fade; one gain for the whole kit; 16-bit FLAC. Hi-hat choking (open hat cut by closed/pedal hat) is not available in the supported SFZ subset.

