#ifndef CE_SIM_H
#define CE_SIM_H

#include <stdint.h>
#include "config.h"
#include "connectome.h"
#include "body.h"
#include "world.h"
#include "modulators.h"
#include "metrics.h"

#define MAX_ABLATE 32

typedef struct {
    double duration_s;
    uint64_t seed;
    const char* connectome_path;
    const char* eval_path;
    const char* ablate_list;
    int headless;
    int quiet;
    int ablate_screen;
    int no_food;
    int thermo;
    float tap_interval_ms;   /* 0 disables automatic taps */
    int evolve_gens;         /* number of generations for --evolve mode */
    int population;          /* evolution population size, 0 means default */
    int evolve_fitness_net;  /* 1 scores net displacement instead of path length */
    const char* evolve_output; /* output path for best genome */
} sim_opts_t;

typedef struct {
    sim_opts_t opts;
    nervous_t  ns;
    connectome_t conn;
    body_t     body;
    world_t    world;
    modulators_t mod;
    metrics_t  metrics;
    rng_t      rng;
    int   idx_ava, idx_avb, idx_aib, idx_aiy, idx_rim;
    int   idx_ash[4]; int n_ash;
    int   idx_awc[4]; int n_awc;
    int   idx_alplm[4]; int n_alplm;
    int   idx_awa[4]; int n_awa;
    int   idx_ase[4]; int n_ase;
    int   idx_afd[4]; int n_afd;
    double t_ms;
    float  tap_timer_ms;
    int    tap_active_ms;
    float  mec_trace;        /* decaying mechanoreceptor activation 0..1 */
    float  g_chem_scale;    /* genome knobs, eval mode */
    float  g_gap_scale;
    float  tau_scale_global;
} sim_t;

int    sim_init(sim_t* s, const sim_opts_t* o);
void   sim_reset_dynamics(sim_t* s);
void   sim_step(sim_t* s);
double sim_fitness(const sim_t* s);
int    sim_apply_genome(sim_t* s, const char* path);
int    sim_run(sim_t* s, void (*frame_cb)(sim_t*, void*), void* cb_arg);
int    sim_ablate_screen(sim_t* s, double per_neuron_s);

#endif
