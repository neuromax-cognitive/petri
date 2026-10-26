# validation, phase 2

run: results/builtin_s1.csv

measured with the frozen csv contract, 10 hz samples

| metric | model | literature | source | verdict |
|---|---|---|---|---|
| speed_off_food_mm_s | 0.037 | 0.10..0.40 | yemini 2013 behavioral database, roaming state | MISMATCH |
| bend_frequency_hz | 0.000 | 0.50..1.50 | wen 2012 proprioceptive coupling study, crawling on agar | MISMATCH |
| body_length_mm | 1.000 | 0.90..1.20 | adult hermaphrodite, standard | match |
| reversal_duration_s | 39.500 | 0.50..2.50 | rankin and broster 1992, tap response | MISMATCH |
| curvature_amplitude_inv_mm | 0.268 | 1.00..5.00 | order of magnitude from bend radius 0.2 to 0.5 mm, approx | MISMATCH |

diagnostics not in the literature table: net 0.20 mm, tail static bias 3.75 1/mm

1 match, 4 mismatch