# benchmark note draft: behavioral divergence of c. elegans connectome reconstructions

status: protocol paper, results preliminary. the locomotion of the model
is below the literature range (see validation.md), so the numbers below
compare the connectomes in a regime where behavior is dominated by
circuit structure rather than by calibrated dynamics. treat this as the
harness description, not as a finding about the animal.

## question

three published reconstructions of the same animal differ in coverage
and linkage: wormatlas NeuronConnect (279 somatic neurons), the openworm
hermaphrodite edge list (302 neurons with pharynx) and the witvliet 2021
adult volume. do they produce different behavior in the same body under
the same physics?

## protocol

one binary, one body, one parameter set, three connectome files.
120 s headless runs, seed 1, frozen csv contract. metrics: net
displacement, mean speed, undulation amplitude, static curl.

## current results

see results/summary.csv. at the time of writing all three connectomes
produce net displacement in the 0.2 to 0.4 mm band per 120 s, below the
literature range, so no divergence claim can be made yet. the
infrastructure produces the comparison in one command (run_all.sh) and
will re-emit the table after calibration phase 1.

## what would make this publishable

1. calibrated locomotion inside the literature range on at least one
   connectome (phase 1)
2. the comparison table with 10 seeds per connectome and a permutation
   test on the metric distributions (phase 3)
3. ablation matrices per connectome, compared cell by cell (phase 3)
