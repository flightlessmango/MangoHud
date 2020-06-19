#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

#include "pci_ids.h"

std::map<uint32_t /*vendor id*/, std::pair<std::string /*vendor desc*/, std::map<uint32_t /*device id*/, device>>> pci_ids;

std::istream& get_uncommented_line(std::istream& is, std::string &line)
{
    while (std::getline(is, line)) {
        auto c = line.find("#");
        if (c!=std::string::npos)
            line.erase(c, std::string::npos);
        if (line.size())
            break;
    }
    return is;
}

void parse_pciids()
{
    std::ifstream file("/usr/share/hwdata/pci.ids");
    if(file.fail()){
        std::ifstream file("/usr/share/misc/pci.ids");
        if (file.fail())
            printf("MANGOHUD: can't find file pci.ids\n");
        
    }

    std::string line;
    size_t tabs = 0;

    uint32_t ven_id = 0, dev_id = 0, subsys_ven_id = 0, subsys_dev_id = 0;
    std::string desc;
    std::stringstream ss;
    while(get_uncommented_line(file, line))
    {
        tabs = line.find_first_not_of("\t");

        ss.str(""); ss.clear();
        ss << line;

        switch(tabs)
        {
            case 0:
                ss >> std::hex >> ven_id;
                if (ven_id == 0xFFFF)
                    return;

                std::getline(ss, desc);
                pci_ids[ven_id].first = desc;
            break;
            case 1:
            {
                ss >> std::hex >> dev_id;
                std::getline(ss, desc); //TODO trim whitespace
                auto& dev = pci_ids[ven_id].second[dev_id];
                dev.desc = desc;
            }
            break;
            case 2:
            {
                ss >> std::hex >> subsys_ven_id;
                ss >> subsys_dev_id;
                std::getline(ss, desc);
                auto& dev = pci_ids[ven_id].second[dev_id];
                dev.subsys.push_back({subsys_ven_id, subsys_dev_id, desc});
            }
            break;
            default:
            break;
        }
    }
}
