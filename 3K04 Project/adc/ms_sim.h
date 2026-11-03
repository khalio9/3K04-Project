/* ms_sim.h - Simulated Monitoring System (demo and tests only)
 *
 * Stands in for Part B's Monitoring System so the TAs can see communication
 * loss and recovery without a real network. It plugs into CommManager as its
 * transport, exactly where the real network link would go.
 * Owner: Omer
 */
#ifndef ADC_MS_SIM_H
#define ADC_MS_SIM_H

#include <stdbool.h>
#include "types.h"

void ms_sim_init(const char *adc_id, NowMsFn now_ms);

/* The transport function given to comm_init(). Drops everything while the link is cut. */
int  ms_sim_transport(const char *bytes, int len);

/* Demo control: cut or restore the network link. */
void ms_sim_set_link(bool up);
bool ms_sim_link_up(void);

/* Delivers the simulated MS's replies (heartbeat replies, ACKs) to comm_receive().
 * Call once per tick, after comm_tick(). */
void ms_sim_pump(void);

/* Demo control: the MS sends a permission update to the ADC. */
void ms_sim_send_permission_update(const char *body);

/* What the MS has received so far (for the demo screen). */
int  ms_sim_received_count(void);
const char *ms_sim_last_received(void);   /* e.g. "ALARM seq=14 type=LOCK_FAILURE" */

#endif