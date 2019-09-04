#ifndef LRTR_UTILS_VRF_H
#define LRTR_UTILS_VRF_H

void vrf_netns_init(void);
int vrf_netns_api_usable(const char *name);
int vrf_netns_switchback(void);
int vrf_netns_switch_to(const char *name);
int vrf_netns_socket(int domain, int type, int protocol, const char *name);
void vrf_netns_activate_support(void);
#endif
