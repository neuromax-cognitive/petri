#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sim.h"
#include "plasticity.h"

static void apply_command_bias(sim_t* s)
{
    int i;
    for (i = 0; i < s->ns.n; i++) {
        const char* nm = s->ns.cells[i].name;
        if (strncmp(nm, "AVB", 3) == 0)
            s->ns.cells[i].i_bias = BIAS_AVB_MV;
        else if (strncmp(nm, "AVA", 3) == 0)
            s->ns.cells[i].i_bias = BIAS_AVA_MV;
    }
}

static void collect_named(sim_t* s)
{
    nervous_t* ns = &s->ns;
    int i;
    s->idx_ava = nervous_find_prefix(ns, "AVA");
    s->idx_avb = nervous_find_prefix(ns, "AVB");
    s->idx_aib = nervous_find_prefix(ns, "AIB");
    s->idx_aiy = nervous_find_prefix(ns, "AIY");
    s->idx_rim = nervous_find_prefix(ns, "RIM");
    s->n_ash = s->n_awc = s->n_awa = s->n_ase = s->n_afd = s->n_alplm = 0;
    for (i = 0; i < ns->n; i++) {
        const char* nm = ns->cells[i].name;
        if (strncmp(nm, "ASH", 3) == 0 && s->n_ash < 4)
            s->idx_ash[s->n_ash++] = i;
        else if (strncmp(nm, "AWC", 3) == 0 && s->n_awc < 4)
            s->idx_awc[s->n_awc++] = i;
        else if (strncmp(nm, "AWA", 3) == 0 && s->n_awa < 4)
            s->idx_awa[s->n_awa++] = i;
        else if (strncmp(nm, "ASE", 3) == 0 && s->n_ase < 4)
            s->idx_ase[s->n_ase++] = i;
        else if (strncmp(nm, "AFD", 3) == 0 && s->n_afd < 4)
            s->idx_afd[s->n_afd++] = i;
        else if ((strncmp(nm, "ALM", 3) == 0 || strncmp(nm, "PLM", 3) == 0) && s->n_alplm < 4)
            s->idx_alplm[s->n_alplm++] = i;
    }
}

int sim_init(sim_t* s, const sim_opts_t* o)
{
    memset(s, 0, sizeof(*s));
    s->opts = *o;
    rng_seed(&s->rng, o->seed);
    nervous_init(&s->ns, &s->rng);

    if (o->connectome_path) {
        if (conn_load_csv(&s->conn, &s->ns, o->connectome_path) != 0) {
            fprintf(stderr, "connectome: cannot parse %s\n", o->connectome_path);
            return -1;
        }
        if (!o->quiet)
            fprintf(stderr, "connectome: %d neurons %d synapses from %s\n",
                    s->conn.n_neurons, s->conn.n_syn, o->connectome_path);
    } else {
        if (conn_load_builtin(&s->conn, &s->ns) != 0)
            return -1;
        if (!o->quiet)
            fprintf(stderr, "connectome: builtin %d neurons %d synapses\n",
                    s->conn.n_neurons, s->conn.n_syn);
    }

    body_init(&s->body);
    body_build_motor_map(&s->body, &s->conn, &s->ns);
    if (!o->quiet)
        fprintf(stderr, "motor map: %d somatic motor neurons\n", s->body.mm.n_mn);
    s->g_chem_scale = 1.0f;
    s->g_gap_scale = 1.0f;
    s->tau_scale_global = 1.0f;
    world_init(&s->world, &s->rng, !o->no_food, o->thermo);
    modulators_init(&s->mod);
    metrics_init(&s->metrics);
    plasticity_init(&s->conn, &s->ns);
    collect_named(s);
    if (o->ablate_list) {
        char buf[256];
        char* tok;
        snprintf(buf, sizeof(buf), "%s", o->ablate_list);
        for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
            int id = nervous_find(&s->ns, tok);
            if (id < 0)
                id = nervous_find_prefix(&s->ns, tok);
            if (id >= 0) {
                s->ns.cells[id].ablated = 1;
                fprintf(stderr, "ablation: %s\n", s->ns.cells[id].name);
            } else {
                fprintf(stderr, "ablation: %s not found\n", tok);
            }
        }
    }
    if (o->eval_path) {
        if (sim_apply_genome(s, o->eval_path) != 0)
            return -1;
    }
    apply_command_bias(s);
    return 0;
}

void sim_reset_dynamics(sim_t* s)
{
    /* used by the ablation screen, rewinds state without reloading the graph */
    rng_seed(&s->rng, s->opts.seed);
    nervous_init(&s->ns, &s->rng);
    {
        /* nervous_init clears names, rebuild from connectome table */
        int i;
        for (i = 0; i < s->conn.n_neurons; i++)
            memcpy(s->ns.cells[i].name, s->conn.names[i], NEURON_NAME_MAX);
        s->ns.n = s->conn.n_neurons;
    }
    conn_classify(&s->conn, &s->ns);
    body_init(&s->body);
    body_build_motor_map(&s->body, &s->conn, &s->ns);
    world_init(&s->world, &s->rng, !s->opts.no_food, s->opts.thermo);
    modulators_init(&s->mod);
    metrics_init(&s->metrics);
    plasticity_init(&s->conn, &s->ns);
    s->t_ms = 0.0;
    s->tap_timer_ms = 0.0f;
    s->tap_active_ms = 0;
    if (s->opts.ablate_list) {
        char buf[256];
        char* tok;
        snprintf(buf, sizeof(buf), "%s", s->opts.ablate_list);
        for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
            int id = nervous_find(&s->ns, tok);
            if (id < 0)
                id = nervous_find_prefix(&s->ns, tok);
            if (id >= 0)
                s->ns.cells[id].ablated = 1;
        }
    }
    apply_command_bias(s);
    /* a loaded genome modifies tau_base and w_mult which the rebuild above
       reset to anatomy defaults, re-apply it or eval mode silently degrades */
    if (s->opts.eval_path)
        sim_apply_genome(s, s->opts.eval_path);
}

/*
 * sensory transduction. the worm only ever sees the concentration under its
 * nose, its time derivative, and local sensory adaptation. no gradient
 * vector exists anywhere in the sensory code.
 */
static void sensory_step(sim_t* s)
{
    world_t* w = &s->world;
    body_t* b = &s->body;
    nervous_t* ns = &s->ns;
    float ca, cb, food, temp;
    int i;

    world_sample(w, b->x[0], b->y[0], &ca, &cb, &food, &temp);

    {
        float dca = world_gradient_dc(w, b, 0);
        float dcb = world_gradient_dc(w, b, 1);
        /* response is offset from the adapted baseline plus a derivative term */
        float resp_a = (ca - w->ca_adapt) * 0.5f + CHEMO_DERIV_W * dca * 0.05f;
        float resp_b = (cb - w->cb_adapt) * 0.5f + CHEMO_DERIV_W * dcb * 0.05f;
        for (i = 0; i < s->n_awc; i++)
            ns->cells[s->idx_awc[i]].i_sens = CHEMO_G * resp_a;
        for (i = 0; i < s->n_awa; i++)
            ns->cells[s->idx_awa[i]].i_sens = CHEMO_G * resp_a;
        for (i = 0; i < s->n_ase; i++)
            ns->cells[s->idx_ase[i]].i_sens = CHEMO_G * resp_b;
    }

    if (s->n_afd) {
        float dt_resp = (temp - w->t_adapt) / 1.0f;
        for (i = 0; i < s->n_afd; i++)
            ns->cells[s->idx_afd[i]].i_sens = THERMO_G * dt_resp;
    }

    /* nociception, nose contact feeds ash, an aversive reversal trigger.
       the current follows the adapting trace kept in sim_step, a
       sustained press is one fading pulse, not a held command */
    for (i = 0; i < s->n_ash; i++)
        ns->cells[s->idx_ash[i]].i_sens = MEC_G * s->ash_trace;
    /* conditioning protocol: the paired arm forces ash during the training
       window so ash punishment coincides with the odor pathway, the control
       arm smells the same odor without punishment */
    if (s->opts.conditioning == 1 && s->t_ms > 20000.0 && s->t_ms < 80000.0)
        for (i = 0; i < s->n_ash; i++)
            ns->cells[s->idx_ash[i]].i_sens = MEC_G;

    /* gentle mechanosensation, the receptor current follows the slow
       relaxation trace, habituation comes from stsp at its synapses */
    for (i = 0; i < s->n_alplm; i++)
        ns->cells[s->idx_alplm[i]].i_sens = MEC_G * s->mec_trace;
}

/*
 * chemical and electrical synaptic currents. walks the csr by row so the
 * presynaptic index is known for tsodyks markram update and weight scaling.
 */
static void synaptic_currents(sim_t* s, float dt)
{
    nervous_t* ns = &s->ns;
    connectome_t* c = &s->conn;
    float g_ser = 1.0f + 0.35f * s->mod.ser - 0.15f * s->mod.oct;
    float g_da = 1.0f - DA_G_MECH_SCALE * s->mod.dop;
    int i, j;

    for (i = 0; i < ns->n; i++)
        ns->cells[i].i_chem = ns->cells[i].i_gap = 0.0f;

    for (i = 0; i < ns->n; i++) {
        neuron_t* pre = &ns->cells[i];
        float pre_scale = pre->homeo * pre->hetero;
        for (j = c->row_ptr[i]; j < c->row_ptr[i + 1]; j++) {
            int post = c->col_ind[j];
            neuron_t* pn = &ns->cells[post];
            if (c->kind[j] == SYN_GAP) {
                float g = c->w[j] * c->w_mult[j] * s->g_gap_scale;
                pn->i_gap += g * (pre->v - pn->v) * I_SCALE;
                continue;
            }
            {
                float a = nervous_hist(ns, i, c->delay[j]);
                float u = c->stsp_u[j], x = c->stsp_x[j];
                float tau_rec, tau_fac, ubase;
                float p, g, erev;
                if (c->stsp_mode[j] == STSP_DEPRESSING) {
                    ubase = STSP_U_DEP; tau_rec = STSP_TAU_REC_DEP; tau_fac = STSP_TAU_FAC_DEP;
                } else if (c->stsp_mode[j] == STSP_FACILITATING) {
                    ubase = STSP_U_FAC; tau_rec = STSP_TAU_REC_FAC; tau_fac = STSP_TAU_FAC_FAC;
                } else {
                    ubase = 0.0f; tau_rec = 1.0f; tau_fac = 1.0f;
                }
                if (ubase > 0.0f) {
                    u += dt * (-u / tau_fac + ubase * a * (1.0f - u) / STSP_EVENT_MS);
                    x += dt * ((1.0f - x) / tau_rec - a * u * x / STSP_EVENT_MS);
                    if (u < 0.0f) u = 0.0f;
                    if (u > 1.0f) u = 1.0f;
                    if (x < 0.01f) x = 0.01f;
                    if (x > 1.0f) x = 1.0f;
                    c->stsp_u[j] = u;
                    c->stsp_x[j] = x;
                } else {
                    u = 1.0f;
                    x = 1.0f;
                }
                p = u * x;
                g = c->w[j] * c->w_mult[j] * s->g_chem_scale * pre_scale;
                if (pre->type == NT_SENSORY)
                    g *= g_da;
                g *= g_ser;
                erev = (c->kind[j] == SYN_INH) ? E_REV_INH_MV : E_REV_EXC_MV;
                pn->i_chem += g * p * a * (erev - pn->v) * I_SCALE;
            }
        }
    }
}

void sim_step(sim_t* s)
{
    float dt = DT_MS;
    nervous_t* ns = &s->ns;
    float tau_mod, nmj_gain;

    /* automatic tap train for habituation assays */
    if (s->opts.tap_interval_ms > 0.0f) {
        s->tap_timer_ms += dt;
        if (s->tap_timer_ms >= s->opts.tap_interval_ms) {
            s->tap_timer_ms = 0.0f;
            s->tap_active_ms = (int)TAP_MS;
        }
    }
    if (s->tap_active_ms > 0)
        s->tap_active_ms -= (int)dt;

    /* mechanoreceptors relax slowly, a 300 ms tap drives the circuit for
       about two seconds, giving the reversal loop time to capture the
       wave. the refresh is edge only, a sustained press is one pulse, a
       receptor held under constant pressure goes quiet */
    {
        int contact = s->tap_active_ms > 0 || s->body.nose_contact ||
                      s->body.tail_contact;
        /* receptor envelopes refresh on the contact edge only, a
           sustained press is one fading pulse per channel, otherwise a
           worm wedged into a corner would carry the touch and pain
           commands forever */
        if (contact && !s->prev_contact)
            s->mec_trace = 1.0f;
        if (s->body.nose_contact && !s->prev_nose_contact)
            s->ash_trace = 1.0f;
        if (s->mec_trace > 0.0f) {
            s->mec_trace -= dt / MEC_TAU_MS;
            if (s->mec_trace < 0.0f)
                s->mec_trace = 0.0f;
        }
        if (s->ash_trace > 0.0f) {
            s->ash_trace -= dt / ASH_TAU_MS;
            if (s->ash_trace < 0.0f)
                s->ash_trace = 0.0f;
        }
        s->prev_contact = contact;
        s->prev_nose_contact = s->body.nose_contact;
    }


#ifdef CE_LOOP_DEBUG
    /* temporary loop instrumentation, enabled with -DCE_LOOP_DEBUG only */
    if ((long)(s->t_ms) % 250 == 0 && (long)(s->t_ms) >= 117000) {
        int j;
        fprintf(stderr, "t=%5.0f:", s->t_ms);
        for (j = 0; j < N_SEG; j++)
            fprintf(stderr, "%s%+4.1f", (j % 6) == 5 ? " |" : " ", s->body.k[j]);
        fprintf(stderr, "\n");
    }
    {
        static int db3 = -2, vb3 = -2, avb = -2;
        int j;
        if (db3 == -2) {
            for (j = 0; j < ns->n; j++) {
                if (strncmp(ns->cells[j].name, "DB3", 4) == 0) db3 = j;
                else if (strncmp(ns->cells[j].name, "VB3", 4) == 0) vb3 = j;
                else if (strncmp(ns->cells[j].name, "AVB", 3) == 0 && avb < 0) avb = j;
            }
        }
        if ((long)(s->t_ms) % 250 == 0 && (long)(s->t_ms) >= 117000 && db3 >= 0)
            fprintf(stderr, "  DB3 v=%+6.1f act=%.2f ip=%+5.1f ic=%+5.1f | VB3 v=%+6.1f act=%.2f ip=%+5.1f | AVB act=%.2f\n",
                    ns->cells[db3].v, neuron_act(&ns->cells[db3]), ns->cells[db3].i_prop, ns->cells[db3].i_chem,
                    ns->cells[vb3].v, neuron_act(&ns->cells[vb3]), ns->cells[vb3].i_prop,
                    avb >= 0 ? neuron_act(&ns->cells[avb]) : -1.0f);
    }
#endif

    world_step(&s->world, &s->body, dt);
    sensory_step(s);
    body_proprioception(&s->body, ns);
    synaptic_currents(s, dt);
    plasticity_step(&s->conn, ns, &s->mod, dt);
    modulators_step(&s->mod, &s->world, &s->body, dt);

    /* neuromodulators bend membrane kinetics and junction gain, graded not binary */
    tau_mod = s->tau_scale_global
        * (1.0f + MOD_TAU_SCALE_SER * s->mod.ser - MOD_TAU_SCALE_OCT * s->mod.oct);
    if (tau_mod < 0.3f)
        tau_mod = 0.3f;
    nmj_gain = NMJ_GAIN / (1.0f + NMJ_GAIN_SER * s->mod.ser) * (1.0f + NMJ_GAIN_OCT * s->mod.oct);

    nervous_step(ns, dt, tau_mod, &s->rng);
    nervous_push_history(ns);
    body_step(&s->body, ns, nmj_gain, dt);
    metrics_step(&s->metrics, &s->body, ns, &s->mod, &s->world, dt);
    s->t_ms += dt;
}

int sim_run(sim_t* s, void (*frame_cb)(sim_t*, void*), void* cb_arg)
{
    long n_steps = (long)(s->opts.duration_s * 1000.0 / DT_MS);
    long i;
    long csv_every = 100;   /* one row per 100 ms */

    if (!s->opts.quiet && s->opts.headless)
        metrics_csv_header(stdout);

    for (i = 0; i < n_steps; i++) {
        sim_step(s);
        if (frame_cb && (i % 40 == 0))
            frame_cb(s, cb_arg);
        if (s->opts.headless && !s->opts.quiet && (i % csv_every == 0))
            metrics_csv_row(stdout, &s->metrics, &s->body, &s->ns, &s->mod);
    }
    return 0;
}

double sim_fitness(const sim_t* s)
{
    /* chemotaxis performance plus net approach to the attractive source */
    double d0 = hypot((double)(6.0 - ODOR_A_X_MM), (double)(8.0 - ODOR_A_Y_MM));
    double d1 = hypot((double)(s->body.x[0] - ODOR_A_X_MM),
                      (double)(s->body.y[0] - ODOR_A_Y_MM));
    double approach = (d0 - d1) / d0;
    return (double)s->metrics.chemotaxis_index * 100.0 + 50.0 * approach
        + 0.1 * (double)s->metrics.speed_ema;
}

/*
 * minimal genome json, flat scalars plus a per neuron block
 * {"tau_scale":1.0,"w_chem":1.0,"w_gap":1.0,
 *  "neuron":{"AVAL":{"tau":1.2,"w_out":0.8}}}
 */
static double json_num(const char* js, const char* key, double dflt)
{
    char pat[64];
    const char* p;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(js, pat);
    if (!p)
        return dflt;
    p = strchr(p + strlen(pat), ':');
    if (!p)
        return dflt;
    return strtod(p + 1, NULL);
}

int sim_apply_genome(sim_t* s, const char* path)
{
    static char buf[65536];
    FILE* f = fopen(path, "r");
    const char* np;
    size_t n;
    if (!f) {
        fprintf(stderr, "genome: cannot open %s\n", path);
        return -1;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    s->g_chem_scale = (float)json_num(buf, "w_chem", 1.0);
    s->g_gap_scale = (float)json_num(buf, "w_gap", 1.0);
    s->tau_scale_global = (float)json_num(buf, "tau_scale", 1.0);

    /* per neuron tau and outgoing weight multipliers */
    np = strstr(buf, "\"neuron\"");
    if (np) {
        char name[NEURON_NAME_MAX];
        const char* p = strchr(np, '{');
        const char* end = strrchr(np, '}');
        if (!p || !end)
            return 0;
        while (p < end) {
            const char* q;
            int len;
            p = strchr(p + 1, '"');
            if (!p || p >= end)
                break;
            q = strchr(p + 1, '"');
            if (!q)
                break;
            len = (int)(q - p - 1);
            if (len <= 0 || len >= NEURON_NAME_MAX) {
                p = q;
                continue;
            }
            memcpy(name, p + 1, (size_t)len);
            name[len] = '\0';
            {
                int id = nervous_find(&s->ns, name);
                if (id < 0) {
                    id = nervous_find_prefix(&s->ns, name);
                }
                if (id >= 0) {
                    /* scan the object body that follows this name */
                    const char* obj = strchr(q, '{');
                    const char* oend = strchr(q, '}');
                    if (obj && oend && obj < oend) {
                        static char ob[512];
                        size_t ol = (size_t)(oend - obj + 1);
                        if (ol >= sizeof(ob))
                            ol = sizeof(ob) - 1;
                        memcpy(ob, obj, ol);
                        ob[ol] = '\0';
                        s->ns.cells[id].tau_base *= (float)json_num(ob, "tau", 1.0);
                        {
                            float wo = (float)json_num(ob, "w_out", 1.0);
                            int j;
                            for (j = s->conn.row_ptr[id]; j < s->conn.row_ptr[id + 1]; j++)
                                s->conn.w_mult[j] *= wo;
                        }
                        p = oend;
                    }
                }
            }
            p = q;
        }
    }
    return 0;
}

/*
 * ablation screen, hyperpolarize each neuron to v_rest in turn and measure
 * the effect on baseline locomotion versus the intact baseline
 */
int sim_ablate_screen(sim_t* s, double per_neuron_s)
{
    int i;
    float base_speed, base_bendhz;
    long base_bends;
    double base_t;
    s->opts.duration_s = per_neuron_s;
    s->opts.quiet = 1;

    sim_reset_dynamics(s);
    sim_run(s, NULL, NULL);
    base_speed = s->metrics.speed_ema;
    base_bends = s->body.bend_count;
    base_t = s->metrics.t_s;
    base_bendhz = base_t > 0.0 ? (float)(base_bends / 2.0 / base_t) : 0.0f;

    fprintf(stdout, "neuron,type,spd_ratio,bend_ratio,rev_count,ci\n");
    for (i = 0; i < s->ns.n; i++) {
        float spd, bhz;
        int j;
        sim_reset_dynamics(s);
        for (j = 0; j < s->ns.n; j++)
            s->ns.cells[j].ablated = (j == i) ? 1 : 0;
        sim_run(s, NULL, NULL);
        spd = s->metrics.speed_ema;
        bhz = s->metrics.t_s > 0.0
            ? (float)(s->body.bend_count / 2.0 / s->metrics.t_s) : 0.0f;
        fprintf(stdout, "%s,%d,%.3f,%.3f,%ld,%.3f\n",
                s->ns.cells[i].name,
                s->ns.cells[i].type,
                base_speed != 0.0f ? spd / base_speed : 0.0f,
                base_bendhz != 0.0f ? bhz / base_bendhz : 0.0f,
                s->metrics.n_reversals,
                s->metrics.chemotaxis_index);
    }
    return 0;
}
