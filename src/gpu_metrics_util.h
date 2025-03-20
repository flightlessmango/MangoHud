#pragma once

struct gpu_metrics {
    int load;
    int temp;
    int junction_temp {-1};
    int memory_temp {-1};
    float memoryUsed;
    float memoryTotal;
    int MemClock;
    int CoreClock;
    float powerUsage;
    float powerLimit;
    float apu_cpu_power;
    int apu_cpu_temp;
    bool is_power_throttled;
    bool is_current_throttled;
    bool is_temp_throttled;
    bool is_other_throttled;
    float gtt_used;
    int fan_speed;
    int voltage;
    bool fan_rpm;

    gpu_metrics()
        : load(0), temp(0), junction_temp(0), memory_temp(0),
          memoryUsed(0.0f), memoryTotal(0.0f), MemClock(0), CoreClock(0),
          powerUsage(0.0f), powerLimit(0.0f), apu_cpu_power(0.0f), apu_cpu_temp(0),
          is_power_throttled(false), is_current_throttled(false),
          is_temp_throttled(false), is_other_throttled(false),
          gtt_used(0.0f), fan_speed(0), voltage(0), fan_rpm(false) {}
};

#define METRICS_UPDATE_PERIOD_MS 500
#define METRICS_POLLING_PERIOD_MS 25
#define METRICS_SAMPLE_COUNT (METRICS_UPDATE_PERIOD_MS/METRICS_POLLING_PERIOD_MS)

#define GPU_UPDATE_METRIC_AVERAGE(FIELD) do { int value_sum = 0; for (size_t s=0; s < METRICS_SAMPLE_COUNT; s++) { value_sum += metrics_buffer[s].FIELD; } metrics.FIELD = value_sum / METRICS_SAMPLE_COUNT; } while(0)
#define GPU_UPDATE_METRIC_AVERAGE_FLOAT(FIELD) do { float value_sum = 0; for (size_t s=0; s < METRICS_SAMPLE_COUNT; s++) { value_sum += metrics_buffer[s].FIELD; } metrics.FIELD = value_sum / METRICS_SAMPLE_COUNT; } while(0)
#define GPU_UPDATE_METRIC_MAX(FIELD) do { int cur_max = metrics_buffer[0].FIELD; for (size_t s=1; s < METRICS_SAMPLE_COUNT; s++) { cur_max = MAX(cur_max, metrics_buffer[s].FIELD); }; metrics.FIELD = cur_max; } while(0)
#define GPU_UPDATE_METRIC_LAST(FIELD) do { metrics.FIELD = metrics_buffer[METRICS_SAMPLE_COUNT - 1].FIELD; } while(0)

class Throttling {
	public:
		std::vector<float> power;
		std::vector<float> thermal;
		int64_t indep_throttle_status;
        uint32_t vendor_id;

		Throttling(uint32_t vendor_id)
			: power(200, 0.0f),
			thermal(200, 0.0f), vendor_id(vendor_id) {}

		void update(){
            if (vendor_id == 0x1002) {
                if (((indep_throttle_status >> 0) & 0xFF) != 0)
                    power.push_back(0.1);
                else
                    power.push_back(0);


                if (((indep_throttle_status >> 32) & 0xFFFF) != 0)
                    thermal.push_back(0.1);
                else
                    thermal.push_back(0);

            } else if (vendor_id == 0x10de) {
                if ((indep_throttle_status & 0x000000000000008CLL) != 0)
                    power.push_back(0.1);
                else
                    power.push_back(0);

                if ((indep_throttle_status & 0x0000000000000060LL) != 0)
                    thermal.push_back(0.1);
                else
                    thermal.push_back(0);
            }

			power.erase(power.begin());
			thermal.erase(thermal.begin());
		}

		bool power_throttling(){
			return std::find(power.begin(), power.end(), 0.1f) != power.end();
		}

		bool thermal_throttling(){
			return std::find(thermal.begin(), thermal.end(), 0.1f) != thermal.end();
		}
};