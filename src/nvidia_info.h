#include <stdbool.h>
#include <stdio.h>
#include <nvml.h>

extern nvmlReturn_t result;
extern unsigned int nvidiaTemp, processSamplesCount, lastSeenTimeStamp, *vgpuInstanceSamplesCount;
extern nvmlValueType_t *sampleValType;
extern nvmlDevice_t nvidiaDevice;
extern struct nvmlUtilization_st nvidiaUtilization;
extern bool nvmlSuccess;

void checkNvidia(void);
void getNvidiaInfo(void);