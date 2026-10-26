# calibration log, phase 1

objective: net displacement + 40 x wave coherence - 8 x static curl, 30 s runs, seed 1, builtin connectome

| step | score | net | corr | amp | curl |
|---|---|---|---|---|---|
| baseline | 15.04 | 0.12 | -0.93 | 0.42 | 2.81 |
| PROP_GAIN=4.0 | 17.60 | 0.11 | -0.93 | 0.39 | 2.44 |
| PROP_BACK_W=0.8 | 20.73 | 0.08 | -0.89 | 0.38 | 1.88 |
| PROP_TAU_MS=250.0 | 21.16 | 0.08 | -0.9 | 0.37 | 1.88 |
| NMJ_GAIN=0.5 | 27.22 | 0.05 | -0.86 | 0.18 | 0.91 |
| BIAS_AVB_MV=2.0 | 27.27 | 0.05 | -0.86 | 0.19 | 0.9 |
| NOISE_SD_MV=0.2 | 31.13 | 0.05 | -0.95 | 0.17 | 0.89 |
| PROP_GAIN=8.0 | 31.13 | 0.05 | -0.95 | 0.17 | 0.89 |
| PROP_BACK_W=1.2 | 31.13 | 0.05 | -0.95 | 0.17 | 0.89 |
| PROP_TAU_MS=500.0 | 31.13 | 0.05 | -0.95 | 0.17 | 0.89 |
| NMJ_GAIN=0.9 | 31.13 | 0.05 | -0.95 | 0.17 | 0.89 |
| BIAS_AVB_MV=4.0 | 31.13 | 0.05 | -0.95 | 0.17 | 0.89 |
| NOISE_SD_MV=0.4 | 31.13 | 0.05 | -0.95 | 0.17 | 0.89 |
