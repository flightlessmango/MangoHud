#include "liquid.h"
#include <iostream>
#include "file_utils.h"
#include <spdlog/spdlog.h>
#include <filesystem.h>

LiquidStats::LiquidStats()
{
}

LiquidStats::~LiquidStats()
{
    for (size_t i = 0; i < this->devices.size(); ++i)
    {
        for (size_t j = 0; j < this->devices[i].sensors.size(); ++ j)
        {
            if (this->devices[i].sensors[j].source)
                fclose(this->devices[i].sensors[j].source);
        }
    }
}

static std::vector<std::pair<std::string, std::vector<std::string>>> parse_liquid_additional_sensors(const std::string &s)
{
    std::vector<std::pair<std::string, std::vector<std::string>>> vec;
    char delim = ';';
    char sensorsDelim = ',';
    size_t lastDelim = 0, newDelim = 0;

    while(lastDelim != s.size())
    {
        if (s.at(lastDelim) == ' ')
            lastDelim += 1;
        newDelim = s.find_first_of(delim, lastDelim);
        size_t space = s.find_first_of(" ", lastDelim);
        size_t sensorListStart = s.find_first_of("{", lastDelim + 1) + 1;
        size_t sensorListEnd = s.find_first_of("}", lastDelim);

        std::string deviceName;
        if (space == std::string::npos || space > sensorListStart)
            deviceName = s.substr(lastDelim, sensorListStart - lastDelim);
        else
            deviceName = s.substr(lastDelim, space - lastDelim);

        std::string sensorsSubstr = s.substr(sensorListStart, sensorListEnd - sensorListStart);
        std::vector<std::string> sensors;
        size_t lastSensorDelim = 0, newSensorDelim;

        while (lastSensorDelim != sensorsSubstr.size())
        {
            newSensorDelim = sensorsSubstr.find_first_of(sensorsDelim, lastSensorDelim);
            std::string sensor;
            if (sensorsSubstr.at(lastSensorDelim) == ' ')
                sensor = sensorsSubstr.substr(lastSensorDelim + 1, newSensorDelim - lastSensorDelim - 1);
            else
                sensor = sensorsSubstr.substr(lastSensorDelim, newSensorDelim - lastSensorDelim);

            if (lastSensorDelim != newSensorDelim)
            {
                sensor.at(0) = toupper(sensor.at(0));
                sensors.push_back(sensor);
            }


            if (newSensorDelim == std::string::npos)
                break;

            lastSensorDelim = newSensorDelim + 1;
        }

        if (lastDelim != newDelim)
            vec.push_back(std::pair<std::string, std::vector<std::string>> (deviceName, sensors));

        if (newDelim == std::string::npos)
            break;

        lastDelim = newDelim + 1;
    }
    return vec;
}

static std::string findPath(std::string deviceName)
{
    std::string hwmon = "/sys/class/hwmon/";
    std::string directory;

    for (auto &path : ghc::filesystem::directory_iterator(hwmon))
    {
        std::string currentEntry = path.path().filename();
        std::string currentDeviceName = read_line(hwmon + currentEntry + "/name");

        if (currentDeviceName == deviceName)
        {
            directory = hwmon + currentEntry + "/";
            break;
        }
    }
    return directory;
}

static bool getInput(WatercoolingDevice &device, std::string label)
{
    bool add = false;
    std::string input;
    for (auto &path : ghc::filesystem::directory_iterator(device.directory))
    {
        std::string fileName = path.path().filename();

        if (fileName.find("_label") == std::string::npos)
            continue;

        std::string currentLabel = read_line(device.directory + "/" + fileName);

        if (currentLabel.find(label) != std::string::npos)
        {
            auto uscore = fileName.find_first_of("_");

            if (uscore != std::string::npos)
            {
                fileName.erase(uscore, std::string::npos);
                input = device.directory + fileName + "_input";
                sensor newSensor;
                newSensor.source = fopen(input.c_str(), "r");
                if (fileName.find("temp") != std::string::npos)
                    newSensor.type = TEMP;

                else if (fileName.find("fan") != std::string::npos && label == "Flow")
                    newSensor.type = FLOW;

                else if (fileName.find("power") != std::string::npos)
                    newSensor.type = POWER;

                else if (fileName.find("fan") != std::string::npos && label.find("speed") != std::string::npos)
                    newSensor.type = RPM;

                std::string line = read_line(input);
                add = (newSensor.source != nullptr) && (line != "0") && (!line.empty());

                if (add)
                    device.sensors.push_back(newSensor);
                else
                    SPDLOG_ERROR("Could not find sensor input in {}", input);

                break;
            }
        }
    }
    return add;
}

void LiquidStats::AddDeviceAndSensor(std::vector<std::string> list, Type type)
{
    std::string label;

    switch (type)
    {
        case TEMP:
            label = "Coolant";
            break;

        case FLOW:
            label = "Flow";
            break;

        case POWER:
            break;

        case RPM:
            break;
    }

    if (list.size() == 1 && list[0] == "1") // No device name(s) provided for liquid_temp/liquid_flow param
    {
        std::vector<std::string> possibleDevices;
        switch (type)
        {
            case TEMP:
                possibleDevices = std::vector<std::string> {"d5next", "highflownext"};
                break;

            case FLOW:
                possibleDevices = std::vector<std::string> {"highflownext"};
                break;

            case POWER:
                // possibleDevices = std::vector<std::string> {"highflownext"};
                break;

            case RPM:
                // possibleDevices = std::vector<std::string> {"d5next"};
                break;
        }

        WatercoolingDevice *device = nullptr;

        for (size_t i = 0; i < this->devices.size(); ++i)
        {
            for (size_t j = 0; j < possibleDevices.size(); ++j)
            {
                if (this->devices[i].deviceName == possibleDevices[j])
                {
                    device = &this->devices[i];
                    getInput((*device), label);
                    break;
                }
            }
        }

        if (device == nullptr)
        {
            for (size_t j = 0; j < possibleDevices.size(); ++j)
            {
                std::string deviceName = possibleDevices[j];
                std::string path = findPath(possibleDevices[j]);

                if (!path.empty())
                {
                    device = new WatercoolingDevice;
                    device->deviceName = deviceName;
                    device->directory = path;
                    getInput((*device), label);
                    this->devices.push_back(*device);
                    delete device;
                }
            }
        }
    }
    else // Device name(s) provided for liquid_temp/liquid_flow, look for the specified device(s)
    {
        WatercoolingDevice *device = nullptr;

        for (size_t i = 0; i < list.size(); ++i)
        {
            for (size_t j = 0; j < this->devices.size(); ++j)
            {
                if (this->devices[j].deviceName == list[i])
                {
                    device = &this->devices[j];
                    getInput((*device), label);
                    break;
                }
            }

            if (device == nullptr)
            {
                std::string path = findPath(list[i]);

                if (!path.empty())
                {
                    device = new WatercoolingDevice;
                    device->deviceName = list[i];
                    device->directory = path;
                    getInput((*device), label);
                    this->devices.push_back(*device);
                    delete device;
                    device = nullptr; // Assign nullptr for next iteration
                }
                else
                    SPDLOG_ERROR("Could not find [{}] device [{}]", type == TEMP? "liquid_temp" : "liquid_flow", list[i]);
            }
        }
    }
}

void LiquidStats::AddAdditionalSensors(std::string s)//(std::vector<std::pair<std::string, std::vector<std::string>>> list)
{
    if (s == "1") // liquid_additional_sensors provided with no values
    {
        std::vector<std::pair<std::string, std::vector<std::string>>> devicesSensors {
                                                                                        {"d5next", {"Pump speed", "Fan speed"}},
                                                                                        {"highflownext", {"External sensor", "Dissipated power"}},
                                                                                        {"quadro", {"Sensor 1", "Sensor 2", "Sensor 3", "Sensor 4", "Fan 1 speed", "Fan 2 speed", "Fan 3 speed", "Fan 4 speed"}},
                                                                                        {"octo", {"Sensor 1", "Sensor 2", "Sensor 3", "Sensor 4", "Sensor 5", "Sensor 6", "Sensor 7", "Sensor 8", "Fan 1 speed", "Fan 2 speed", "Fan 3 speed", "Fan 4 speed",
                                                                                            "Fan 5 speed", "Fan 6 speed", "Fan 7 speed", "Fan 8 speed"}}
                                                                                     };

        for (size_t i = 0; i < devicesSensors.size(); ++i)
        {
            std::string deviceName = devicesSensors[i].first;

            WatercoolingDevice *device = nullptr;

            for (size_t j = 0; j < this->devices.size(); ++j)
            {
                if (this->devices[j].deviceName == deviceName)
                {
                    device = &this->devices[j];
                    for (size_t k = 0; k < devicesSensors[i].second.size(); ++k)
                        getInput((*device), devicesSensors[i].second[k]);

                    break;
                }
            }

            if (device == nullptr)
            {
                std::string path = findPath(deviceName);

                if (!path.empty())
                {
                    device = new WatercoolingDevice;
                    device->deviceName = deviceName;
                    device->directory = path;

                    for (size_t j = 0; j < devicesSensors[i].second.size(); ++j)
                        getInput((*device), devicesSensors[i].second[j]);

                    this->devices.push_back(*device);
                    delete device;
                    device = nullptr;
                }
            }
        }
    }
    else
    {
        std::vector<std::pair<std::string, std::vector<std::string>>> list = parse_liquid_additional_sensors(s);

        if (list.empty())
            return;

        for (size_t i = 0; i < list.size(); ++i)
            {
                std::string deviceName = list[i].first;

                WatercoolingDevice *device = nullptr;

                for (size_t j = 0; j < this->devices.size(); ++j)
                {
                    if (this->devices[j].deviceName == deviceName)
                    {
                        device = &this->devices[j];
                        for (size_t k = 0; k < list[i].second.size(); ++k)
                        {
                            bool found = getInput((*device), list[i].second[k]);
                            if (!found)
                                SPDLOG_ERROR("Could not find [{}] sensor for [{}]", list[i].second[k], deviceName);
                        }
                        break;
                    }
                }

                if (device == nullptr)
                {
                    std::string path = findPath(deviceName);

                    if (!path.empty())
                    {
                        device = new WatercoolingDevice;
                        device->deviceName = deviceName;
                        device->directory = path;
                        for (size_t j = 0; j < list[i].second.size(); ++j)
                        {
                            bool found = getInput((*device), list[i].second[j]);
                            if (!found)
                                SPDLOG_ERROR("Could not find [{}] sensor for [{}]", list[i].second[j], deviceName);
                        }
                        this->devices.push_back(*device);
                        delete device;
                        device = nullptr;
                    }
                    else
                        SPDLOG_ERROR("Could not find device [{}]", deviceName);
                }
            }
    }
}

// Order: temp, flow, power, rpm. Also when multiple sensors of the same type
// get accessed from the same device (eg. highflownext internal temp sensor and
// external temp sensor) they get grouped together
void LiquidStats::SortSensors()
{
    for (size_t i = 0; i < this->devices.size(); ++i)
    {
        std::vector<sensor> tempSensors;
        std::vector<sensor> flowSensors;
        std::vector<sensor> powerSensors;
        std::vector<sensor> rpmSensors;
        for (size_t j = 0; j < this->devices[i].sensors.size(); ++j)
        {
            switch (this->devices[i].sensors[j].type)
            {
                case TEMP:
                    tempSensors.push_back(this->devices[i].sensors[j]);
                    break;

                case FLOW:
                    flowSensors.push_back(this->devices[i].sensors[j]);
                    break;

                case POWER:
                    powerSensors.push_back(this->devices[i].sensors[j]);
                    break;

                case RPM:
                    rpmSensors.push_back(this->devices[i].sensors[j]);
                    break;
            }
        }
        std::vector<sensor> newSensorsVect;
        for (size_t j = 0; j < tempSensors.size(); ++j)
            newSensorsVect.push_back(tempSensors[j]);

        for (size_t j = 0; j < flowSensors.size(); ++j)
            newSensorsVect.push_back(flowSensors[j]);

        for (size_t j = 0; j < powerSensors.size(); ++j)
            newSensorsVect.push_back(powerSensors[j]);


        for (size_t j = 0; j < rpmSensors.size(); ++j)
            newSensorsVect.push_back(rpmSensors[j]);

        this->devices[i].sensors = newSensorsVect;
    }
}

bool LiquidStats::Init(std::vector<std::string> tempDevices, std::vector<std::string> flowDevices, std::string additionalSensors)//std::vector<std::pair<std::string, std::vector<std::string>>> additionalSensors
{
    if (!this->devices.empty())
        return true;

    if (tempDevices.empty() && flowDevices.empty() && additionalSensors.empty())
        return false;

    AddDeviceAndSensor(tempDevices, TEMP);
    AddDeviceAndSensor(flowDevices, FLOW);
    AddAdditionalSensors(additionalSensors);
    SortSensors();

    if (this->devices.empty())
    {
        SPDLOG_ERROR("Could not find watercooling devices");
        return false;
    }

    // Changes the devices names to all uppercase letters:
    for (size_t i = 0; i < this->devices.size(); ++i)
    {
        std::transform(this->devices[i].deviceName.begin(), this->devices[i].deviceName.end(), this->devices[i].deviceName.begin(), ::toupper);
    }

    bool sensorFound = false;

    for (size_t i = 0; i < this->devices.size(); ++i)
    {
        if (!this->devices[i].sensors.empty())
        {
            sensorFound = true;
            break;
        }

    }

    return sensorFound;
}

bool LiquidStats::ReadInputFile(struct sensor sensor, float &input)
{
    if (this->devices.empty())
        return false;

    rewind(sensor.source);
    fflush(sensor.source);

    bool ret = (fscanf(sensor.source, "%f", &input) == 1);

    switch(sensor.type)
    {
        case TEMP:
            input = input / 1000.0f;
            break;

        case FLOW:
            input = input / 10.0f;
            break;

        case POWER:
            input = input / 1000000.0f;
            break;

        case RPM:
            break;
    }

    return ret;
}

bool LiquidStats::Update()
{
    bool ret = false;
    for (size_t i = 0; i < this->devices.size(); ++i)
    {
        for (size_t j = 0; j < this->devices[i].sensors.size(); ++j)
        {
            float input = 0.0f;
            ret = ReadInputFile(this->devices[i].sensors[j], input);
            this->devices[i].sensors[j].input = input;
        }
    }
    return ret;
}

int LiquidStats::GetDeviceCount() const
{
    return this->devices.size();
}

std::vector< WatercoolingDevice > LiquidStats::GetDevicesData() const
{
    return this->devices;
}

std::unique_ptr<LiquidStats> liquidStats;
