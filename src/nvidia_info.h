#include <nvml.h>
nvmlReturn_t result;
unsigned int nvidiaTemp, processSamplesCount, lastSeenTimeStamp, *vgpuInstanceSamplesCount;
nvmlValueType_t *sampleValType;
nvmlDevice_t nvidiaDevice;
struct nvmlUtilization_st nvidiaUtilization;

void checkNvidia(void);
