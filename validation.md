# validation, phase 2

run: results/builtin_s1.csv

measured with the frozen csv contract, 10 hz samples

| metric | model | literature | source | verdict |
|---|---|---|---|---|
| speed_off_food_mm_s | 0.231 | 0.10..0.40 | yemini 2013 behavioral database, roaming state | match |
| bend_frequency_hz | 1.030 | 0.50..1.50 | wen 2012 proprioceptive coupling study, crawling on agar | match |
| body_length_mm | 1.000 | 0.90..1.20 | adult hermaphrodite, standard | match |
| reversal_duration_s | 1.100 | 0.50..2.50 | rankin and broster 1992, tap response | match |
| curvature_amplitude_inv_mm | 2.386 | 1.00..5.00 | order of magnitude from bend radius 0.2 to 0.5 mm, approx | match |

diagnostics not in the literature table: net 7.05 mm, tail static bias -0.00 1/mm

5 match, 0 mismatch