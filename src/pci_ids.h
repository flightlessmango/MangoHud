#pragma once
#ifndef MANGOHUD_PCI_IDS_H
#define MANGOHUD_PCI_IDS_H

#include <map>
#include <vector>

struct subsys_device
{
    uint32_t vendor_id;
    uint32_t device_id;
    std::string desc;
};

struct device
{
    std::string desc;
    std::vector<subsys_device> subsys;
};

extern std::map<uint32_t /*vendor id*/, std::pair<std::string /*vendor desc*/, std::map<uint32_t /*device id*/, device>>> pci_ids;

void parse_pciids(void);

#endif //MANGOHUD_PCI_IDS_H
