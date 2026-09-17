#include <spdlog/spdlog.h>
#include <filesystem.h>
#include "battery.h"

namespace fs = ghc::filesystem;
using namespace std;

void BatteryStats::numBattery() {
    int batteryCount = 0;
    
    if (fs::exists("/sys/class/power_supply/")) {
    	fs::path path("/sys/class/power_supply/");
    	for (auto& p : fs::directory_iterator(path)) {
    	    if ( ( fs::exists(p.path().string() + "/charge_now") && fs::exists(p.path().string() + "/charge_full") ) || ( fs::exists(p.path().string() + "/energy_now") && fs::exists(p.path().string() + "/energy_full")) ) {
    	    	//Test if path contains the stuff we use if it is a battery
    	        battPath[batteryCount] = p.path();
    	        
    	        std::istringstream iss((std::stringstream{} << std::ifstream((p.path().string() + "/charge_now")).rdbuf()).str());
    	        if (int a; iss >> a) {
    	        	battType[batteryCount] = "/charge";
    	        } else {
    	        	std::istringstream iss2((std::stringstream{} << std::ifstream((p.path().string() + "/energy_now")).rdbuf()).str());
    	        	if (int b; iss2 >> b) {
    	        		battType[batteryCount] = "/energy";
    	        	} else {
    	        		battType[batteryCount] = "/none";
    	        	}
    	        }
    	 
    	        batteryCount += 1;
    	    }
    	}
    	batt_count = batteryCount;
    	batt_check = true;
    }
}

void BatteryStats::update() {
    if (!batt_check) {
        numBattery();
        if (batt_count == 0) {
            SPDLOG_ERROR("No battery found");
        }
    }

     if (batt_count > 0) {
        current_watt = getPower();
        current_percent = getPercent();
        remaining_time = getTimeRemaining();
    }
}

float BatteryStats::getPercent() {
    float charge_n = 0;
    float charge_f = 0;
    for(int i = 0; i < batt_count; i++) {
        string syspath = battPath[i];
        
		if ((battType[i] == "/charge") | (battType[i] == "/energy")) {
			std::ifstream input(syspath + battType[i] + "_now");
		    std::string line;
			if(std::getline(input, line)) {
				charge_n += (stof(line) / 1000000);
			}
			
			std::ifstream input2(syspath + battType[i] + "_full");
			if(std::getline(input2, line)) {
			    charge_f += (stof(line) / 1000000);
			}
		} else {
            // using /sys/class/power_supply/BAT*/capacity
            // No way to get an accurate reading just average the percents if mutiple batteries
            std::ifstream input(syspath + "/capacity");
            std::string line;
            if(std::getline(input, line)) {
                charge_n += stof(line) / 100;
                charge_f = batt_count;
            }
        }
    }
    return (charge_n / charge_f) * 100;
}

float BatteryStats::getPower() {
    float power_w = 0.0f;

    for (int i = 0; i < batt_count; i++) {
        string syspath = battPath[i];
        string current_now = syspath + "/current_now";
        string voltage_now = syspath + "/voltage_now";
        string power_now = syspath + "/power_now";
        string status = syspath + "/status";

        {
            std::ifstream input(status);
            std::string line;
            if (std::getline(input, line)) {
                current_status = line;
                state[i] = current_status;
            }
        }

        if (state[i] == "Charging" || state[i] == "Unknown" || state[i] == "Full") {
            // TODO if we have multiple batteries, we will return 0 if just one of them is charging
            return 0.0f;
        }

        // Prefer power_now (µW) when available.
        if (fs::exists(power_now)) {
            std::ifstream input(power_now);
            std::string line;
            if (std::getline(input, line)) {
                power_w += std::fabs(stof(line)) / 1000000.0f;
            }
            continue;
        }

        if (fs::exists(current_now) && fs::exists(voltage_now)) {
            float i_ua = 0.0f;
            float v_uv = 0.0f;

            {
                std::ifstream input(current_now);
                std::string line;
                if (std::getline(input, line)) {
                    i_ua = stof(line);
                }
            }
            {
                std::ifstream input(voltage_now);
                std::string line;
                if (std::getline(input, line)) {
                    v_uv = stof(line);
                }
            }

            power_w += (std::fabs(i_ua) * std::fabs(v_uv)) * 1e-12f;
        }
    }

    return power_w;
}


//TODO: This shares similar issues with pre-rewrite getPercents and doesn't work OoB on A16 either.
float BatteryStats::getTimeRemaining() {
    float current = 0.0f;
    float charge = 0.0f;

    for (int i = 0; i < batt_count; i++) {
        string syspath = battPath[i];
 
        string current_now = syspath + "/current_now";
        string charge_now = syspath + "/charge_now";
        string energy_now = syspath + "/energy_now";
        string voltage_now = syspath + "/voltage_now";
        string power_now = syspath + "/power_now";
       
        
        if (fs::exists(current_now)) {
            std::ifstream input(current_now);
            std::string line;
            if (std::getline(input, line)) {
                current_now_vec.push_back(std::fabs(stof(line)));
            }
        } else if (fs::exists(power_now) && fs::exists(voltage_now)) {
            float voltage = 0.0f;
            float power = 0.0f;

            {
                std::ifstream input_voltage(voltage_now);
                std::string line;
                if (std::getline(input_voltage, line)) {
                    voltage = stof(line);
                }
            }
            {
                std::ifstream input_power(power_now);
                std::string line;
                if (std::getline(input_power, line)) {
                    power = stof(line);
                }
            }
            if (voltage > 0.0f) {
                // (µW / µV) = µA
                current_now_vec.push_back(std::fabs(power) / voltage);
            }
        }

    	std::istringstream iss((std::stringstream{} << std::ifstream(charge_now).rdbuf()).str());
        if (int a; fs::exists(charge_now) && iss >> a) {
   
            std::ifstream input(charge_now);
            std::string line;
            if (std::getline(input, line)) {
            
                charge += stof(line);
            }
        } else if (fs::exists(energy_now) && fs::exists(voltage_now)) {
            float energy = 0.0f;
            float voltage = 0.0f;

            {
                std::ifstream input_energy(energy_now);
                std::string line;
                if (std::getline(input_energy, line)) {
                    energy = stof(line);
                }
            }
            {
                std::ifstream input_voltage(voltage_now);
                std::string line;
                if (std::getline(input_voltage, line)) {
                    voltage = stof(line);
                }
            }

            if (voltage > 0.0f) {
                // (µWh / µV) = µAh
                charge += energy / voltage;
            }
        }

        if (current_now_vec.size() > 25) {
            current_now_vec.erase(current_now_vec.begin());
        }
    }

    if (current_now_vec.empty()) {
        return 0.0f;
    }

    for (const auto& current_now_sample : current_now_vec) {
        current += current_now_sample;
    }
    current /= static_cast<float>(current_now_vec.size());

    if (current <= 0.0f) {
        return 0.0f;
    }

    return charge / current;
}

BatteryStats Battery_Stats;
