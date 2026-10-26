# elegans

whole animal c. elegans locomotion simulator. behavior emerges exclusively from
connectome dynamics, biomechanics and sensory streams. no behavioral scripts,
no state machines.

c99, libc + libm only, posix termios for the interactive mode. no ncurses.

## build

    make            # -std=c99 -Wall -Wextra -pedantic -O3 -march=native
    make debug      # -O0 -fsanitize=address,undefined
    make clean

## modes

interactive (requires a real tty):

    ./elegans

keys: q quit, p pause, t tap, +/- simulation speed. the render loop is ansi
only, termios raw mode, non blocking buffered stdin drain.

headless batch, csv (10 Hz) to stdout:

    ./elegans --headless --duration 600 --seed 7

genome evaluation (applies mutations, prints fitness):

    ./elegans --eval best_genome.json --duration 1000

evolutionary search, population genetics over connectome weight scales:

    ./elegans --evolve 50 --population 20 --duration 300 \
        --evolve-output best_genome.json --seed 42

fitness is the head path length (odometer, mm) over the evaluation run, all
individuals of one generation are scored on the same noise realization.
per generation log on stderr shows fitness and net displacement per
individual; the net column separates real crawling from undulation in place.

single neuron ablation screen, csv to stdout:

    ./elegans --ablate-screen --duration 60

columns: neuron, type, spd_ratio, bend_ratio, rev_count, ci. ratios are
relative to the intact baseline run.

duration defaults when -d is not given: 180 s plain, 300 s per eval in
--evolve, 60 s per neuron in --ablate-screen, 1000 s semantics via explicit
-d for --eval.

## connectome

three connectomes ship with the repo, switch with one flag:

    ./elegans                                   builtin, 75 neurons / 295 synapses
    ./elegans --connectome varshney_full.csv    wormatlas sheet, 279 somatic neurons
    ./elegans --connectome herm_302.csv         full hermaphrodite, 302 neurons

herm_302.csv comes from the openworm herm_full_edgelist (openworm/c302,
data/herm_full_edgelist.csv) and covers the complete animal: all 302 neurons
including the pharyngeal nervous system, plus 145 muscle and pharyngeal
muscle cells carried as passive chemical targets. 6036 synapses after
deduplication, 58 somatic motor neurons in the motor map. regenerate with
tools/convert_herm_full.py. note the openworm naming uses db01 style motor
names and dbwml01 style muscle names, the loader upper cases everything so
the motor map requires a digit right after the class letters to tell them
apart.

the builtin 75 neuron net covers the appetitive chemotaxis path
(awa, awc -> aia, aiy), nociception (ash -> ava), touch (alm, plm -> avd, pvc),
command pools (ava, avb with endogenous avb bias) and 36 somatic motor neurons
(db, vb, da, va, dd, vd x 6 positions) with electrotonic chains.

external connectomes via csv, one edge per line, optional header:

    Source,Target,Type,Weight[,Sign]
    ADAL,ADAR,S,3
    ADAL,ADAR,EJ,2

type: S / Sp / Rp / R chemical, EJ electrical, Nmj skipped (junctions are
rebuilt from the motor map). synaptic sign comes from the optional 5th column
(-1) or from the gabaergic presynaptic list (dd, vd, ris, rme, avl, dvb, rig).

a ready converted file is included: varshney_full.csv, 279 somatic neurons,
3092 synapses, 58 somatic motor neurons, sourced from the canonical wormatlas
NeuronConnect sheet (openworm/c302, data/NeuronConnectFormatted.xlsx). to
regenerate: convert that xlsx to Source,Target,Type,Weight rows, one edge per
line, types kept as is. pharyngeal neurons are not in the wormatlas sheet,
use herm_302.csv for the complete animal.

the witvliet 2021 developmental connectomes (8 em volumes, dwitvliet/nature2021,
data/nemanode) convert with tools/convert_witvliet.py:

    python3 tools/convert_witvliet.py witvliet_2020_8.json witvliet_adult.csv
    ./elegans --headless --connectome witvliet_adult.csv

typ 0 is chemical, typ 2 is a gap junction, one json entry aggregates the
synapse sizes of one directed pair. limitation: in that export the ventral
cord motor neurons are not individually identified, so the motor map finds
almost nothing and the animal does not crawl on that file. the dataset is
kept for future work, the wormatlas sheet stays the primary full connectome.

wormatlas lists every chemical synapse twice, once per partner: s/sp rows run
sender to receiver, r/rp rows run receiver to sender. the loader flips r/rp
rows and drops flipped copies of synapses already covered by a send row; gap
junctions are deduplicated to one edge per pair. weights are anatomical
synapse counts from the white 1986 em series, not functional conductances:
no dataset records release probability or receptor density, so the mapping
from anatomy to effective strength is closed by evolution (--evolve) or hand
tuning. synaptic sign is inferred from the gabaergic presynaptic list, the
only sign information available in em data.

## genome json

    {
      "w_chem": 1.0, "w_gap": 1.0, "tau_scale": 1.0,
      "neuron": {
        "N0": {"tau": 1.2, "w_out": 0.8}
      }
    }

flat scalars scale all chemical / electrical conductances and membrane taus.
per neuron entries use builtin connectome indices (N<index>); with a csv
connectome the index mapping follows load order, check the neuron list before
reusing a genome across connectomes.

## architecture

    src/main.c        getopt, termios, ansi render, modes
    src/sim.c         orchestrator: sensory, synaptic currents, stsp, genome
    src/connectome.c  csr graph, builtin wiring, csv import
    src/neuron.c      graded membrane, semi implicit euler, noise
    src/body.c        24 segment hydrostatic skeleton, proprioceptive wave,
                      quasi static force balance with anisotropic drag
    src/world.c       agar plate, odor diffusion, lawn, temperature
    src/modulators.c  5-ht, octopamine, dopamine kinetics
    src/plasticity.c  homeostatic and heterosynaptic plasticity
    src/evolution.c   ga over weight scales, fitness = odometer
    src/metrics.c     speed, reversals, omega turns, chemotaxis index, csv
    src/rng.c         xorshift64*, deterministic per seed

## biophysics

graded potentials, no spikes: tau dv/dt = -(v - v_rest) + i_syn + i_gap +
i_prop + i_sens + i_mod + noise, semi implicit euler, dt 1 ms.

chemical synapses: delayed presynaptic activation ring (1 to 3 ms), sigmoid
transmitter release, tsodyks markram short term plasticity, depressing on
mechanosensory endings (tap habituation), facilitating on interneurons.
gap junctions: current proportional to potential difference.

proprioceptive pattern generation: b class motor neurons sense the bend
contrast (k[src] - k[own]) a few segments toward the head, a class toward the
tail. the spatial mean cancels, the traveling contrast regenerates itself at
the right phase, a wave emerges with no cpg neuron. d class cross inhibition
prevents co contraction.

body: 24 segments, curvature from the dorsal ventral muscle imbalance with
viscoelastic cuticle relaxation, motion from a 3x3 quasi static force balance
at zero reynolds number with anisotropic drag (c_n/c_t = 36, gray and
lissmann). the shape velocity is measured in the frame carried with the body,
the frame rotation of the previous step is rotated out before differencing.

sensory: chemo response is the offset from the adapted baseline plus dc/dt,
no gradient vector anywhere. thermo compares to the cultivation temperature.
nose contact feeds ash (reversal), touch feeds alm/plm -> avd/pvc.

neuromodulation: serotonin rises on food and slows taus plus nmj gain,
octopamine rises off food and does the opposite, dopamine shunts
mechanosensory synapses on contact. graded, no mode switches.

## limitations

- fitness = path length rewards undulation in place; watch the net column or
  switch fitness to net displacement if forward locomotion is the goal
- genome json keys are connectome load order indices, not neuron names
- chemo adaptation is a single shared baseline, no bilateral receptor pair
- interactive mode expects a terminal at least ~90 columns wide
- tap and nose touch circuits respond (measurable shift in command pool
  activity) but the sustained reversal capture does not reliably trigger on
  the builtin parameter set: the running wave self-drives the b pools at
  about the same level the touch path drives the a pools. candidates for
  closing the gap: evolution over the command weights (--evolve), or a
  longer mechanoreceptor relaxation constant
- the cpg was redesigned around a stretch reflex oscillator with a
  headward copy (tools/cpg_scan.py, design scan): the wave now travels
  head to tail with a measured head lead of 0.2 s and wavelength near one
  body length (head tail corr -0.77, builtin net, seed 1). architecture
  follows the proprioceptive wave mechanism of wen 2012 and kunert 2017,
  arrived at independently through the scan. the ava gating of a class
  proprioception is our assumption, not literature.
  open gaps:
  amplitude campaign closed with the static curl identified but not fixed.
  measured at 120 s, seed 1: the body holds a standing profile, head mean
  -2.3 1/mm ventral, tail mean +3.8 dorsal, with the traveling wave as a
  +-0.5 ripple on top, net 0.3 mm per 120 s. excluded causes: homeostatic
  drift (homeo tau 60 s to 600 s, no change), boundary free end mode
  (boundary damping, no change). remaining suspects: the copy chain holds
  a self consistent exponential profile (per hop attenuation 0.89) and
  something at the head seeds it. next ideas: separate filter paths for
  the copy and the self term, or head region proprioception shaping