#pragma once
#ifndef MANGOHUD_LIQUID_H
#define MANGOHUD_LIQUID_H

#include <cstdio>
#include <memory>
#include "string_utils.h"

enum Type {TEMP, FLOW, POWER, RPM};

struct sensor
{
    FILE *source = nullptr;
    float input;
    //int intInput;
    Type type;
};

struct WatercoolingDevice
{
    std::string deviceName, directory;
    std::vector<sensor> sensors;
};

class LiquidStats
{
public:
    LiquidStats();
    ~LiquidStats();
    void AddDeviceAndSensor(std::vector<std::string>, Type type);
    void AddAdditionalSensors(std::string s); //(std::vector<std::pair<std::string, std::vector<std::string>>>)
    void SortSensors();
    bool Init(std::vector<std::string> tempDevices, std::vector<std::string> flowDevices, std::string additionalSensors); //std::vector<std::pair<std::string, std::vector<std::string>>> additionalSensors
    bool Update();
    bool ReadInputFile(struct sensor sensor, float &input);
    float GetTemp();
    int GetDeviceCount() const;
    std::vector<WatercoolingDevice> GetDevicesData() const;

private:
    std::vector<WatercoolingDevice> devices;
};

extern std::unique_ptr<LiquidStats> liquidStats;

#endif // MANGOHUD_LIQUID_H
