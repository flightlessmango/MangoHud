#include <stdbool.h>
#include <stdio.h>
#include <nvml.h>

nvmlReturn_t result;
unsigned int nvidiaTemp, processSamplesCount, lastSeenTimeStamp, *vgpuInstanceSamplesCount;
nvmlValueType_t *sampleValType;
nvmlDevice_t nvidiaDevice;
struct nvmlUtilization_st nvidiaUtilization;
bool nvmlSuccess;

void checkNvidia(void);
void getNvidiaInfo(void);