#ifndef NETWORK_TASK_H
#define NETWORK_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

bool wifi_init_sta(void);
void vNetworkTask(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_TASK_H
